// Benchmark harness (plan S1-6.8; spec §25.1, §25.2): measures, on a synthetic 24 MP, 20-layer document
// (6000 × 4000, uint8), the operations whose budgets spec §25.2 states, at the CPU boundary:
//
//   warm_pan_zoom          frame time of the canvas frame source panning and zooming a cached composite
//                          at 2560 × 1600 device pixels (budget p95 <= 16.7 ms)
//   pixel_update_after_stroke  committed brush stroke to the updated frame (budget p95 <= 50 ms)
//   small_undo             undo of one stroke to the updated frame (budget p95 <= 100 ms)
//   composite_cpu, composite_gpu  one uncached full composite (no budget; GPU viability evidence, ADR-0012)
//
// Brush preview latency and launch time run in the desktop application and are collected by
// scripts/bench_record.py. Prints one line "EVIDENCE bench <json>".
//
//   bench_main [--profile A|C] [--frames N] [--strokes N]
//
// Profile C (16 GiB RAM, CPU-only) is simulated: ARCHMAGE_FORCE_CPU=1 and an 8 GiB RAM budget.

#include "archmage/brush/Stroke.h"
#include "archmage/commands/MutationExecutor.h"
#include "archmage/gpu/GpuEvaluator.h"
#include "archmage/render/CpuEvaluator.h"
#include "archmage/render/ViewTransform.h"
#include "archmage/tiles/ResourceCoordinator.h"
#include "archmage/ui/canvas/DocumentFrameSource.h"

#include <QGuiApplication>
#include <QImage>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <string>
#include <vector>

namespace {

using archmage::commands::CommandEnvelope;
using nlohmann::json;
using Clock = std::chrono::steady_clock;

constexpr std::int64_t kWidth = 6000;
constexpr std::int64_t kHeight = 4000;
constexpr int kLayers = 20;
constexpr int kDeviceWidth = 2560;
constexpr int kDeviceHeight = 1600;

double msSince(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

json percentiles(std::vector<double> samples, double budgetMs) {
    std::sort(samples.begin(), samples.end());
    const auto at = [&](double q) {
        if (samples.empty()) {
            return 0.0;
        }
        const auto index = static_cast<std::size_t>(std::ceil(q * static_cast<double>(samples.size()))) - 1;
        return samples[std::min(index, samples.size() - 1)];
    };
    json out{{"samples", samples.size()}, {"p50_ms", at(0.50)}, {"p95_ms", at(0.95)},
             {"max_ms", samples.empty() ? 0.0 : samples.back()}};
    if (budgetMs > 0.0) {
        out["budget_p95_ms"] = budgetMs;
        out["pass"] = at(0.95) <= budgetMs;
    }
    return out;
}

// A dense tile whose pattern depends on its layer and position: smooth gradients plus fine texture, so the
// tiles are neither uniform nor trivially compressible.
std::shared_ptr<const archmage::tiles::Tile> patternTile(int layer, archmage::tiles::TileCoord coord, bool opaque) {
    archmage::tiles::TileBuffer buffer(archmage::tiles::SampleType::UInt8, 4);
    for (std::size_t p = 0; p < 4; ++p) {
        auto samples = buffer.planeAs<std::uint8_t>(p);
        for (int y = 0; y < archmage::tiles::kTileSize; ++y) {
            for (int x = 0; x < archmage::tiles::kTileSize; ++x) {
                const std::int64_t gx = coord.tx * archmage::tiles::kTileSize + x;
                const std::int64_t gy = coord.ty * archmage::tiles::kTileSize + y;
                std::uint32_t v = 0;
                if (p == 3) {
                    v = opaque ? 255u : static_cast<std::uint32_t>(96 + ((gx / 7 + gy / 5 + layer * 13) % 128));
                } else {
                    v = static_cast<std::uint32_t>((gx * (p + 1) + gy * (3 - p) + layer * 37) / 23 + ((gx ^ gy) & 7));
                }
                samples[static_cast<std::size_t>(y * archmage::tiles::kTileSize + x)] = static_cast<std::uint8_t>(v & 0xffu);
            }
        }
    }
    return std::move(buffer).seal();
}

archmage::document::DocumentState syntheticDocument() {
    auto created = archmage::commands::createDocument(CommandEnvelope::make(
        "archmage.document.create",
        {{"width", kWidth}, {"height", kHeight}, {"title", "Synthetic 24 MP, 20 layers"}, {"background", "transparent"}}));
    archmage::document::DocumentState doc = created->state;
    doc.layers.clear();
    std::mt19937 random(25012026u);
    std::uniform_real_distribution<double> unit(0.0, 1.0);
    const auto tileColumns = (kWidth + archmage::tiles::kTileSize - 1) / archmage::tiles::kTileSize;
    const auto tileRows = (kHeight + archmage::tiles::kTileSize - 1) / archmage::tiles::kTileSize;
    for (int l = 0; l < kLayers; ++l) {
        auto layer = std::make_shared<archmage::document::Layer>(
            *archmage::document::makeRasterLayer(doc, "Layer " + std::to_string(l + 1)));
        auto content = std::make_shared<archmage::document::RasterContent>(*layer->content);
        // The bottom layer covers the canvas; the others cover random rectangles of 20 to 60 % of each axis.
        std::int64_t x0 = 0, y0 = 0, x1 = tileColumns, y1 = tileRows;
        if (l > 0) {
            const auto w = static_cast<std::int64_t>(static_cast<double>(tileColumns) * (0.2 + 0.4 * unit(random)));
            const auto h = static_cast<std::int64_t>(static_cast<double>(tileRows) * (0.2 + 0.4 * unit(random)));
            x0 = static_cast<std::int64_t>(random() % static_cast<std::uint32_t>(tileColumns - w));
            y0 = static_cast<std::int64_t>(random() % static_cast<std::uint32_t>(tileRows - h));
            x1 = x0 + w;
            y1 = y0 + h;
            layer->opacity = 0.35f + 0.6f * static_cast<float>(unit(random));
        }
        for (std::int64_t ty = y0; ty < y1; ++ty) {
            for (std::int64_t tx = x0; tx < x1; ++tx) {
                content->tiles.set({tx, ty}, patternTile(l, {tx, ty}, l == 0));
            }
        }
        layer->content = std::move(content);
        doc.layers.push_back(std::move(layer));
    }
    return doc;
}

std::vector<archmage::tiles::TileCoord> canvasTiles() {
    std::vector<archmage::tiles::TileCoord> coords;
    const auto range = archmage::tiles::tileRangeCovering({0, 0, kWidth, kHeight});
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            coords.push_back({tx, ty});
        }
    }
    return coords;
}

CommandEnvelope stroke(const std::string &layer, int index) {
    archmage::brush::BrushSettings settings;
    settings.color = {0.9f, 0.2f + 0.03f * static_cast<float>(index % 20), 0.1f};
    settings.size = 40.0;
    std::vector<archmage::brush::StrokeSample> samples;
    const double cx = 600.0 + (index % 8) * 600.0;
    const double cy = 600.0 + ((index / 8) % 5) * 600.0;
    for (int i = 0; i <= 60; ++i) {
        const double t = i / 60.0;
        samples.push_back({{cx + 400.0 * t, cy + 120.0 * std::sin(t * 6.0)}, 0.6f + 0.4f * static_cast<float>(t),
                           i * 4'166'667LL});
    }
    json parameters = settings.toJson();
    parameters["samples"] = archmage::brush::samplesToJson(samples);
    return CommandEnvelope::make("archmage.raster.paintStroke", parameters, {layer});
}

} // namespace

int main(int argc, char **argv) {
    std::string profile = "A";
    int frames = 300;
    int strokes = 40;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--profile" && i + 1 < argc) {
            profile = argv[++i];
        } else if (arg == "--frames" && i + 1 < argc) {
            frames = std::max(10, std::atoi(argv[++i]));
        } else if (arg == "--strokes" && i + 1 < argc) {
            strokes = std::max(5, std::atoi(argv[++i]));
        }
    }
    if (profile == "C") {
        ::setenv("ARCHMAGE_FORCE_CPU", "1", 1);
        archmage::tiles::ResourceCoordinator::global().setLimit(archmage::tiles::Budget::Ram, 8ull << 30);
    }
    if (std::getenv("QT_QPA_PLATFORM") == nullptr) {
        ::setenv("QT_QPA_PLATFORM", "offscreen", 1);
    }
    QGuiApplication app(argc, argv);

    json records = json::object();
    const auto generationStarted = Clock::now();
    const archmage::document::DocumentState document = syntheticDocument();
    std::size_t denseTiles = 0;
    for (const auto &layer : document.layers) {
        denseTiles += layer->content->tiles.denseTileCount();
    }
    const double generationMs = msSince(generationStarted);
    auto evaluator = std::make_shared<archmage::render::CpuEvaluator>();

    // One uncached full composite on the CPU, and on the GPU slice when a device exists.
    {
        const auto root = archmage::render::compileGraph(document);
        const auto coords = canvasTiles();
        archmage::render::CpuEvaluator cold;
        auto started = Clock::now();
        const auto cpuTiles = cold.renderTiles(*root, coords, archmage::render::EvaluationContext{});
        const double cpuMs = msSince(started);
        records["composite_cpu"] = {{"tiles", coords.size()}, {"ms", cpuMs}, {"threads", cold.threadCount()}};
        auto context = archmage::gpu::GpuContext::create(archmage::gpu::GpuContext::Options{.validation = false});
        if (context) {
            archmage::gpu::GpuEvaluator gpu(*context, cold);
            (void)gpu.renderTiles(*root, std::span(coords).first(1), archmage::render::EvaluationContext{}); // pipeline warm-up
            const auto before = gpu.stats();
            started = Clock::now();
            const auto gpuTiles = gpu.renderTiles(*root, coords, archmage::render::EvaluationContext{});
            const double gpuMs = msSince(started);
            const auto after = gpu.stats();
            const std::uint64_t onGpu = after.gpuTiles - before.gpuTiles;
            double maxDifference = 0.0;
            for (std::size_t t = 0; t < coords.size(); ++t) {
                if (cpuTiles[t] && gpuTiles[t]) {
                    const auto &a = (*cpuTiles[t])->data();
                    const auto &b = (*gpuTiles[t])->data();
                    for (std::size_t i = 0; i < a.size(); ++i) {
                        maxDifference = std::max(maxDifference, static_cast<double>(std::fabs(a[i] - b[i])));
                    }
                }
            }
            records["composite_gpu"] = {{"tiles", coords.size()},
                                        {"ms", gpuMs},
                                        {"device", (*context)->info().name},
                                        {"gpu_tiles", onGpu},
                                        {"gather_ms", after.gatherMs - before.gatherMs},
                                        {"dispatch_ms", after.dispatchMs - before.dispatchMs},
                                        {"readback_ms", after.readbackMs - before.readbackMs},
                                        {"batches", after.batches - before.batches},
                                        {"max_difference_vs_cpu", maxDifference},
                                        {"note", "host-visible buffers; includes CPU gathering of layer samples"}};
        } else {
            records["composite_gpu"] = {{"unavailable", archmage::base::format(context.error())}};
        }
    }

    // Warm pan and zoom: the frame source with every display tile cached, at the profile A display size.
    archmage::ui::canvas::DocumentFrameSource frameSource(*evaluator, 512ull << 20);
    auto state = std::make_shared<const archmage::document::DocumentState>(document);
    frameSource.setState(state);
    QImage frame(kDeviceWidth, kDeviceHeight, QImage::Format_RGBA8888_Premultiplied);
    const archmage::base::DSize deviceSize{static_cast<double>(kDeviceWidth), static_cast<double>(kDeviceHeight)};
    const auto fit = archmage::render::ViewTransform::fit(
        archmage::base::DRect{0.0, 0.0, static_cast<double>(kWidth), static_cast<double>(kHeight)}, deviceSize, 1.0);
    const auto warmStarted = Clock::now();
    frameSource.renderFrame(fit, frame); // evaluates and caches every tile
    const double warmupMs = msSince(warmStarted);
    std::vector<double> panZoom;
    for (int f = 0; f < frames; ++f) {
        archmage::render::ViewTransform view = fit;
        if (f < frames / 2) {
            view = fit.panned(archmage::base::DPoint{static_cast<double>((f * 9) % 400) - 200.0,
                                                     static_cast<double>((f * 5) % 240) - 120.0});
        } else {
            const double steps = static_cast<double>(f - frames / 2) / (frames / 2) * 4.0; // up to 2× fit
            view = fit.zoomedAbout(fit.scale() * std::pow(2.0, steps / 4.0),
                                   archmage::base::DPoint{kDeviceWidth / 2.0, kDeviceHeight / 2.0});
        }
        const auto started = Clock::now();
        frameSource.renderFrame(view, frame);
        panZoom.push_back(msSince(started));
    }
    records["warm_pan_zoom"] = percentiles(panZoom, 16.7);
    records["warm_pan_zoom"]["view"] = "2560x1600 device pixels, fit to 2x fit, pans of up to 200 px";
    records["warm_pan_zoom"]["warmup_ms"] = warmupMs;
    records["warm_pan_zoom"]["display_hits"] = frameSource.stats().displayHits;
    records["warm_pan_zoom"]["display_misses"] = frameSource.stats().displayMisses;
    records["warm_pan_zoom"]["boundary"] = "frame source CPU time per frame; presentation excluded (offscreen)";

    // Pixel update after a stroke, and small undo: command to updated frame, at the fit view.
    archmage::commands::MutationExecutor executor(document, "New Document");
    const std::string top = document.layers.back()->id.toString();
    frameSource.setState(executor.current()->state);
    frameSource.renderFrame(fit, frame);
    std::vector<double> update;
    for (int s = 0; s < strokes; ++s) {
        const auto started = Clock::now();
        auto result = executor.execute(stroke(top, s));
        if (!result) {
            std::printf("EVIDENCE bench {\"error\":\"%s\"}\n", archmage::base::format(result.error()).c_str());
            return 1;
        }
        frameSource.setState(executor.current()->state);
        frameSource.renderFrame(fit, frame);
        update.push_back(msSince(started));
    }
    records["pixel_update_after_stroke"] = percentiles(update, 50.0);
    records["pixel_update_after_stroke"]["stroke"] = "round brush, size 40, 61 samples over about 400 px";
    records["pixel_update_after_stroke"]["boundary"] = "paintStroke commit to the next frame of the fit view";
    std::vector<double> undo;
    for (int s = 0; s < strokes; ++s) {
        const auto started = Clock::now();
        if (!executor.undo()) {
            break;
        }
        frameSource.setState(executor.current()->state);
        frameSource.renderFrame(fit, frame);
        undo.push_back(msSince(started));
    }
    records["small_undo"] = percentiles(undo, 100.0);
    records["small_undo"]["boundary"] = "undo of one stroke to the next frame of the fit view";

    const json out{{"profile", profile},
                   {"simulated", profile == "C" ? json("ARCHMAGE_FORCE_CPU=1, 8 GiB RAM budget") : json(nullptr)},
                   {"fixture",
                    {{"generator", "benchmarks/bench_main.cpp syntheticDocument (seed 25012026)"},
                     {"width", kWidth},
                     {"height", kHeight},
                     {"layers", kLayers},
                     {"dense_tiles", denseTiles},
                     {"generation_ms", generationMs}}},
                   {"evaluator_threads", evaluator->threadCount()},
                   {"ram_peak_mib", archmage::tiles::ResourceCoordinator::global().peak(archmage::tiles::Budget::Ram) >> 20},
                   {"records", records}};
    std::printf("EVIDENCE bench %s\n", out.dump().c_str());
    std::fflush(stdout);
    return 0;
}
