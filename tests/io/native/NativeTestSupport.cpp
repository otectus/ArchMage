#include "NativeTestSupport.h"

#include "archmage/base/Random.h"
#include "archmage/base/Sha256.h"
#include "archmage/native/Sqlite.h"
#include "archmage/render/CpuEvaluator.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>

namespace archmage::native::testing {

using commands::CommandEnvelope;
using nlohmann::json;

ScratchDirectory::ScratchDirectory(const std::filesystem::path &parent) {
    std::array<std::byte, 6> random{};
    base::secureRandomBytes(random);
    std::string name = "archmage-test-";
    for (const std::byte b : random) {
        name += "0123456789abcdef"[static_cast<unsigned>(b) >> 4u];
        name += "0123456789abcdef"[static_cast<unsigned>(b) & 15u];
    }
    path_ = parent / name;
    std::filesystem::create_directories(path_);
}

ScratchDirectory::~ScratchDirectory() {
    std::error_code ignored;
    std::filesystem::remove_all(path_, ignored);
}

std::filesystem::path tmpRoot() { return "/tmp"; }

std::filesystem::path homeRoot() {
    const std::filesystem::path root = ARCHMAGE_HOME_SCRATCH;
    std::filesystem::create_directories(root);
    return root;
}

std::vector<brush::StrokeSample> line(base::DPoint from, base::DPoint to, int steps) {
    std::vector<brush::StrokeSample> samples;
    for (int i = 0; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        samples.push_back({{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t},
                           static_cast<float>(0.4 + 0.6 * t),
                           i * 4'166'667LL});
    }
    return samples;
}

CommandEnvelope stroke(const std::string &layer, const brush::BrushSettings &settings,
                       const std::vector<brush::StrokeSample> &samples) {
    json parameters = settings.toJson();
    parameters["samples"] = brush::samplesToJson(samples);
    return CommandEnvelope::make("archmage.raster.paintStroke", parameters, {layer});
}

SampleDocument buildSampleDocument(std::int64_t width, std::int64_t height, std::string_view sampleType) {
    auto created = commands::createDocument(CommandEnvelope::make(
        "archmage.document.create", {{"width", width},
                                     {"height", height},
                                     {"sampleType", sampleType},
                                     {"title", "Sample \xE2\x80\x94 layered"},
                                     {"background", "white"}}));
    EXPECT_TRUE(created) << base::format(created.error());
    SampleDocument doc;
    doc.executor = std::make_unique<commands::MutationExecutor>(created->state, "New Document");
    commands::MutationExecutor &executor = *doc.executor;
    doc.background = executor.current()->state->layers[0]->id.toString();
    const auto add = [&](const char *name) {
        auto added = executor.execute(CommandEnvelope::make("archmage.layer.add", {{"name", name}}));
        EXPECT_TRUE(added) << base::format(added.error());
        return added->result["layerId"].get<std::string>();
    };
    const auto run = [&](CommandEnvelope envelope) {
        auto result = executor.execute(std::move(envelope));
        EXPECT_TRUE(result) << base::format(result.error());
        return result ? result->result : json::object();
    };
    doc.ink = add("Ink");
    brush::BrushSettings ink;
    ink.color = {0.1f, 0.2f, 0.7f};
    ink.size = 24.0;
    ink.hardness = 0.6;
    const double w = static_cast<double>(width);
    const double h = static_cast<double>(height);
    run(stroke(doc.ink, ink, line({w * 0.05, h * 0.1}, {w * 0.95, h * 0.8}, 90)));
    doc.glaze = add("Glaze \xC3\xA9t\xC3\xA9");
    brush::BrushSettings glaze;
    glaze.color = {0.9f, 0.6f, 0.1f};
    glaze.size = 40.0;
    glaze.hardness = 1.0;
    run(stroke(doc.glaze, glaze, line({w * 0.9, h * 0.1}, {w * 0.1, h * 0.9}, 60)));
    run(CommandEnvelope::make("archmage.layer.setOpacity", {{"opacity", 0.5}}, {doc.glaze}));
    run(CommandEnvelope::make("archmage.layer.setOffset", {{"x", 17}, {"y", -9}}, {doc.glaze}));
    doc.instance = run(CommandEnvelope::make("archmage.layer.duplicateInstance", {{"name", "Ink instance"}},
                                             {doc.ink}))["layerId"]
                       .get<std::string>();
    run(CommandEnvelope::make("archmage.layer.setVisible", {{"visible", false}}, {doc.instance}));
    run(CommandEnvelope::make("archmage.layer.setLocked", {{"locked", true}}, {doc.background}));
    return doc;
}

std::vector<std::byte> composite(const document::DocumentState &state) {
    render::CpuEvaluator evaluator(render::CpuEvaluator::Options{.threads = 2});
    render::RenderRequest request;
    request.document = state.id;
    request.region = state.canvasRect();
    auto result = evaluator.render(state, request);
    EXPECT_TRUE(result) << base::format(result.error());
    return result ? result->pixels : std::vector<std::byte>{};
}

bool sameTiles(const tiles::TileMap &a, const tiles::TileMap &b) {
    bool same = a.tileCount() == b.tileCount() && a.layout() == b.layout() &&
                a.defaultTile()->sameContent(*b.defaultTile());
    a.forEach([&](tiles::TileCoord coord, const std::shared_ptr<const tiles::Tile> &tile) {
        same = same && tile->sameContent(*b.at(coord));
    });
    return same;
}

std::string fileHash(const std::filesystem::path &file) {
    std::ifstream in(file, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return base::Sha256::of(std::as_bytes(std::span(bytes))).hex();
}

std::vector<std::string> directoryEntries(const std::filesystem::path &directory) {
    std::vector<std::string> names;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

base::Result<SaveOutcome> save(const std::shared_ptr<const document::Revision> &revision,
                               const std::filesystem::path &target, FileIdentity identity,
                               std::function<base::Status(SaveStep)> hook) {
    SaveRequest request;
    request.target = target;
    request.revision = revision;
    request.identity = identity;
    request.hook = std::move(hook);
    return saveDocument(request);
}

void execSql(const std::filesystem::path &file, const std::string &sql) {
    auto db = Database::open(file, Database::Mode::ReadWriteCreate);
    ASSERT_TRUE(db) << base::format(db.error());
    // Tests write foreign content on purpose; defensive mode would refuse schema edits only.
    const auto status = db->exec(sql);
    ASSERT_TRUE(status) << base::format(status.error());
}

} // namespace archmage::native::testing
