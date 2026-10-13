// S1-2.5 and S1-2.6: command envelopes and the Stage 1 document and layer commands.

#include "archmage/commands/Command.h"

#include <gtest/gtest.h>

namespace archmage::commands {
namespace {

using base::ErrorCategory;
using nlohmann::json;

document::DocumentState newDocument(const char *background = "white", const char *type = "uint8") {
    auto effect = createDocument(CommandEnvelope::make(
        "archmage.document.create",
        {{"width", 300}, {"height", 200}, {"background", background}, {"sampleType", type}, {"title", "T"}}));
    EXPECT_TRUE(effect) << base::format(effect.error());
    return effect->state;
}

base::Result<CommandEffect> run(const document::DocumentState &state, std::string id, json parameters,
                                std::vector<std::string> targets = {}) {
    const CommandDefinition *definition = CommandRegistry::builtIn().find(id);
    EXPECT_NE(definition, nullptr) << id;
    CommandEnvelope envelope =
        CommandEnvelope::make(std::move(id), std::move(parameters), std::move(targets));
    // Every command's envelope survives a JSON round trip unchanged (plan S1-2.6 DoD).
    auto reparsed = CommandEnvelope::fromJson(envelope.toJson());
    EXPECT_TRUE(reparsed);
    EXPECT_EQ(reparsed->toJson(), envelope.toJson());
    return definition->apply(*reparsed, state);
}

std::string layerId(const document::DocumentState &state, std::size_t index) {
    return state.layers.at(index)->id.toString();
}

TEST(Envelope, RoundTripsEveryField) {
    CommandEnvelope e = CommandEnvelope::make("archmage.layer.setOpacity", {{"opacity", 0.65}}, {"layer-x"});
    e.documentId = base::DocumentId::generate();
    e.baseRevision = 104;
    e.transactionId = base::TransactionId::generate();
    e.transactionLabel = "Change layer opacity";
    const json text = e.toJson();
    EXPECT_EQ(text["protocolVersion"], 1);
    EXPECT_EQ(text["transaction"]["label"], "Change layer opacity");
    const auto back = CommandEnvelope::fromJson(text);
    ASSERT_TRUE(back);
    EXPECT_EQ(back->requestId, e.requestId);
    EXPECT_EQ(back->documentId, e.documentId);
    EXPECT_EQ(back->baseRevision, 104u);
    EXPECT_EQ(back->transactionId, e.transactionId);
    EXPECT_EQ(back->targetIds, e.targetIds);
    EXPECT_EQ(back->parameters["opacity"], 0.65);
}

TEST(Envelope, MalformedInputIsRejected) {
    EXPECT_EQ(CommandEnvelope::fromJson(json::array()).error().code, "envelope.malformed");
    EXPECT_EQ(CommandEnvelope::fromJson({{"commandId", 5}}).error().code, "envelope.malformed");
    EXPECT_EQ(CommandEnvelope::fromJson({{"commandId", "x"}, {"protocolVersion", 2}}).error().code,
              "envelope.protocol");
    EXPECT_EQ(CommandEnvelope::fromJson({{"commandId", "x"}, {"requestId", "req-bad"}}).error().code,
              "id.malformed");
    EXPECT_FALSE(CommandEnvelope::fromJson({{"commandId", "x"}, {"parameters", json::array()}}));
    EXPECT_FALSE(CommandEnvelope::fromJson({{"commandId", "x"}, {"targetIds", {1, 2}}}));
}

TEST(CreateDocument, WhiteBackgroundCoversExactlyTheCanvas) {
    const document::DocumentState doc = newDocument();
    ASSERT_EQ(doc.layers.size(), 1u);
    const auto &tiles = doc.layers[0]->content->tiles;
    EXPECT_EQ(tiles.pixelBounds(), (base::IRect{0, 0, 384, 256}));
    EXPECT_TRUE(tiles.at({0, 0})->isUniform());
    const auto edge = tiles.at({2, 1}); // pixels 256..383 x 128..255; the canvas ends at 300 x 200
    EXPECT_FALSE(edge->isUniform());
    EXPECT_EQ(edge->sample(3, 43, 71), 1.0f); // (299, 199)
    EXPECT_EQ(edge->sample(3, 44, 71), 0.0f); // (300, 199): beyond the canvas
    EXPECT_EQ(edge->sample(3, 43, 72), 0.0f); // (299, 200)
    EXPECT_EQ(doc.layers[0]->name, "Background");
}

TEST(CreateDocument, ParametersAreValidated) {
    const document::DocumentState transparent = newDocument("transparent", "float32");
    EXPECT_EQ(transparent.layers[0]->content->tiles.tileCount(), 0u);
    EXPECT_EQ(transparent.sampleType, tiles::SampleType::Float32);
    EXPECT_FALSE(createDocument(CommandEnvelope::make("archmage.document.create", {{"width", 10}})));
    EXPECT_EQ(createDocument(CommandEnvelope::make("archmage.document.create",
                                                   {{"width", 10}, {"height", 10}, {"sampleType", "uint16"}}))
                  .error()
                  .category,
              ErrorCategory::UnsupportedInput);
    EXPECT_FALSE(createDocument(CommandEnvelope::make(
        "archmage.document.create", {{"width", 10}, {"height", 10}, {"background", "plaid"}})));
}

TEST(LayerCommands, AddInsertsAboveATargetOrAtAnIndex) {
    const document::DocumentState doc = newDocument();
    const auto top = run(doc, "archmage.layer.add", {{"name", "Ink"}});
    ASSERT_TRUE(top);
    EXPECT_EQ(top->state.layers.size(), 2u);
    EXPECT_EQ(top->state.layers[1]->name, "Ink");
    EXPECT_EQ(top->result["layerId"].get<std::string>(), layerId(top->state, 1));
    const auto bottom = run(top->state, "archmage.layer.add", {{"index", 0}});
    ASSERT_TRUE(bottom);
    EXPECT_EQ(bottom->state.layers[0]->name, "Layer 3");
    const auto above = run(bottom->state, "archmage.layer.add", {}, {layerId(bottom->state, 0)});
    EXPECT_EQ(above->state.layers[1]->id.toString(), above->result["layerId"].get<std::string>());
    EXPECT_FALSE(run(doc, "archmage.layer.add", {{"index", 7}}));
}

TEST(LayerCommands, PropertyEditsAndLocks) {
    document::DocumentState doc = newDocument();
    const std::string id = layerId(doc, 0);
    const auto opacity = run(doc, "archmage.layer.setOpacity", {{"opacity", 0.25}}, {id});
    ASSERT_TRUE(opacity);
    EXPECT_EQ(opacity->state.layers[0]->opacity, 0.25f);
    EXPECT_EQ(doc.layers[0]->opacity, 1.0f) << "commands never mutate their input";
    EXPECT_EQ(opacity->state.layers[0]->content, doc.layers[0]->content);

    const auto locked = run(doc, "archmage.layer.setLocked", {{"locked", true}}, {id});
    ASSERT_TRUE(locked);
    for (const auto &[command, parameters] :
         std::vector<std::pair<std::string, json>>{{"archmage.layer.setOpacity", {{"opacity", 0.5}}},
                                                   {"archmage.layer.setOffset", {{"x", 1}, {"y", 2}}},
                                                   {"archmage.layer.remove", json::object()}}) {
        const auto rejected = run(locked->state, command, parameters, {id});
        ASSERT_FALSE(rejected) << command;
        EXPECT_EQ(rejected.error().code, "layer.locked") << command;
    }
    EXPECT_TRUE(run(locked->state, "archmage.layer.rename", {{"name", "Paper"}}, {id}));
    EXPECT_TRUE(run(locked->state, "archmage.layer.setVisible", {{"visible", false}}, {id}));

    const auto moved = run(doc, "archmage.layer.setOffset", {{"x", -37}, {"y", 59}}, {id});
    EXPECT_EQ(moved->state.layers[0]->offset, (base::IPoint{-37, 59}));
    EXPECT_EQ(run(doc, "archmage.layer.setOpacity", {{"opacity", 1.5}}, {id}).error().code,
              "command.parameters");
    EXPECT_EQ(run(doc, "archmage.layer.setVisible", {{"visible", "yes"}}, {id}).error().code,
              "command.parameters");
    EXPECT_EQ(run(doc, "archmage.layer.rename", {{"name", ""}}, {id}).error().code, "command.parameters");
}

TEST(LayerCommands, TargetsMustExist) {
    const document::DocumentState doc = newDocument();
    const auto unknown =
        run(doc, "archmage.layer.setVisible", {{"visible", false}}, {base::LayerId::generate().toString()});
    ASSERT_FALSE(unknown);
    EXPECT_EQ(unknown.error().code, "layer.not_found");
    EXPECT_EQ(run(doc, "archmage.layer.setVisible", {{"visible", false}}, {}).error().code,
              "command.targets");
    EXPECT_EQ(run(doc, "archmage.layer.setVisible", {{"visible", false}}, {"doc-0"}).error().code,
              "command.targets");
}

TEST(LayerCommands, ReorderDuplicateAndRemove) {
    document::DocumentState doc = newDocument();
    doc = run(doc, "archmage.layer.add", {{"name", "A"}})->state;
    doc = run(doc, "archmage.layer.add", {{"name", "B"}})->state;
    const auto reordered = run(doc, "archmage.layer.reorder", {{"index", 0}}, {layerId(doc, 2)});
    ASSERT_TRUE(reordered);
    EXPECT_EQ(reordered->state.layers[0]->name, "B");
    EXPECT_EQ(reordered->state.layers[1]->name, "Background");
    EXPECT_FALSE(run(doc, "archmage.layer.reorder", {{"index", 3}}, {layerId(doc, 0)}));

    const auto duplicated = run(doc, "archmage.layer.duplicateInstance", {}, {layerId(doc, 0)});
    ASSERT_TRUE(duplicated);
    EXPECT_EQ(duplicated->state.layers[1]->name, "Background copy");
    EXPECT_EQ(duplicated->state.layers[1]->content, doc.layers[0]->content)
        << "an instance shares its content";
    EXPECT_NE(duplicated->state.layers[1]->id, doc.layers[0]->id);

    const auto removed = run(doc, "archmage.layer.remove", {}, {layerId(doc, 1)});
    ASSERT_TRUE(removed);
    EXPECT_EQ(removed->state.layers.size(), 2u);
    EXPECT_EQ(removed->state.layers[1]->name, "B");
}

TEST(Registry, ListsTheStage1Commands) {
    const std::vector<std::string> ids = CommandRegistry::builtIn().ids();
    for (const char *id :
         {"archmage.layer.add", "archmage.layer.remove", "archmage.layer.rename", "archmage.layer.reorder",
          "archmage.layer.duplicateInstance", "archmage.layer.setVisible", "archmage.layer.setOpacity",
          "archmage.layer.setLocked", "archmage.layer.setOffset"}) {
        EXPECT_NE(std::find(ids.begin(), ids.end(), id), ids.end()) << id;
    }
}

} // namespace
} // namespace archmage::commands
