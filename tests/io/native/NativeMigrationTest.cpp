// S1-4.7: the migration registry covers every older minor, and every minor this code has written keeps a
// fixture that still opens with the content recorded when it was written.

#include "NativeTestSupport.h"

#include "archmage/native/Format.h"
#include "archmage/native/Migration.h"
#include "archmage/render/CpuEvaluator.h"

#include <gtest/gtest.h>

#include <fstream>

namespace archmage::native {
namespace {

using nlohmann::json;

std::filesystem::path fixture(int major, int minor) {
    return std::filesystem::path(ARCHMAGE_FIXTURE_DIR) /
           ("v" + std::to_string(major) + "." + std::to_string(minor) + ".archmage");
}

TEST(NativeMigration, TheRegistryHasOneStepForEveryOlderMinor) {
    for (int minor = 1; minor < kSchemaMinor; ++minor) {
        int steps = 0;
        for (const MigrationStep &step : migrationSteps()) {
            steps += step.major == kSchemaMajor && step.fromMinor == minor ? 1 : 0;
        }
        EXPECT_EQ(steps, 1) << "schema " << kSchemaMajor << "." << minor << " needs exactly one migration step";
    }
    for (const MigrationStep &step : migrationSteps()) {
        EXPECT_EQ(step.major, kSchemaMajor);
        EXPECT_GE(step.fromMinor, 1);
        EXPECT_LT(step.fromMinor, kSchemaMinor) << step.description;
        EXPECT_NE(step.apply, nullptr);
    }
}

TEST(NativeMigration, AGapInTheRegistryIsAnError) {
    auto db = Database::openMemory();
    ASSERT_TRUE(db);
    ASSERT_TRUE(db->exec(schemaSql()));
    EXPECT_TRUE(migrate(*db, kSchemaMajor, kSchemaMinor)) << "nothing to do at the current minor";
    auto gap = migrate(*db, kSchemaMajor, 0);
    ASSERT_FALSE(gap);
    EXPECT_EQ(gap.error().code, "schema.migration_missing");
    EXPECT_EQ(migrate(*db, kSchemaMajor + 1, 0).error().code, "schema.no_migration");
}

TEST(NativeMigration, EveryWrittenMinorHasAFixtureThatOpens) {
    for (int minor = 1; minor <= kSchemaMinor; ++minor) {
        const std::filesystem::path file = fixture(kSchemaMajor, minor);
        SCOPED_TRACE(file.string());
        ASSERT_TRUE(std::filesystem::exists(file))
            << "schema " << kSchemaMajor << "." << minor << " has no fixture: write one with "
            << "`archmage-native-fixture write " << file.string() << "` and commit it with its .expected.json";
        std::ifstream in(file.string() + ".expected.json");
        const json expected = json::parse(in, nullptr, false);
        ASSERT_TRUE(expected.is_object()) << "missing or unreadable .expected.json";

        auto opened = readDocument(file);
        ASSERT_TRUE(opened) << base::format(opened.error());
        EXPECT_FALSE(opened->info.readOnly) << opened->info.readOnlyExplanation;
        ASSERT_TRUE(opened->state);
        const document::DocumentState &state = *opened->state;
        EXPECT_EQ(opened->info.schemaMinor, minor);
        EXPECT_EQ(state.id.toString(), expected["documentId"].get<std::string>());
        EXPECT_EQ(opened->info.revisionId, expected["revisionId"].get<std::string>());
        EXPECT_EQ(state.title, expected["title"].get<std::string>());
        EXPECT_EQ(state.width, expected["width"].get<std::int64_t>());
        EXPECT_EQ(state.height, expected["height"].get<std::int64_t>());
        EXPECT_EQ(std::string(tiles::sampleTypeName(state.sampleType)), expected["sampleType"].get<std::string>());
        EXPECT_EQ(state.colorPolicy.profile->hash().hex(), expected["profileHash"].get<std::string>());
        ASSERT_EQ(state.layers.size(), expected["layers"].size());
        for (std::size_t i = 0; i < state.layers.size(); ++i) {
            const document::Layer &layer = *state.layers[i];
            const json &want = expected["layers"][i];
            EXPECT_EQ(layer.id.toString(), want["id"].get<std::string>());
            EXPECT_EQ(layer.name, want["name"].get<std::string>());
            EXPECT_EQ(layer.visible, want["visible"].get<bool>());
            EXPECT_EQ(layer.locked, want["locked"].get<bool>());
            EXPECT_EQ(layer.opacity, want["opacity"].get<float>());
            EXPECT_EQ(layer.offset.x, want["offset"][0].get<std::int64_t>());
            EXPECT_EQ(layer.offset.y, want["offset"][1].get<std::int64_t>());
            EXPECT_EQ(layer.content->id.toString(), want["raster"].get<std::string>());
        }
        const std::vector<std::byte> pixels = testing::composite(state);
        for (const json &sample : expected["samples"]) {
            const auto x = sample["x"].get<std::int64_t>();
            const auto y = sample["y"].get<std::int64_t>();
            for (int c = 0; c < 4; ++c) {
                EXPECT_EQ(static_cast<int>(pixels[static_cast<std::size_t>((y * state.width + x) * 4 + c)]),
                          sample["rgba"][static_cast<std::size_t>(c)].get<int>())
                    << x << "," << y << " channel " << c;
            }
        }
        auto report = validateFile(file, ValidationLevel::Deep);
        ASSERT_TRUE(report);
        EXPECT_TRUE(report->valid());
    }
}

} // namespace
} // namespace archmage::native
