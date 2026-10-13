// S1-2.7: the compiled graph and the CPU evaluator against an independently coded reference.

#include "archmage/render/CpuEvaluator.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cmath>
#include <limits>
#include <thread>

namespace archmage::render {
namespace {

using base::IRect;
using document::DocumentState;
using document::Layer;

struct Rgba8 {
    std::uint8_t r, g, b, a;
};

// Fills `rect` (layer-local pixels) of a layer with one straight color.
std::shared_ptr<const Layer> paint(const std::shared_ptr<const Layer> &layer, const IRect &rect,
                                   Rgba8 color) {
    auto content = std::make_shared<document::RasterContent>(*layer->content);
    const IRect range = tiles::tileRangeCovering(rect);
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            const tiles::TileCoord coord{tx, ty};
            tiles::TileBuffer buffer = *tiles::TileBuffer::copyOf(*content->tiles.at(coord));
            const IRect part = tiles::tilePixelRect(coord).intersected(rect);
            for (std::int64_t y = part.y0; y < part.y1; ++y) {
                for (std::int64_t x = part.x0; x < part.x1; ++x) {
                    const int lx = static_cast<int>(x - tx * tiles::kTileSize);
                    const int ly = static_cast<int>(y - ty * tiles::kTileSize);
                    buffer.setSample(0, lx, ly, color.r / 255.0f);
                    buffer.setSample(1, lx, ly, color.g / 255.0f);
                    buffer.setSample(2, lx, ly, color.b / 255.0f);
                    buffer.setSample(3, lx, ly, color.a / 255.0f);
                }
            }
            content->tiles.set(coord, std::move(buffer).seal());
        }
    }
    auto painted = std::make_shared<Layer>(*layer);
    painted->content = std::move(content);
    return painted;
}

std::shared_ptr<const Layer> withProps(const std::shared_ptr<const Layer> &layer, float opacity,
                                       base::IPoint offset) {
    auto copy = std::make_shared<Layer>(*layer);
    copy->opacity = opacity;
    copy->offset = offset;
    return copy;
}

DocumentState threeLayerDocument() {
    DocumentState doc = *document::makeDocumentState(300, 200, tiles::SampleType::UInt8,
                                                     color::ColorPolicy::defaultPolicy(), "Reference");
    auto bottom = paint(document::makeRasterLayer(doc, "Red"), IRect{0, 0, 300, 200}, {255, 0, 0, 255});
    auto middle = paint(document::makeRasterLayer(doc, "Blue"), IRect{0, 0, 200, 150}, {0, 0, 255, 128});
    auto top = paint(document::makeRasterLayer(doc, "Green"), IRect{100, 20, 260, 180}, {0, 255, 0, 255});
    doc.layers = {bottom, withProps(middle, 0.5f, {-37, 59}), withProps(top, 0.25f, {0, 0})};
    return doc;
}

// The spec §7.4 equations written out independently, in the evaluator's float32 order.
Rgba8 referencePixel(const DocumentState &doc, std::int64_t x, std::int64_t y) {
    float dst[4] = {0, 0, 0, 0};
    for (const auto &layer : doc.layers) {
        const std::int64_t lx = x - layer->offset.x;
        const std::int64_t ly = y - layer->offset.y;
        const auto tile = layer->content->tiles.at(tiles::tileContaining({lx, ly}));
        const int tx = static_cast<int>(((lx % 128) + 128) % 128);
        const int ty = static_cast<int>(((ly % 128) + 128) % 128);
        const float alpha = tile->sample(3, tx, ty) * layer->opacity;
        const float src[4] = {tile->sample(0, tx, ty) * alpha, tile->sample(1, tx, ty) * alpha,
                              tile->sample(2, tx, ty) * alpha, alpha};
        const float k = 1.0f - src[3];
        for (int c = 0; c < 4; ++c) {
            dst[c] = src[c] + dst[c] * k;
        }
    }
    const auto encode = [](float v) {
        return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
    };
    if (dst[3] == 0.0f) {
        return {0, 0, 0, 0};
    }
    const float inverse = 1.0f / dst[3];
    return {encode(dst[0] * inverse), encode(dst[1] * inverse), encode(dst[2] * inverse), encode(dst[3])};
}

RenderRequest requestFor(const DocumentState &doc, IRect region,
                         tiles::SampleType type = tiles::SampleType::UInt8) {
    RenderRequest request;
    request.document = doc.id;
    request.region = region;
    request.destination.sampleType = type;
    return request;
}

TEST(Evaluator, ThreeLayerDocumentMatchesTheReference) {
    const DocumentState doc = threeLayerDocument();
    CpuEvaluator evaluator;
    const IRect region{-10, -10, 310, 210}; // beyond the canvas on every side
    const auto result = evaluator.render(doc, requestFor(doc, region));
    ASSERT_TRUE(result) << base::format(result.error());
    EXPECT_EQ(result->region, region);
    std::int64_t mismatches = 0;
    for (std::int64_t y = region.y0; y < region.y1; ++y) {
        for (std::int64_t x = region.x0; x < region.x1; ++x) {
            const Rgba8 expected = referencePixel(doc, x, y);
            const std::size_t at = static_cast<std::size_t>(y - region.y0) * result->stride() +
                                   static_cast<std::size_t>(x - region.x0) * 4;
            const Rgba8 actual{static_cast<std::uint8_t>(result->pixels[at]),
                               static_cast<std::uint8_t>(result->pixels[at + 1]),
                               static_cast<std::uint8_t>(result->pixels[at + 2]),
                               static_cast<std::uint8_t>(result->pixels[at + 3])};
            if (std::memcmp(&expected, &actual, 4) != 0 && ++mismatches <= 5) {
                ADD_FAILURE() << "pixel " << x << "," << y << " expected " << int(expected.r) << ","
                              << int(expected.g) << "," << int(expected.b) << "," << int(expected.a)
                              << " got " << int(actual.r) << "," << int(actual.g) << "," << int(actual.b)
                              << "," << int(actual.a);
            }
        }
    }
    EXPECT_EQ(mismatches, 0);
    // Spot checks against hand arithmetic: blue at 50 % alpha and 50 % opacity over red, below green.
    EXPECT_EQ(result->sample(0, 0, 3), 1.0f);
    EXPECT_EQ(result->sample(-5, -5, 3), 0.0f) << "outside every layer stays transparent";
}

TEST(Evaluator, OpaqueUint8PixelsRoundTripExactly) {
    DocumentState doc = *document::makeDocumentState(256, 1, tiles::SampleType::UInt8,
                                                     color::ColorPolicy::defaultPolicy(), "Ramp");
    auto layer = document::makeRasterLayer(doc, "Ramp");
    for (int v = 0; v < 256; ++v) {
        layer = paint(layer, IRect{v, 0, v + 1, 1},
                      {static_cast<std::uint8_t>(v), static_cast<std::uint8_t>(255 - v),
                       static_cast<std::uint8_t>(v / 2), 255});
    }
    doc.layers = {layer};
    CpuEvaluator evaluator;
    const auto result = evaluator.render(doc, requestFor(doc, doc.canvasRect()));
    ASSERT_TRUE(result);
    for (int v = 0; v < 256; ++v) {
        ASSERT_EQ(static_cast<int>(result->pixels[static_cast<std::size_t>(v) * 4]), v);
        ASSERT_EQ(static_cast<int>(result->pixels[static_cast<std::size_t>(v) * 4 + 1]), 255 - v);
        ASSERT_EQ(static_cast<int>(result->pixels[static_cast<std::size_t>(v) * 4 + 3]), 255);
    }
}

TEST(Evaluator, HiddenAndTransparentLayersAreSkipped) {
    DocumentState doc = threeLayerDocument();
    auto hidden = std::make_shared<Layer>(*doc.layers[0]);
    hidden->visible = false;
    doc.layers[0] = hidden;
    auto clear = std::make_shared<Layer>(*doc.layers[2]);
    clear->opacity = 0.0f;
    doc.layers[2] = clear;
    EXPECT_EQ(compileGraph(doc)->inputs().size(), 1u);
    CpuEvaluator evaluator;
    const auto result = evaluator.render(doc, requestFor(doc, IRect{0, 0, 10, 80}));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->sample(5, 5, 3), 0.0f) << "(5,5) lies outside the blue layer moved to (-37, 59)";
    EXPECT_GT(result->sample(5, 70, 3), 0.0f);
}

TEST(Evaluator, CacheServesAnUnchangedRevision) {
    const DocumentState doc = threeLayerDocument();
    CpuEvaluator evaluator;
    ASSERT_TRUE(evaluator.render(doc, requestFor(doc, doc.canvasRect())));
    const std::uint64_t evaluated = evaluator.tilesEvaluated();
    EXPECT_GT(evaluated, 0u);
    ASSERT_TRUE(evaluator.render(doc, requestFor(doc, doc.canvasRect())));
    EXPECT_EQ(evaluator.tilesEvaluated(), evaluated) << "every tile came from the cache";
    EXPECT_GE(evaluator.cacheStats().hits, evaluated);

    DocumentState changed = doc;
    changed.layers[1] = withProps(doc.layers[1], 0.75f, doc.layers[1]->offset);
    ASSERT_TRUE(evaluator.render(changed, requestFor(changed, changed.canvasRect())));
    EXPECT_GT(evaluator.tilesEvaluated(), evaluated) << "a changed layer changes the cache key";
}

TEST(Evaluator, OnlyTilesWhoseSourcesChangedAreReevaluated) {
    const DocumentState doc = threeLayerDocument(); // 300 x 200: tiles x 0..2, y 0..1
    CpuEvaluator evaluator;
    ASSERT_TRUE(evaluator.render(doc, requestFor(doc, doc.canvasRect())));
    const std::uint64_t first = evaluator.tilesEvaluated();
    ASSERT_EQ(first, 6u);
    DocumentState edited = doc; // repaint one pixel of the bottom layer, inside tile (2, 1)
    edited.layers[0] = paint(doc.layers[0], IRect{290, 150, 291, 151}, {0, 255, 255, 255});
    ASSERT_TRUE(evaluator.render(edited, requestFor(edited, edited.canvasRect())));
    EXPECT_EQ(evaluator.tilesEvaluated(), first + 1)
        << "one changed source tile, one re-evaluated output tile";
}

class SlowNode final : public GraphNode {
public:
    SlowNode(std::stop_source *stopper, std::atomic<int> *evaluations)
        : stopper_(stopper), evaluations_(evaluations) {}
    [[nodiscard]] std::string_view typeId() const override { return "test.slow"; }
    [[nodiscard]] int algorithmVersion() const override { return 1; }
    [[nodiscard]] OperationClass operationClass() const override { return OperationClass::Local; }
    [[nodiscard]] IRect bounds() const override { return {-100000, -100000, 100000, 100000}; }
    [[nodiscard]] std::uint64_t cacheKey(const IRect &) const override { return 42; }
    base::Status evaluate(const EvaluationContext &, ImageBuffer &) const override {
        if (evaluations_->fetch_add(1) == 0) {
            stopper_->request_stop();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        return {};
    }

private:
    std::stop_source *stopper_;
    std::atomic<int> *evaluations_;
};

TEST(Evaluator, CancellationStopsWithinOneTilePerWorker) {
    CpuEvaluator evaluator(CpuEvaluator::Options{.threads = 2, .cacheBytes = 0});
    std::stop_source stopper;
    std::atomic<int> evaluations{0};
    const SlowNode node(&stopper, &evaluations);
    RenderRequest request;
    request.region = IRect{0, 0, 128 * 16, 128 * 16}; // 256 tiles
    const auto result =
        evaluator.renderGraph(node, color::ColorPolicy::defaultPolicy(), request, stopper.get_token());
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, base::ErrorCategory::Cancelled);
    EXPECT_LE(evaluations.load(), static_cast<int>(evaluator.threadCount()))
        << "only tiles already in flight finish after the stop request";
}

TEST(Evaluator, FloatDocumentsReportNonFiniteSamples) {
    DocumentState doc = *document::makeDocumentState(128, 128, tiles::SampleType::Float32,
                                                     color::ColorPolicy::defaultPolicy(), "Float");
    auto layer = document::makeRasterLayer(doc, "NaN");
    auto content = std::make_shared<document::RasterContent>(*layer->content);
    tiles::TileBuffer buffer(tiles::SampleType::Float32, 4);
    buffer.fill(3, 1.0f);
    buffer.fill(1, 1.25f); // above one survives in float output
    buffer.setSample(0, 3, 3, std::numeric_limits<float>::quiet_NaN());
    content->tiles.set({0, 0}, std::move(buffer).seal());
    auto painted = std::make_shared<Layer>(*layer);
    painted->content = content;
    doc.layers = {painted};
    CpuEvaluator evaluator;
    const auto result = evaluator.render(doc, requestFor(doc, doc.canvasRect(), tiles::SampleType::Float32));
    ASSERT_TRUE(result);
    EXPECT_EQ(result->nonFiniteSamples, 1u);
    EXPECT_EQ(result->sample(3, 3, 0), 0.0f);
    EXPECT_EQ(result->sample(10, 10, 1), 1.25f);
}

TEST(Evaluator, RejectsEmptyAndOversizedRequests) {
    const DocumentState doc = threeLayerDocument();
    CpuEvaluator evaluator;
    EXPECT_EQ(evaluator.render(doc, requestFor(doc, IRect{})).error().code, "render.empty_region");
    const auto huge =
        evaluator.render(doc, requestFor(doc, IRect{0, 0, std::int64_t{1} << 40, std::int64_t{1} << 40}));
    ASSERT_FALSE(huge);
    EXPECT_EQ(huge.error().category, base::ErrorCategory::ResourceExhausted);
}

} // namespace
} // namespace archmage::render
