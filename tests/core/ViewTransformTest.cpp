// S1-1.5: ViewTransform mappings (D-17). Exact at integer zoom, within 1e-9 otherwise.

#include "archmage/render/ViewTransform.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <random>

namespace archmage::render {
namespace {

using tiles::DPoint;
using tiles::DRect;
using tiles::DSize;
using tiles::IPoint;
using tiles::IRect;

bool near(double a, double b, double tolerance = 1e-9) {
    return std::abs(a - b) <= tolerance * std::max(1.0, std::abs(a));
}

TEST(ViewTransform, IdentityByDefault) {
    const ViewTransform v;
    EXPECT_EQ(v.scale(), 1.0);
    EXPECT_EQ(v.devicePixelRatio(), 1.0);
    EXPECT_EQ(v.documentToDevice(DPoint{12.5, -3.0}), (DPoint{12.5, -3.0}));
    EXPECT_EQ(v.logicalToDocument(DPoint{7.0, 9.0}), (DPoint{7.0, 9.0}));
}

TEST(ViewTransform, RoundTripIsExactAtIntegerZoom) {
    for (const double scale : {1.0, 2.0, 3.0, 4.0, 8.0, 16.0, 64.0, 256.0}) {
        for (const double dpr : {1.0, 2.0}) {
            const ViewTransform v{scale, DPoint{-37.0, 59.5}, dpr};
            for (double x = -300.0; x <= 300.0; x += 12.5) {
                for (double y = -200.0; y <= 200.0; y += 7.5) {
                    const DPoint document{x, y};
                    ASSERT_EQ(v.deviceToDocument(v.documentToDevice(document)), document)
                        << "scale " << scale << " point " << x << "," << y;
                    ASSERT_EQ(v.logicalToDocument(v.documentToLogical(document)), document);
                }
            }
        }
    }
}

TEST(ViewTransform, RoundTripWithinToleranceAtArbitraryZoom) {
    std::mt19937_64 rng{0x5eed1u};
    std::uniform_real_distribution<double> logScale{std::log(ViewTransform::kMinScale),
                                                    std::log(ViewTransform::kMaxScale)};
    std::uniform_real_distribution<double> coordinate{-300000.0, 300000.0};
    std::uniform_real_distribution<double> panDistribution{-1.0e6, 1.0e6};
    const double ratios[] = {1.0, 1.25, 1.5, 1.75, 2.0, 3.0};
    for (int i = 0; i < 100000; ++i) {
        const ViewTransform v{std::exp(logScale(rng)), DPoint{panDistribution(rng), panDistribution(rng)},
                              ratios[static_cast<std::size_t>(i) % std::size(ratios)]};
        const DPoint document{coordinate(rng), coordinate(rng)};
        const DPoint back = v.deviceToDocument(v.documentToDevice(document));
        ASSERT_TRUE(near(back.x, document.x) && near(back.y, document.y))
            << "scale " << v.scale() << " point " << document.x << "," << document.y;
        const DPoint logical = v.documentToLogical(document);
        const DPoint logicalBack = v.documentToLogical(v.logicalToDocument(logical));
        ASSERT_TRUE(near(logicalBack.x, logical.x) && near(logicalBack.y, logical.y));
    }
}

TEST(ViewTransform, ZoomKeepsTheAnchoredDocumentPoint) {
    const ViewTransform v{0.75, DPoint{120.0, -40.0}, 1.5};
    const DPoint anchor{333.0, 217.0};
    const DPoint before = v.deviceToDocument(anchor);
    for (const double s : {0.01, 0.5, 1.0, 2.0, 13.7, 200.0}) {
        const DPoint after = v.zoomedAbout(s, anchor).deviceToDocument(anchor);
        EXPECT_TRUE(near(after.x, before.x) && near(after.y, before.y)) << s;
    }
}

TEST(ViewTransform, ScaleIsClampedAndSanitized) {
    EXPECT_EQ((ViewTransform{1.0e9, {}, 1.0}.scale()), ViewTransform::kMaxScale);
    EXPECT_EQ((ViewTransform{1.0e-9, {}, 1.0}.scale()), ViewTransform::kMinScale);
    EXPECT_EQ((ViewTransform{0.0, {}, 1.0}.scale()), 1.0);
    EXPECT_EQ((ViewTransform{std::nan(""), {}, 1.0}.scale()), 1.0);
    EXPECT_EQ((ViewTransform{1.0, {}, -2.0}.devicePixelRatio()), 1.0);
    EXPECT_EQ(ViewTransform{}.zoomedAbout(1.0e6, DPoint{}).scale(), ViewTransform::kMaxScale);
}

TEST(ViewTransform, DevicePixelRatioChangeKeepsScaleAndCenter) {
    const DSize logicalViewport{800.0, 600.0};
    const ViewTransform v{2.0, DPoint{-1000.0, 250.0}, 1.0};
    const DPoint centerBefore = v.deviceToDocument(DPoint{400.0, 300.0});
    const ViewTransform moved = v.withDevicePixelRatio(1.5, logicalViewport);
    EXPECT_EQ(moved.scale(), 2.0);
    EXPECT_EQ(moved.devicePixelRatio(), 1.5);
    const DPoint centerAfter = moved.deviceToDocument(DPoint{600.0, 450.0});
    EXPECT_TRUE(near(centerAfter.x, centerBefore.x) && near(centerAfter.y, centerBefore.y));
}

TEST(ViewTransform, FitCentersAndCapsAtActualPixels) {
    const ViewTransform large = ViewTransform::fit(DRect{0, 0, 4000, 3000}, DSize{1000, 1000}, 1.0);
    EXPECT_EQ(large.scale(), 0.25);
    const DPoint center = large.deviceToDocument(DPoint{500, 500});
    EXPECT_EQ(center, (DPoint{2000, 1500}));

    const ViewTransform small = ViewTransform::fit(DRect{0, 0, 100, 50}, DSize{1000, 1000}, 2.0, 20.0);
    EXPECT_EQ(small.scale(), 1.0);
    EXPECT_EQ(small.documentToDevice(DPoint{50, 25}), (DPoint{500, 500}));

    const ViewTransform withMargin =
        ViewTransform::fit(DRect{-50, -50, 100, 100}, DSize{120, 220}, 1.0, 10.0);
    EXPECT_EQ(withMargin.scale(), 1.0);
    const ViewTransform shrunk = ViewTransform::fit(DRect{0, 0, 400, 200}, DSize{220, 220}, 1.0, 10.0);
    EXPECT_EQ(shrunk.scale(), 0.5);
}

TEST(ViewTransform, ActualPixelsCentersTheDocumentPoint) {
    const ViewTransform v = ViewTransform::actualPixels(DPoint{1234.5, -77.25}, DSize{1920, 1200}, 1.5);
    EXPECT_EQ(v.scale(), 1.0);
    EXPECT_EQ(v.documentToDevice(DPoint{1234.5, -77.25}), (DPoint{960, 600}));
}

TEST(ViewTransform, VisibleDocumentRect) {
    const ViewTransform v{2.0, DPoint{100.0, 50.0}, 1.0};
    EXPECT_EQ(v.visibleDocumentRect(DSize{400, 300}), (DRect{-50.0, -25.0, 200.0, 150.0}));
}

TEST(Geometry, PixelCenterConvention) {
    EXPECT_EQ(tiles::pixelAt(DPoint{-0.5, 0.0}), (IPoint{-1, 0}));
    EXPECT_EQ(tiles::pixelAt(DPoint{0.999, -1.0}), (IPoint{0, -1}));
    EXPECT_EQ(tiles::pixelCenter(IPoint{0, -3}), (DPoint{0.5, -2.5}));
    EXPECT_EQ((DRect{-0.5, 0.25, 2.0, 1.0}.coveringPixels()), (IRect{-1, 0, 2, 2}));
    EXPECT_TRUE((DRect{0, 0, 0, 5}.coveringPixels().empty()));
}

TEST(Geometry, HalfOpenRectangles) {
    const IRect a{-10, -10, 10, 10};
    EXPECT_EQ(a.width(), 20);
    EXPECT_TRUE(a.contains(IPoint{-10, 9}));
    EXPECT_FALSE(a.contains(IPoint{10, 0}));
    EXPECT_EQ(a.intersected(IRect{5, 5, 50, 50}), (IRect{5, 5, 10, 10}));
    EXPECT_TRUE(a.intersected(IRect{10, 0, 20, 5}).empty());
    EXPECT_EQ(a.united(IRect{}), a);
    EXPECT_EQ(IRect{}.united(a), a);
    EXPECT_EQ(a.united(IRect{20, 20, 30, 30}), (IRect{-10, -10, 30, 30}));
}

} // namespace
} // namespace archmage::render
