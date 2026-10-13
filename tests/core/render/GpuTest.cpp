// S1-6.1 and S1-6.2 (D-12, ADR-0012, spec §7.5): the headless Vulkan device and the GPU source-over slice,
// compared with the CPU reference: uint8 results exactly, float32 within 1e-6 absolute. Every test skips
// cleanly when no Vulkan device is available.

#include "archmage/brush/Stroke.h"
#include "archmage/commands/MutationExecutor.h"
#include "archmage/gpu/GpuEvaluator.h"
#include "archmage/render/Kernels.h"
#include "archmage/tiles/ResourceCoordinator.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <random>

namespace archmage::gpu {
namespace {

using commands::CommandEnvelope;

std::shared_ptr<GpuContext> contextOrSkip() {
    auto context = GpuContext::create(GpuContext::Options{});
    if (!context) {
        return nullptr;
    }
    return *context;
}

#define REQUIRE_GPU(context)                                                                                         \
    auto context = contextOrSkip();                                                                                  \
    if (!context) {                                                                                                  \
        GTEST_SKIP() << "no Vulkan device: " << base::format(GpuContext::create(GpuContext::Options{}).error());    \
    }

std::vector<tiles::TileCoord> canvasTiles(const document::DocumentState &state) {
    std::vector<tiles::TileCoord> coords;
    const base::IRect range = tiles::tileRangeCovering(state.canvasRect());
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            coords.push_back({tx, ty});
        }
    }
    return coords;
}

struct Comparison {
    double maxFloatDifference = 0.0;
    std::size_t uint8Mismatches = 0;
    std::size_t pixels = 0;
};

// Both premultiplied tile sets, and their straight uint8 encodings as an export would produce them.
Comparison compare(const std::vector<base::Result<std::shared_ptr<const render::ImageBuffer>>> &cpu,
                   const std::vector<base::Result<std::shared_ptr<const render::ImageBuffer>>> &gpu) {
    Comparison result;
    EXPECT_EQ(cpu.size(), gpu.size());
    for (std::size_t t = 0; t < cpu.size() && t < gpu.size(); ++t) {
        EXPECT_TRUE(cpu[t] && gpu[t]);
        if (!cpu[t] || !gpu[t]) {
            continue;
        }
        const std::vector<float> &a = (*cpu[t])->data();
        const std::vector<float> &b = (*gpu[t])->data();
        EXPECT_EQ(a.size(), b.size());
        for (std::size_t i = 0; i + 3 < a.size() && i + 3 < b.size(); i += 4) {
            float pa[4], pb[4];
            for (int c = 0; c < 4; ++c) {
                pa[c] = a[i + static_cast<std::size_t>(c)];
                pb[c] = b[i + static_cast<std::size_t>(c)];
                result.maxFloatDifference =
                    std::max(result.maxFloatDifference, static_cast<double>(std::fabs(pa[c] - pb[c])));
            }
            render::unpremultiplyRow(std::span<float>(pa, 4));
            render::unpremultiplyRow(std::span<float>(pb, 4));
            for (int c = 0; c < 4; ++c) {
                std::byte ea{}, eb{};
                tiles::storeSample(tiles::SampleType::UInt8, pa[c], &ea);
                tiles::storeSample(tiles::SampleType::UInt8, pb[c], &eb);
                result.uint8Mismatches += ea != eb ? 1 : 0;
            }
            ++result.pixels;
        }
    }
    return result;
}

TEST(GpuContext, CreatesAndDestroysADeviceWithoutValidationErrors) {
    const std::uint64_t before = GpuContext::totalValidationErrors();
    {
        REQUIRE_GPU(context);
        RecordProperty("device", context->info().name);
        RecordProperty("validation", context->validationEnabled() ? "on" : "off");
        EXPECT_FALSE(context->info().name.empty());
        EXPECT_GT(context->info().deviceLocalBytes, 0u);
        EXPECT_EQ(tiles::ResourceCoordinator::global().limit(tiles::Budget::Vram), context->info().vramBudget);
        EXPECT_FALSE(context->lost());
    }
    EXPECT_EQ(GpuContext::totalValidationErrors(), before) << "validation errors while creating or destroying";
}

TEST(GpuContext, ForcedCpuIsUnavailable) {
    ::setenv("ARCHMAGE_FORCE_CPU", "1", 1);
    auto context = GpuContext::create(GpuContext::Options{});
    ::unsetenv("ARCHMAGE_FORCE_CPU");
    ASSERT_FALSE(context);
    EXPECT_EQ(context.error().category, base::ErrorCategory::ProviderUnavailable);
    EXPECT_EQ(context.error().code, "gpu.unavailable");
}

// The AT-002 fixture: a stroke across four tile corners on a layer at (−37, 59) over a white background.
TEST(GpuEvaluator, MatchesTheCpuOnTheAt002Fixture) {
    REQUIRE_GPU(context);
    auto created = commands::createDocument(CommandEnvelope::make(
        "archmage.document.create", {{"width", 1000}, {"height", 1000}, {"background", "white"}}));
    ASSERT_TRUE(created);
    document::DocumentState doc = created->state;
    auto paint = std::make_shared<document::Layer>(*document::makeRasterLayer(doc, "Paint"));
    paint->offset = {-37, 59};
    paint->opacity = 0.8f;
    doc.layers.push_back(paint);
    commands::MutationExecutor executor(doc, "New Document");
    brush::BrushSettings settings;
    settings.color = {0.15f, 0.3f, 0.85f};
    settings.size = 36.0;
    settings.hardness = 0.6;
    std::vector<brush::StrokeSample> samples;
    for (int i = 0; i <= 300; ++i) {
        const double t = i / 300.0;
        samples.push_back({{50.0 + 550.0 * t, 146.0 + 550.0 * t}, static_cast<float>(0.3 + 0.7 * t), i * 4'166'667LL});
    }
    nlohmann::json parameters = settings.toJson();
    parameters["samples"] = brush::samplesToJson(samples);
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.raster.paintStroke", parameters, {paint->id.toString()})));
    const auto state = executor.current()->state;

    const auto root = render::compileGraph(*state);
    ASSERT_TRUE(GpuEvaluator::supports(*root));
    const auto coords = canvasTiles(*state);
    render::CpuEvaluator cpu(render::CpuEvaluator::Options{.threads = 2});
    GpuEvaluator gpu(context, cpu);
    const auto expected = cpu.renderTiles(*root, coords, render::EvaluationContext{});
    const auto actual = gpu.renderTiles(*root, coords, render::EvaluationContext{});
    EXPECT_EQ(gpu.stats().gpuTiles, coords.size());
    const Comparison result = compare(expected, actual);
    RecordProperty("max_float_difference", std::to_string(result.maxFloatDifference));
    EXPECT_EQ(result.uint8Mismatches, 0u) << "uint8 must match exactly";
    EXPECT_LE(result.maxFloatDifference, 1e-6);
    EXPECT_EQ(context->validationErrors(), 0u) << (context->validationLog().empty() ? "" : context->validationLog()[0]);
}

// A random 20-layer stack: offsets, opacities (some 0 and 1), hidden layers, dense random tiles, uniform
// tiles; in float32 also values outside [0, 1] and non-finite samples.
document::DocumentState randomStack(tiles::SampleType type, std::uint32_t seed) {
    std::mt19937 random(seed);
    auto created = commands::createDocument(CommandEnvelope::make(
        "archmage.document.create", {{"width", 700},
                                     {"height", 520},
                                     {"sampleType", std::string(tiles::sampleTypeName(type))},
                                     {"background", "transparent"}}));
    EXPECT_TRUE(created);
    document::DocumentState doc = created->state;
    doc.layers.clear();
    std::uniform_int_distribution<int> offset(-200, 200);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);
    std::uniform_real_distribution<float> wide(-0.25f, 1.25f);
    for (int l = 0; l < 20; ++l) {
        auto layer = std::make_shared<document::Layer>(*document::makeRasterLayer(doc, "Layer " + std::to_string(l)));
        layer->offset = {offset(random), offset(random)};
        layer->opacity = l == 3 ? 0.0f : (l == 7 ? 1.0f : unit(random));
        layer->visible = l != 11;
        auto content = std::make_shared<document::RasterContent>(*layer->content);
        for (std::int64_t ty = 0; ty < 5; ++ty) {
            for (std::int64_t tx = 0; tx < 6; ++tx) {
                if (random() % 3 == 0) {
                    continue; // sparse
                }
                tiles::TileBuffer buffer(type, 4);
                if (random() % 5 == 0) {
                    for (std::size_t p = 0; p < 4; ++p) {
                        buffer.fill(p, unit(random)); // seals to a uniform tile
                    }
                } else {
                    for (int y = 0; y < tiles::kTileSize; ++y) {
                        for (int x = 0; x < tiles::kTileSize; ++x) {
                            for (std::size_t p = 0; p < 4; ++p) {
                                float v = type == tiles::SampleType::Float32 ? wide(random) : unit(random);
                                if (type == tiles::SampleType::Float32 && random() % 10007 == 0) {
                                    v = std::nanf("");
                                }
                                buffer.setSample(p, x, y, v);
                            }
                        }
                    }
                }
                content->tiles.set({tx, ty}, std::move(buffer).seal());
            }
        }
        layer->content = std::move(content);
        doc.layers.push_back(std::move(layer));
    }
    return doc;
}

class GpuRandomStack : public ::testing::TestWithParam<tiles::SampleType> {};

TEST_P(GpuRandomStack, MatchesTheCpu) {
    REQUIRE_GPU(context);
    const document::DocumentState doc = randomStack(GetParam(), 20261013u);
    const auto root = render::compileGraph(doc);
    ASSERT_TRUE(GpuEvaluator::supports(*root));
    const auto coords = canvasTiles(doc);
    render::CpuEvaluator cpu(render::CpuEvaluator::Options{.threads = 2});
    GpuEvaluator gpu(context, cpu);
    const auto expected = cpu.renderTiles(*root, coords, render::EvaluationContext{});
    const auto actual = gpu.renderTiles(*root, coords, render::EvaluationContext{});
    EXPECT_EQ(gpu.stats().gpuTiles, coords.size());
    const Comparison result = compare(expected, actual);
    RecordProperty("max_float_difference", std::to_string(result.maxFloatDifference));
    RecordProperty("pixels", static_cast<int>(result.pixels));
    if (GetParam() == tiles::SampleType::UInt8) {
        EXPECT_EQ(result.uint8Mismatches, 0u) << "uint8 must match exactly";
    }
    EXPECT_LE(result.maxFloatDifference, 1e-6);
    EXPECT_EQ(context->validationErrors(), 0u) << (context->validationLog().empty() ? "" : context->validationLog()[0]);
}

INSTANTIATE_TEST_SUITE_P(SampleTypes, GpuRandomStack,
                         ::testing::Values(tiles::SampleType::UInt8, tiles::SampleType::Float32),
                         [](const auto &suite) { return std::string(tiles::sampleTypeName(suite.param)); });

// Spec §7.5: after a device loss nothing more is submitted and the CPU produces the same results.
TEST(GpuEvaluator, FallsBackToTheCpuAfterDeviceLoss) {
    REQUIRE_GPU(context);
    const document::DocumentState doc = randomStack(tiles::SampleType::UInt8, 7u);
    const auto root = render::compileGraph(doc);
    const auto coords = canvasTiles(doc);
    render::CpuEvaluator cpu(render::CpuEvaluator::Options{.threads = 2});
    GpuEvaluator gpu(context, cpu);
    context->markLost();
    const auto actual = gpu.renderTiles(*root, coords, render::EvaluationContext{});
    const auto expected = cpu.renderTiles(*root, coords, render::EvaluationContext{});
    EXPECT_TRUE(gpu.stats().deviceLost);
    EXPECT_EQ(gpu.stats().gpuTiles, 0u);
    EXPECT_EQ(gpu.stats().cpuTiles, coords.size());
    const Comparison result = compare(expected, actual);
    EXPECT_EQ(result.maxFloatDifference, 0.0);
}

TEST(GpuEvaluator, OtherGraphsRunOnTheCpu) {
    REQUIRE_GPU(context);
    const document::DocumentState doc = randomStack(tiles::SampleType::UInt8, 9u);
    // A composite of composites is outside the slice.
    const auto inner = render::compileGraph(doc);
    const render::CompositeNode outer({inner});
    EXPECT_FALSE(GpuEvaluator::supports(outer));
    render::CpuEvaluator cpu(render::CpuEvaluator::Options{.threads = 2});
    GpuEvaluator gpu(context, cpu);
    const std::vector<tiles::TileCoord> coords{{0, 0}, {1, 1}};
    const auto actual = gpu.renderTiles(outer, coords, render::EvaluationContext{});
    EXPECT_EQ(gpu.stats().cpuTiles, 2u);
    EXPECT_EQ(compare(cpu.renderTiles(outer, coords, render::EvaluationContext{}), actual).maxFloatDifference, 0.0);
}

} // namespace
} // namespace archmage::gpu
