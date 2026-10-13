// S1-5.1 (D-27): the fault grammar, and every enumerated fault point is reached by a scenario that saves
// and journals a document, and fires there.

#include "../io/native/NativeTestSupport.h"

#include "archmage/native/FaultPoints.h"
#include "archmage/native/Recovery.h"

#include <gtest/gtest.h>

namespace archmage::native {
namespace {

using commands::CommandEnvelope;

TEST(FaultPoints, ParseTheEnvironmentGrammar) {
    const auto abort = parseFaultSpec("rename");
    ASSERT_TRUE(abort);
    EXPECT_EQ(abort->point, FaultPoint::Rename);
    EXPECT_EQ(abort->mode, FaultMode::Abort);
    EXPECT_EQ(abort->hit, 1u);
    const auto third = parseFaultSpec("enospc@tile_write:3");
    ASSERT_TRUE(third);
    EXPECT_EQ(third->point, FaultPoint::TileWrite);
    EXPECT_EQ(third->mode, FaultMode::Enospc);
    EXPECT_EQ(third->hit, 3u);
    EXPECT_EQ(parseFaultSpec("error@dir_sync")->mode, FaultMode::Error);
    for (const char *bad : {"", "nowhere", "tile_write:0", "tile_write:x", "boom@rename", "rename:2:3"}) {
        EXPECT_FALSE(parseFaultSpec(bad)) << bad;
    }
    for (const FaultPoint point : kFaultPoints) {
        EXPECT_EQ(faultPointFromName(faultPointName(point)), point);
    }
}

// Saves twice (the second time through the backup fallback) and journals with compaction after every write:
// together these pass through every fault point. Returns the first failure.
base::Status scenario(const std::filesystem::path &directory) {
    testing::SampleDocument doc = testing::buildSampleDocument(160, 120);
    RecoverySession::Options options;
    options.runThread = false;
    options.compactEvery = 1;
    options.previewMaxSize = 32;
    options.previewInterval = std::chrono::milliseconds(0);
    auto session = RecoverySession::start(directory / "root", options);
    if (!session) {
        return base::unexpected(session.error());
    }
    auto journal = (*session)->track(*doc.executor);
    if (!journal) {
        return base::unexpected(journal.error());
    }
    base::Status status = (*journal)->flush();
    if (status) {
        status = testing::save(doc.executor->current(), directory / "doc.archmage",
                               identityForSave(*doc.executor->current()->state, std::nullopt))
                     .transform([](const SaveOutcome &) {});
    }
    if (status) {
        SaveRequest request;
        request.target = directory / "doc.archmage";
        request.revision = doc.executor->current();
        request.identity = identityForSave(*request.revision->state, std::nullopt);
        request.filesystem = FilesystemInfo{"forced", true, false};
        status = saveDocument(request).transform([](const SaveOutcome &) {});
    }
    (void)(*session)->untrack(*journal, true);
    return status;
}

TEST(FaultPoints, EveryPointIsReachedAndFires) {
    if (!FaultInjector::kEnabled) {
        GTEST_SKIP() << "built without ARCHMAGE_FAULT_INJECTION";
    }
    FaultInjector &injector = FaultInjector::global();
    {
        testing::ScratchDirectory dir(testing::tmpRoot());
        injector.resetHits();
        ASSERT_TRUE(scenario(dir.path()));
        for (const FaultPoint point : kFaultPoints) {
            EXPECT_GE(injector.hits(point), 1u) << faultPointName(point) << " is not reached";
        }
    }
    for (const FaultPoint point : kFaultPoints) {
        SCOPED_TRACE(faultPointName(point));
        testing::ScratchDirectory dir(testing::tmpRoot());
        injector.arm({point, FaultMode::Error, 1});
        const base::Status status = scenario(dir.path());
        injector.disarm();
        ASSERT_FALSE(status);
        EXPECT_EQ(status.error().code, "fault.injected");
        EXPECT_NE(status.error().detail.find(faultPointName(point)), std::string::npos) << status.error().detail;
    }
    testing::ScratchDirectory dir(testing::tmpRoot());
    injector.arm({FaultPoint::TileWrite, FaultMode::Enospc, 2});
    const base::Status full = scenario(dir.path());
    injector.disarm();
    ASSERT_FALSE(full);
    EXPECT_EQ(full.error().category, base::ErrorCategory::ResourceExhausted);
    EXPECT_EQ(full.error().code, "io.enospc");
}

} // namespace
} // namespace archmage::native
