// AT-014 (plan S1-5.6, spec §10.3, §26.2): saving runs out of space, once through RLIMIT_FSIZE on a child
// process and once through an injected ENOSPC. The original stays intact, the error is resource_exhausted
// and names the file, the document stays modified, journaling continues and Save As elsewhere succeeds.

#include "ChildProcess.h"

#include "archmage/native/FaultPoints.h"
#include "archmage/native/NativeFile.h"
#include "archmage/native/Recovery.h"
#include "archmage/native/SaveTransaction.h"

#include "../io/native/NativeTestSupport.h"

#include <gtest/gtest.h>

namespace archmage::native {
namespace {

class DiskFull : public ::testing::TestWithParam<const char *> {};

TEST_P(DiskFull, NoDamagedOriginalNoFakeSuccessAndAWayOut) {
    if (std::string_view(GetParam()) == "enospc" && !FaultInjector::kEnabled) {
        GTEST_SKIP() << "built without ARCHMAGE_FAULT_INJECTION";
    }
    testing::ScratchDirectory dir(testing::homeRoot());
    const auto target = dir / "original.archmage";
    const auto elsewhere = dir / "elsewhere.archmage";
    {
        auto created = commands::createDocument(commands::CommandEnvelope::make(
            "archmage.document.create", {{"width", 1024}, {"height", 768}, {"title", "AT-014"}}));
        ASSERT_TRUE(created);
        ASSERT_TRUE(testing::save(std::make_shared<document::Revision>(document::Revision{
                                      base::RevisionId::generate(), 1, {},
                                      std::make_shared<const document::DocumentState>(created->state), "New", {}, {}}),
                                  target, identityForSave(created->state, std::nullopt)));
    }
    const std::string before = testing::fileHash(target);

    const auto child = archmage::testing::runChild({ARCHMAGE_CLI, "selftest-disk-full", "--root",
                                                    (dir / "root").string(), "--target", target.string(),
                                                    "--save-as", elsewhere.string(), "--mode", GetParam()});
    ASSERT_EQ(child.signal, 0) << child.output;
    ASSERT_EQ(child.exitCode, 0) << child.output;
    const auto has = [&](const std::string &prefix) {
        return std::find_if(child.lines.begin(), child.lines.end(),
                            [&](const std::string &line) { return line.starts_with(prefix); }) != child.lines.end();
    };
    const auto at = [&](const std::string &prefix) {
        return std::find_if(child.lines.begin(), child.lines.end(),
                            [&](const std::string &line) { return line.starts_with(prefix); });
    };
    EXPECT_TRUE(has("save_failed resource_exhausted")) << child.output;
    ASSERT_TRUE(has("detail ")) << child.output;
    EXPECT_NE(at("detail ")->find(target.string()), std::string::npos) << "the error names the file: " << *at("detail ");
    EXPECT_TRUE(has("original intact")) << child.output;
    EXPECT_TRUE(has("modified true")) << "no fake success state\n" << child.output;
    // Journaling continues after the failure.
    const auto failure = at("save_failed");
    EXPECT_TRUE(std::any_of(failure, child.lines.end(), [](const std::string &line) { return line.starts_with("journaled"); }))
        << child.output;
    EXPECT_TRUE(has("saved_as " + elsewhere.string())) << child.output;

    EXPECT_EQ(testing::fileHash(target), before) << "the original is byte-identical";
    EXPECT_TRUE(readDocument(target));
    EXPECT_TRUE(strayTemporaries(target).empty());
    auto copy = readDocument(elsewhere);
    ASSERT_TRUE(copy) << base::format(copy.error());
    ASSERT_TRUE(copy->state);
    EXPECT_EQ(copy->state->layers.back()->name, "After the failure");
    EXPECT_FALSE(copy->info.derivedFrom.isNull()) << "Save As records where the copy came from";
    auto candidates = listRecoveryCandidates(dir / "root");
    ASSERT_TRUE(candidates);
    EXPECT_TRUE(candidates->empty()) << "the child closed its document cleanly";
}

INSTANTIATE_TEST_SUITE_P(Modes, DiskFull, ::testing::Values("rlimit", "enospc"),
                         [](const auto &suite) { return std::string(suite.param); });

} // namespace
} // namespace archmage::native
