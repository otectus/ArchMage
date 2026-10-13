// S1-6.3 (spec §7.2, §15.3): the Gaussian blur's required input region is its output grown by the radius,
// so a blur evaluated tile by tile equals the whole-image reference; the plan allows 1 LSB in uint8.

#include "archmage/commands/Command.h"
#include "archmage/render/Blur.h"
#include "archmage/render/CpuEvaluator.h"
#include "archmage/render/Kernels.h"

#include <gtest/gtest.h>

#include <cmath>
#include <numeric>

namespace archmage::render {
namespace {

// High-contrast features exactly on tile borders: a white bar ending at x = 128, a one-pixel line on
// y = 255 / 256, a square around the corner (384, 256), a checkerboard straddling (256, 128), on black.
document::DocumentState seamFixture() {
    auto created = commands::createDocument(commands::CommandEnvelope::make(
        "archmage.document.create", {{"width", 600}, {"height", 450}, {"background", "white"}}));
    EXPECT_TRUE(created);
    document::DocumentState doc = created->state;
    auto layer = std::make_shared<document::Layer>(*document::makeRasterLayer(doc, "Features"));
    layer->offset = {0, 0};
    auto content = std::make_shared<document::RasterContent>(*layer->content);
    const auto white = [](std::int64_t x, std::int64_t y) {
        if (x >= 100 && x < 128 && y >= 20 && y < 400) {
            return true;
        }
        if (y == 255 || y == 256) {
            return x >= 10 && x < 590;
        }
        if (x >= 370 && x < 398 && y >= 242 && y < 270) {
            return true;
        }
        if (x >= 240 && x < 272 && y >= 112 && y < 144) {
            return ((x / 4) + (y / 4)) % 2 == 0;
        }
        return false;
    };
    for (std::int64_t ty = 0; ty < 4; ++ty) {
        for (std::int64_t tx = 0; tx < 5; ++tx) {
            tiles::TileBuffer buffer(tiles::SampleType::UInt8, 4);
            for (int y = 0; y < tiles::kTileSize; ++y) {
                for (int x = 0; x < tiles::kTileSize; ++x) {
                    const bool on = white(tx * tiles::kTileSize + x, ty * tiles::kTileSize + y);
                    for (std::size_t p = 0; p < 3; ++p) {
                        buffer.setSample(p, x, y, on ? 1.0f : 0.0f);
                    }
                    buffer.setSample(3, x, y, 1.0f);
                }
            }
            content->tiles.set({tx, ty}, std::move(buffer).seal());
        }
    }
    layer->content = std::move(content);
    doc.layers.push_back(std::move(layer));
    return doc;
}

std::uint8_t encode(const float *premultiplied, int channel) {
    float pixel[4] = {premultiplied[0], premultiplied[1], premultiplied[2], premultiplied[3]};
    unpremultiplyRow(std::span<float>(pixel, 4));
    std::byte out{};
    tiles::storeSample(tiles::SampleType::UInt8, pixel[channel], &out);
    return static_cast<std::uint8_t>(out);
}

TEST(GaussianBlur, DeclaresItsHaloAndRequiredRegion) {
    const document::DocumentState doc = seamFixture();
    const auto blur = std::make_shared<GaussianBlurNode>(compileGraph(doc), 4.0);
    EXPECT_EQ(blur->radius(), 12);
    EXPECT_EQ(blur->halo(), 12);
    EXPECT_EQ(blur->operationClass(), OperationClass::BoundedNeighborhood);
    EXPECT_EQ(blur->requiredInputRegion({128, 128, 256, 256}, 0), (base::IRect{116, 116, 268, 268}));
    const auto &w = blur->weights();
    EXPECT_NEAR(std::accumulate(w.begin(), w.end(), 0.0), 1.0, 1e-6);
    EXPECT_EQ(w.front(), w.back()) << "symmetric";
    const GaussianBlurNode other(compileGraph(doc), 4.5);
    EXPECT_NE(blur->cacheKey({0, 0, 128, 128}), other.cacheKey({0, 0, 128, 128}));
}

class BlurSeams : public ::testing::TestWithParam<double> {};

TEST_P(BlurSeams, TiledEqualsTheWholeImage) {
    const document::DocumentState doc = seamFixture();
    const auto blur = std::make_shared<GaussianBlurNode>(compileGraph(doc), GetParam());
    const base::IRect canvas = doc.canvasRect();

    ImageBuffer whole(canvas);
    ASSERT_TRUE(blur->evaluate(EvaluationContext{}, whole));

    std::vector<tiles::TileCoord> coords;
    const base::IRect range = tiles::tileRangeCovering(canvas);
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            coords.push_back({tx, ty});
        }
    }
    CpuEvaluator evaluator(CpuEvaluator::Options{.threads = 4});
    const auto tiled = evaluator.renderTiles(*blur, coords, EvaluationContext{});
    int maxLsb = 0;
    double maxFloat = 0.0;
    std::size_t compared = 0;
    for (std::size_t t = 0; t < coords.size(); ++t) {
        ASSERT_TRUE(tiled[t]);
        const ImageBuffer &tile = **tiled[t];
        const base::IRect part = tile.region().intersected(canvas);
        for (std::int64_t y = part.y0; y < part.y1; ++y) {
            for (std::int64_t x = part.x0; x < part.x1; ++x) {
                const float *a = whole.pixel(x, y);
                const float *b = tile.pixel(x, y);
                for (int c = 0; c < 4; ++c) {
                    maxFloat = std::max(maxFloat, static_cast<double>(std::fabs(a[c] - b[c])));
                    maxLsb = std::max(maxLsb, std::abs(int(encode(a, c)) - int(encode(b, c))));
                }
                ++compared;
            }
        }
    }
    EXPECT_EQ(compared, static_cast<std::size_t>(canvas.width() * canvas.height()));
    RecordProperty("max_lsb", maxLsb);
    RecordProperty("max_float_difference", std::to_string(maxFloat));
    EXPECT_LE(maxLsb, 1);
    EXPECT_EQ(maxFloat, 0.0) << "each pixel sums the same inputs in the same order";
    // The features are blurred: the line on the tile border spreads into both tiles.
    EXPECT_GT(encode(whole.pixel(300, 254), 0), 0);
    EXPECT_LT(encode(whole.pixel(300, 255), 0), 255);
}

INSTANTIATE_TEST_SUITE_P(Sigmas, BlurSeams, ::testing::Values(0.6, 2.0, 7.5),
                         [](const auto &suite) { return "sigma_" + std::to_string(static_cast<int>(suite.param * 10)); });

TEST(GaussianBlur, PreservesTotalIntensityOfAPoint) {
    auto created = commands::createDocument(commands::CommandEnvelope::make(
        "archmage.document.create",
        {{"width", 256}, {"height", 256}, {"sampleType", "float32"}, {"background", "transparent"}}));
    ASSERT_TRUE(created);
    document::DocumentState doc = created->state;
    auto layer = std::make_shared<document::Layer>(*doc.layers[0]);
    auto content = std::make_shared<document::RasterContent>(*layer->content);
    tiles::TileBuffer buffer(tiles::SampleType::Float32, 4);
    for (std::size_t p = 0; p < 4; ++p) {
        buffer.setSample(p, 127, 127, 1.0f); // at a tile corner: its blur spreads into four tiles
    }
    content->tiles.set({0, 0}, std::move(buffer).seal());
    layer->content = std::move(content);
    doc.layers[0] = std::move(layer);
    const GaussianBlurNode blur(compileGraph(doc), 3.0);
    ImageBuffer out(base::IRect{0, 0, 256, 256});
    ASSERT_TRUE(blur.evaluate(EvaluationContext{}, out));
    double alpha = 0.0;
    for (std::int64_t y = 0; y < 256; ++y) {
        for (std::int64_t x = 0; x < 256; ++x) {
            alpha += out.pixel(x, y)[3];
        }
    }
    EXPECT_NEAR(alpha, 1.0, 1e-5);
    EXPECT_GT(out.pixel(128, 128)[3], 0.0f);
    EXPECT_EQ(out.pixel(127, 127)[3], out.pixel(127, 127)[3]);
}

} // namespace
} // namespace archmage::render
