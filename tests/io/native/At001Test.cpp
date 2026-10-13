// AT-001, engine half (plan S1-4.9): a layered document created and painted through commands, saved and
// reopened, keeps its pixels, layer names, order, opacity and visibility, color policy and sample type.
// The UI half (open, edit, save through the shell) is S1-7/S1-8.

#include "NativeTestSupport.h"

#include "archmage/render/CpuEvaluator.h"

#include <gtest/gtest.h>

namespace archmage::native {
namespace {

using commands::CommandEnvelope;

std::vector<std::byte> render(const document::DocumentState &state, tiles::SampleType type) {
    render::CpuEvaluator evaluator(render::CpuEvaluator::Options{.threads = 2});
    render::RenderRequest request;
    request.document = state.id;
    request.region = state.canvasRect();
    request.destination.sampleType = type;
    auto result = evaluator.render(state, request);
    EXPECT_TRUE(result) << base::format(result.error());
    return result ? result->pixels : std::vector<std::byte>{};
}

class At001 : public ::testing::TestWithParam<const char *> {};

TEST_P(At001, APaintedLayeredDocumentSurvivesSaveAndReopen) {
    testing::ScratchDirectory dir(testing::homeRoot());
    auto created = commands::createDocument(CommandEnvelope::make(
        "archmage.document.create",
        {{"width", 640}, {"height", 480}, {"sampleType", GetParam()}, {"title", "AT-001"}, {"background", "white"}}));
    ASSERT_TRUE(created) << base::format(created.error());
    commands::MutationExecutor executor(created->state, "New Document");
    const auto run = [&](CommandEnvelope envelope) {
        auto result = executor.execute(std::move(envelope));
        EXPECT_TRUE(result) << base::format(result.error());
        return result ? result->result : nlohmann::json::object();
    };
    const std::string sky = run(CommandEnvelope::make("archmage.layer.add", {{"name", "Sky"}}))["layerId"];
    const std::string trees = run(CommandEnvelope::make("archmage.layer.add", {{"name", "Trees"}}))["layerId"];
    const std::string notes = run(CommandEnvelope::make("archmage.layer.add", {{"name", "Notes"}}))["layerId"];
    brush::BrushSettings broad;
    broad.color = {0.3f, 0.5f, 0.9f};
    broad.size = 80.0;
    broad.hardness = 0.3;
    run(testing::stroke(sky, broad, testing::line({0, 60}, {640, 90}, 80)));
    brush::BrushSettings fine;
    fine.color = {0.1f, 0.45f, 0.15f};
    fine.size = 12.0;
    run(testing::stroke(trees, fine, testing::line({100, 470}, {130, 200}, 60)));
    run(testing::stroke(trees, fine, testing::line({500, 470}, {470, 230}, 60)));
    run(testing::stroke(notes, fine, testing::line({20, 20}, {200, 20}, 30)));
    run(CommandEnvelope::make("archmage.layer.setOpacity", {{"opacity", 0.65}}, {sky}));
    run(CommandEnvelope::make("archmage.layer.setVisible", {{"visible", false}}, {notes}));
    run(CommandEnvelope::make("archmage.layer.reorder", {{"index", 1}}, {trees}));
    run(CommandEnvelope::make("archmage.layer.rename", {{"name", "Forest"}}, {trees}));

    const auto revision = executor.current();
    const document::DocumentState &before = *revision->state;
    auto saved = testing::save(revision, dir / "at001.archmage", identityForSave(before, std::nullopt));
    ASSERT_TRUE(saved) << base::format(saved.error());
    executor.markSaved(saved->sequence);
    EXPECT_FALSE(executor.isModified());

    auto reopened = readDocument(dir / "at001.archmage");
    ASSERT_TRUE(reopened) << base::format(reopened.error());
    ASSERT_TRUE(reopened->state);
    const document::DocumentState &after = *reopened->state;
    EXPECT_EQ(after.sampleType, before.sampleType);
    EXPECT_TRUE(after.colorPolicy.profile->sameAs(*before.colorPolicy.profile));
    EXPECT_EQ(after.colorPolicy.blendPolicy, before.colorPolicy.blendPolicy);
    ASSERT_EQ(after.layers.size(), 4u);
    for (std::size_t i = 0; i < after.layers.size(); ++i) {
        EXPECT_EQ(after.layers[i]->name, before.layers[i]->name);
        EXPECT_EQ(after.layers[i]->opacity, before.layers[i]->opacity);
        EXPECT_EQ(after.layers[i]->visible, before.layers[i]->visible);
        EXPECT_TRUE(testing::sameTiles(after.layers[i]->content->tiles, before.layers[i]->content->tiles));
    }
    EXPECT_EQ(after.layers[1]->name, "Forest");
    EXPECT_EQ(after.layers[2]->opacity, 0.65f);
    EXPECT_FALSE(after.layers[3]->visible);
    EXPECT_EQ(render(after, tiles::SampleType::UInt8), render(before, tiles::SampleType::UInt8));
    EXPECT_EQ(render(after, tiles::SampleType::Float32), render(before, tiles::SampleType::Float32));
}

INSTANTIATE_TEST_SUITE_P(SampleTypes, At001, ::testing::Values("uint8", "float32"),
                         [](const auto &suite) { return std::string(suite.param); });

} // namespace
} // namespace archmage::native
