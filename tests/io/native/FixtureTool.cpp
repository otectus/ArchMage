// Writes native format fixtures (plan S1-4.7) and damaged copies for the CLI tests.
//
//   archmage-native-fixture write <file>               the canonical document in the current schema, and
//                                                      <file>.expected.json with what a reader must find
//   archmage-native-fixture corrupt-tile <in> <out>    a copy of <in> with one bit flipped in one tile blob
//
// A fixture is written once, when its schema minor is introduced, and committed; never regenerate an
// existing one: the point is that files written by earlier builds keep opening.

#include "archmage/brush/Stroke.h"
#include "archmage/commands/MutationExecutor.h"
#include "archmage/native/Format.h"
#include "archmage/native/SaveTransaction.h"
#include "archmage/native/Sqlite.h"
#include "archmage/render/CpuEvaluator.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>

namespace {

using archmage::commands::CommandEnvelope;
using nlohmann::json;

int fail(const std::string &what) {
    std::cerr << "archmage-native-fixture: " << what << '\n';
    return 2;
}

json run(archmage::commands::MutationExecutor &executor, CommandEnvelope envelope) {
    auto result = executor.execute(std::move(envelope));
    if (!result) {
        throw std::runtime_error(archmage::base::format(result.error()));
    }
    return result->result;
}

CommandEnvelope stroke(const std::string &layer, const archmage::brush::BrushSettings &settings,
                       archmage::base::DPoint from, archmage::base::DPoint to) {
    std::vector<archmage::brush::StrokeSample> samples;
    for (int i = 0; i <= 50; ++i) {
        const double t = i / 50.0;
        samples.push_back({{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t},
                           static_cast<float>(0.5 + 0.5 * t),
                           i * 4'166'667LL});
    }
    json parameters = settings.toJson();
    parameters["samples"] = archmage::brush::samplesToJson(samples);
    return CommandEnvelope::make("archmage.raster.paintStroke", parameters, {layer});
}

int write(const std::filesystem::path &file) {
    namespace native = archmage::native;
    auto created = archmage::commands::createDocument(CommandEnvelope::make(
        "archmage.document.create",
        {{"width", 320}, {"height", 200}, {"title", "Fixture " + std::to_string(native::kSchemaMajor) + "." +
                                                         std::to_string(native::kSchemaMinor)},
         {"background", "white"}}));
    if (!created) {
        return fail(archmage::base::format(created.error()));
    }
    archmage::commands::MutationExecutor executor(created->state, "New Document");
    const std::string background = executor.current()->state->layers[0]->id.toString();
    const std::string ink = run(executor, CommandEnvelope::make("archmage.layer.add", {{"name", "Ink"}}))["layerId"];
    archmage::brush::BrushSettings inkBrush;
    inkBrush.color = {0.1f, 0.2f, 0.7f};
    inkBrush.size = 20.0;
    run(executor, stroke(ink, inkBrush, {20, 30}, {300, 170}));
    const std::string glaze = run(executor, CommandEnvelope::make("archmage.layer.add", {{"name", "Glaze"}}))["layerId"];
    archmage::brush::BrushSettings glazeBrush;
    glazeBrush.color = {0.9f, 0.6f, 0.1f};
    glazeBrush.size = 36.0;
    glazeBrush.hardness = 1.0;
    run(executor, stroke(glaze, glazeBrush, {300, 20}, {30, 180}));
    run(executor, CommandEnvelope::make("archmage.layer.setOpacity", {{"opacity", 0.5}}, {glaze}));
    run(executor, CommandEnvelope::make("archmage.layer.setOffset", {{"x", 17}, {"y", -9}}, {glaze}));
    const std::string instance =
        run(executor, CommandEnvelope::make("archmage.layer.duplicateInstance", {{"name", "Ink instance"}}, {ink}))["layerId"];
    run(executor, CommandEnvelope::make("archmage.layer.setVisible", {{"visible", false}}, {instance}));
    run(executor, CommandEnvelope::make("archmage.layer.setLocked", {{"locked", true}}, {background}));

    const auto revision = executor.current();
    native::SaveRequest request;
    request.target = file;
    request.revision = revision;
    request.identity = native::identityForSave(*revision->state, std::nullopt);
    auto saved = native::saveDocument(request);
    if (!saved) {
        return fail(archmage::base::format(saved.error()));
    }

    const archmage::document::DocumentState &state = *revision->state;
    archmage::render::CpuEvaluator evaluator;
    archmage::render::RenderRequest render;
    render.document = state.id;
    render.region = state.canvasRect();
    auto pixels = evaluator.render(state, render);
    if (!pixels) {
        return fail(archmage::base::format(pixels.error()));
    }
    json layers = json::array();
    for (const auto &layer : state.layers) {
        layers.push_back({{"id", layer->id.toString()},
                          {"name", layer->name},
                          {"visible", layer->visible},
                          {"locked", layer->locked},
                          {"opacity", layer->opacity},
                          {"offset", {layer->offset.x, layer->offset.y}},
                          {"raster", layer->content->id.toString()}});
    }
    json samples = json::array();
    for (const auto &[x, y] : std::vector<std::pair<int, int>>{{0, 0}, {20, 30}, {160, 100}, {300, 170}, {150, 95},
                                                               {299, 20}, {48, 171}, {319, 199}}) {
        json rgba = json::array();
        for (int c = 0; c < 4; ++c) {
            rgba.push_back(static_cast<int>(pixels->pixels[static_cast<std::size_t>((y * 320 + x) * 4 + c)]));
        }
        samples.push_back({{"x", x}, {"y", y}, {"rgba", rgba}});
    }
    const json expected{{"schema", {{"major", native::kSchemaMajor}, {"minor", native::kSchemaMinor}}},
                        {"documentId", state.id.toString()},
                        {"revisionId", revision->id.toString()},
                        {"title", state.title},
                        {"width", state.width},
                        {"height", state.height},
                        {"sampleType", "uint8"},
                        {"profileHash", state.colorPolicy.profile->hash().hex()},
                        {"layers", layers},
                        {"samples", samples}};
    std::ofstream out(file.string() + ".expected.json");
    out << expected.dump(2) << '\n';
    std::cout << "wrote " << file.string() << " (" << saved->bytes << " bytes) and its .expected.json\n";
    return 0;
}

int corruptTile(const std::filesystem::path &in, const std::filesystem::path &out) {
    std::error_code ec;
    std::filesystem::copy_file(in, out, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return fail("copy: " + ec.message());
    }
    auto db = archmage::native::Database::open(out, archmage::native::Database::Mode::ReadWriteCreate);
    if (!db) {
        return fail(archmage::base::format(db.error()));
    }
    auto select = db->prepare("SELECT rowid, data FROM resources WHERE media_type = "
                              "'application/vnd.archmage.tile-plane' ORDER BY hash LIMIT 1");
    if (!select || !select->step()) {
        return fail("no tile blob to corrupt");
    }
    const std::int64_t row = select->integer(0);
    std::vector<std::byte> data = select->blob(1);
    data[data.size() / 2] ^= std::byte{0x01};
    auto update = db->prepare("UPDATE resources SET data = ? WHERE rowid = ?");
    if (!update) {
        return fail(archmage::base::format(update.error()));
    }
    update->bind(1, data).bind(2, row);
    update->step();
    if (auto status = update->status(); !status) {
        return fail(archmage::base::format(status.error()));
    }
    return 0;
}

} // namespace

int main(int argc, char **argv) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        if (args.size() == 2 && args[0] == "write") {
            return write(args[1]);
        }
        if (args.size() == 3 && args[0] == "corrupt-tile") {
            return corruptTile(args[1], args[2]);
        }
        return fail("usage: archmage-native-fixture write <file> | corrupt-tile <in> <out>");
    } catch (const std::exception &error) {
        return fail(error.what());
    }
}
