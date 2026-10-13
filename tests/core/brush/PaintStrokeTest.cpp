// S1-3.3, S1-3.4 and S1-3.6 (AT-002): the paintStroke command, the preview session and stroke undo/redo at
// tile boundaries.

#include "archmage/brush/Stroke.h"
#include "archmage/commands/MutationExecutor.h"
#include "archmage/render/CpuEvaluator.h"

#include <gtest/gtest.h>

#include <cmath>

namespace archmage::brush {
namespace {

using commands::CommandEnvelope;
using commands::MutationExecutor;
using nlohmann::json;

document::DocumentState newDocument(std::int64_t size, const char *background) {
    return commands::createDocument(
               CommandEnvelope::make("archmage.document.create",
                                     {{"width", size}, {"height", size}, {"background", background}}))
        ->state;
}

std::vector<StrokeSample> diagonal(base::DPoint from, base::DPoint to, int steps) {
    std::vector<StrokeSample> samples;
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        samples.push_back({{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t},
                           static_cast<float>(0.3 + 0.7 * t),
                           i * 4'166'667LL});
    }
    return samples;
}

CommandEnvelope strokeCommand(const std::string &layer, const BrushSettings &settings,
                              const std::vector<StrokeSample> &samples) {
    json parameters = settings.toJson();
    parameters["samples"] = samplesToJson(samples);
    return CommandEnvelope::make("archmage.raster.paintStroke", parameters, {layer});
}

bool sameTiles(const tiles::TileMap &a, const tiles::TileMap &b) {
    bool same = a.tileCount() == b.tileCount();
    a.forEach([&](tiles::TileCoord coord, const std::shared_ptr<const tiles::Tile> &tile) {
        same = same && tile->sameContent(*b.at(coord));
    });
    return same;
}

std::vector<std::byte> renderComposite(render::CpuEvaluator &evaluator,
                                       const document::DocumentState &state) {
    render::RenderRequest request;
    request.document = state.id;
    request.region = state.canvasRect();
    auto result = evaluator.render(state, request);
    EXPECT_TRUE(result);
    return result ? result->pixels : std::vector<std::byte>{};
}

TEST(PaintStroke, PaintUndoRedoAreByteExact) {
    MutationExecutor executor(newDocument(400, "white"), "New Document");
    const std::string layer = executor.current()->state->layers[0]->id.toString();
    const auto before = executor.current()->state;
    BrushSettings settings;
    settings.color = {0.8f, 0.1f, 0.2f};
    settings.size = 30.0;
    const auto painted =
        executor.execute(strokeCommand(layer, settings, diagonal({20, 20}, {380, 300}, 120)));
    ASSERT_TRUE(painted) << base::format(painted.error());
    EXPECT_EQ(painted->label, "Brush Stroke");
    EXPECT_GT(painted->result["dabs"].get<int>(), 100);
    EXPECT_EQ(executor.history().size(), 2u) << "one stroke, one history item";
    const auto after = executor.current()->state;
    EXPECT_FALSE(sameTiles(before->layers[0]->content->tiles, after->layers[0]->content->tiles));
    ASSERT_TRUE(executor.undo());
    EXPECT_TRUE(
        sameTiles(executor.current()->state->layers[0]->content->tiles, before->layers[0]->content->tiles));
    ASSERT_TRUE(executor.redo());
    EXPECT_TRUE(
        sameTiles(executor.current()->state->layers[0]->content->tiles, after->layers[0]->content->tiles));
}

TEST(PaintStroke, LockedLayersRejectAndHiddenLayersWarn) {
    MutationExecutor executor(newDocument(200, "transparent"), "New Document");
    const std::string layer = executor.current()->state->layers[0]->id.toString();
    const auto samples = diagonal({10, 10}, {100, 100}, 20);
    ASSERT_TRUE(
        executor.execute(CommandEnvelope::make("archmage.layer.setVisible", {{"visible", false}}, {layer})));
    const auto hidden = executor.execute(strokeCommand(layer, BrushSettings{}, samples));
    ASSERT_TRUE(hidden);
    EXPECT_EQ(hidden->result["warning"], "layer.hidden");
    ASSERT_TRUE(
        executor.execute(CommandEnvelope::make("archmage.layer.setLocked", {{"locked", true}}, {layer})));
    const auto locked = executor.execute(strokeCommand(layer, BrushSettings{}, samples));
    ASSERT_FALSE(locked);
    EXPECT_EQ(locked.error().code, "layer.locked");
    json bad = BrushSettings{}.toJson();
    bad["samples"] = json::array();
    EXPECT_FALSE(executor.execute(CommandEnvelope::make("archmage.raster.paintStroke", bad, {layer})));
}

TEST(PaintStroke, FlatOpacityDoesNotBuildUpWhereDabsOverlap) {
    const document::DocumentState doc = newDocument(128, "white");
    BrushSettings settings;
    settings.color = {0.0f, 0.0f, 0.0f};
    settings.size = 40.0;
    settings.hardness = 1.0;
    settings.spacing = 0.05; // heavily overlapping dabs
    settings.opacity = 0.5;
    const auto layer = paintLayer(*doc.layers[0], settings, diagonal({20, 64}, {108, 64}, 40));
    ASSERT_TRUE(layer);
    const auto tile = (*layer)->content->tiles.at({0, 0});
    // Fully covered pixels: white mixed with black at exactly 50 %, wherever dabs overlap.
    EXPECT_NEAR(tile->sample(0, 64, 64), 0.5f, 1.0f / 255.0f);
    EXPECT_EQ(tile->sample(0, 64, 64), tile->sample(0, 30, 64));
    EXPECT_EQ(tile->sample(3, 64, 64), 1.0f);
}

TEST(PaintStroke, EraseLowersAlphaAndKeepsTheStoredColor) {
    const document::DocumentState doc = newDocument(128, "white");
    BrushSettings settings;
    settings.mode = PaintMode::Erase;
    settings.size = 20.0;
    settings.hardness = 1.0;
    const auto layer = paintLayer(*doc.layers[0], settings, {{{{64.0, 64.0}, 1.0f}}});
    ASSERT_TRUE(layer);
    const auto tile = (*layer)->content->tiles.at({0, 0});
    EXPECT_EQ(tile->sample(3, 64, 64), 0.0f);
    EXPECT_EQ(tile->sample(0, 64, 64), 1.0f) << "hidden color under zero alpha is preserved (D-09)";
    EXPECT_EQ(tile->sample(3, 100, 100), 1.0f);
}

TEST(StrokeSession, PreviewEqualsTheCommittedStroke) {
    MutationExecutor executor(newDocument(600, "white"), "New Document");
    const auto base = executor.current()->state;
    const base::LayerId layerId = base->layers[0]->id;
    BrushSettings settings;
    settings.color = {0.1f, 0.4f, 0.9f};
    settings.size = 25.0;
    StrokeSession session(base, layerId, settings);
    const auto samples = diagonal({30, 500}, {570, 40}, 200);
    tiles::DirtyRegion dirty;
    for (std::size_t i = 0; i < samples.size(); i += 13) { // arrives in uneven batches
        const std::size_t n = std::min<std::size_t>(13, samples.size() - i);
        dirty.merge(session.add(std::span(samples).subspan(i, n)));
    }
    EXPECT_FALSE(dirty.empty());
    EXPECT_EQ(executor.current()->state, base) << "the preview never reaches the executor";
    auto envelope = CommandEnvelope::make("archmage.raster.paintStroke", session.commandParameters(),
                                          {layerId.toString()});
    ASSERT_TRUE(executor.execute(envelope));
    EXPECT_TRUE(sameTiles(session.preview()->layers[0]->content->tiles,
                          executor.current()->state->layers[0]->content->tiles));
}

TEST(StrokeSession, CancellingLeavesTheRevisionAndPixelsUntouched) {
    MutationExecutor executor(newDocument(300, "transparent"), "New Document");
    const auto base = executor.current();
    {
        StrokeSession session(base->state, base->state->layers[0]->id, BrushSettings{});
        session.add(diagonal({10, 10}, {290, 290}, 60));
        EXPECT_GT(session.preview()->layers[0]->content->tiles.tileCount(), 0u);
    } // Escape: the session is dropped without a command
    EXPECT_EQ(executor.current(), base);
    EXPECT_EQ(executor.current()->state->layers[0]->content->tiles.tileCount(), 0u)
        << "no committed tile allocated";
    EXPECT_EQ(executor.history().size(), 1u);
}

// AT-002: stroke undo/redo at tile boundaries. Exact raster restoration, no seams, no random reseeding.
TEST(AT002, StrokeUndoRedoAtTileBoundaries) {
    document::DocumentState doc = newDocument(1000, "white");
    auto paint = std::make_shared<document::Layer>(*document::makeRasterLayer(doc, "Paint"));
    paint->offset = {-37, 59};
    doc.layers.push_back(paint);
    MutationExecutor executor(doc, "New Document");
    render::CpuEvaluator evaluator;
    const auto before = executor.current()->state;
    const std::vector<std::byte> renderedBefore = renderComposite(evaluator, *before);

    // Layer-local tile corners at (128, 128), (256, 256), (384, 384) and (512, 512) lie on this diagonal.
    BrushSettings settings;
    settings.color = {0.15f, 0.3f, 0.85f};
    settings.size = 36.0;
    settings.hardness = 0.6;
    const auto samples = diagonal({50.0, 146.0}, {600.0, 696.0}, 300);
    const CommandEnvelope stroke = strokeCommand(paint->id.toString(), settings, samples);
    ASSERT_TRUE(executor.execute(stroke));
    const auto after = executor.current()->state;
    const std::vector<std::byte> renderedAfter = renderComposite(evaluator, *after);
    ASSERT_NE(renderedBefore, renderedAfter);

    const auto &strokeTiles = after->layers[1]->content->tiles;
    for (const tiles::TileCoord corner :
         {tiles::TileCoord{1, 1}, tiles::TileCoord{2, 2}, tiles::TileCoord{3, 3}, tiles::TileCoord{4, 4}}) {
        for (const tiles::TileCoord around :
             {corner, tiles::TileCoord{corner.tx - 1, corner.ty}, tiles::TileCoord{corner.tx, corner.ty - 1},
              tiles::TileCoord{corner.tx - 1, corner.ty - 1}}) {
            ASSERT_NE(strokeTiles.find(around), nullptr)
                << "the stroke crosses the corner of tile " << corner.tx;
        }
    }

    for (int cycle = 0; cycle < 10; ++cycle) {
        ASSERT_TRUE(executor.undo());
        ASSERT_EQ(renderComposite(evaluator, *executor.current()->state), renderedBefore)
            << "undo, cycle " << cycle;
        ASSERT_TRUE(sameTiles(executor.current()->state->layers[1]->content->tiles,
                              before->layers[1]->content->tiles));
        ASSERT_TRUE(executor.redo());
        ASSERT_EQ(renderComposite(evaluator, *executor.current()->state), renderedAfter)
            << "redo, cycle " << cycle;
    }

    // No reseeding: replaying the recorded envelope reproduces the stroke bit for bit.
    const auto recorded =
        CommandEnvelope::fromJson(after->layers.empty() ? json() : executor.current()->provenance[0]);
    ASSERT_TRUE(recorded);
    EXPECT_FALSE(recorded->parameters.contains("seed")) << "the round brush has no stochastic input";
    const auto replayed =
        commands::CommandRegistry::builtIn().find("archmage.raster.paintStroke")->apply(*recorded, *before);
    ASSERT_TRUE(replayed);
    EXPECT_TRUE(sameTiles(replayed->state.layers[1]->content->tiles, strokeTiles));

    // No seams: pixels within two pixels of every tile border equal an untiled rasterization of the dabs.
    StrokeResampler resampler(settings);
    std::vector<Dab> dabs;
    for (StrokeSample s : samples) {
        s.position = {s.position.x - static_cast<double>(paint->offset.x),
                      s.position.y - static_cast<double>(paint->offset.y)};
        resampler.add(s, dabs);
    }
    std::int64_t checked = 0;
    const base::IRect bounds = strokeTiles.pixelBounds();
    for (std::int64_t y = bounds.y0; y < bounds.y1; ++y) {
        for (std::int64_t x = bounds.x0; x < bounds.x1; ++x) {
            const std::int64_t mx = ((x % 128) + 128) % 128;
            const std::int64_t my = ((y % 128) + 128) % 128;
            if (!(mx < 2 || mx >= 126 || my < 2 || my >= 126)) {
                continue;
            }
            float coverage = 0.0f;
            for (const Dab &dab : dabs) {
                if (dabBounds(dab).contains(base::IPoint{x, y})) {
                    coverage =
                        std::max(coverage, dabCoverage(dab, settings.hardness, base::pixelCenter({x, y})));
                }
            }
            const float m = std::clamp(coverage * static_cast<float>(settings.opacity), 0.0f, 1.0f);
            std::byte expected[1];
            tiles::storeSample(tiles::SampleType::UInt8, m, expected);
            const auto tile = strokeTiles.at(tiles::tileContaining({x, y}));
            const float actual = tile->sample(3, static_cast<int>(mx), static_cast<int>(my));
            ASSERT_EQ(actual, tiles::loadSample(tiles::SampleType::UInt8, expected))
                << "seam at " << x << "," << y;
            ++checked;
        }
    }
    EXPECT_GT(checked, 10000);
}

} // namespace
} // namespace archmage::brush
