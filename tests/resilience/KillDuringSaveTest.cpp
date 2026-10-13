// AT-013 (plan S1-5.5, spec §10.4): a child that saves and journals is killed at every fault point, three
// times each. Afterwards the last completed save opens, and the latest acknowledged journaled revision (or a
// later committed one) is recovered from the journal on its own.

#include "ChildProcess.h"

#include "archmage/native/FaultPoints.h"
#include "archmage/native/NativeFile.h"
#include "archmage/native/Recovery.h"
#include "archmage/native/SaveTransaction.h"

#include "../io/native/NativeTestSupport.h"

#include <gtest/gtest.h>

#include <sstream>

namespace archmage::native {
namespace {

struct Case {
    FaultPoint point;
    const char *phase; // where the fault is armed: before the third journal write or before the second save
    bool fallback;     // save through the backup fallback (the backup point)
};

struct Acknowledged {
    std::vector<std::pair<std::string, std::uint64_t>> committed;
    std::vector<std::pair<std::string, std::uint64_t>> journaled;
    std::vector<std::string> saved;
    bool survived = false;
};

Acknowledged parse(const std::vector<std::string> &lines) {
    Acknowledged ack;
    for (const std::string &line : lines) {
        std::istringstream in(line);
        std::string word, id;
        std::uint64_t sequence = 0;
        in >> word >> id >> sequence;
        if (word == "committed") {
            ack.committed.emplace_back(id, sequence);
        } else if (word == "journaled") {
            ack.journaled.emplace_back(id, sequence);
        } else if (word == "saved") {
            ack.saved.push_back(id);
        } else if (word == "survived") {
            ack.survived = true;
        }
    }
    return ack;
}

class KillDuringSave : public ::testing::TestWithParam<Case> {};

TEST_P(KillDuringSave, TheLastSaveOpensAndTheLatestAcknowledgedRevisionRecovers) {
    if (!FaultInjector::kEnabled) {
        GTEST_SKIP() << "built without ARCHMAGE_FAULT_INJECTION";
    }
    const Case c = GetParam();
    for (int repetition = 1; repetition <= 3; ++repetition) {
        SCOPED_TRACE("repetition " + std::to_string(repetition));
        testing::ScratchDirectory dir(testing::tmpRoot());
        const auto target = dir / "doc.archmage";
        std::vector<std::string> args{ARCHMAGE_CLI,         "selftest-save", "--root", (dir / "root").string(),
                                      "--target",           target.string(), "--fault",
                                      std::string(faultPointName(c.point)), "--phase", c.phase};
        if (c.fallback) {
            args.emplace_back("--fallback");
        }
        const auto child = archmage::testing::runChild(args);
        const Acknowledged ack = parse(child.lines);
        ASSERT_EQ(child.signal, 0) << child.output;
        ASSERT_EQ(child.exitCode, 137) << "the child must die at " << faultPointName(c.point) << "\n" << child.output;
        ASSERT_FALSE(ack.survived);
        ASSERT_EQ(ack.committed.size(), 3u) << child.output;
        ASSERT_GE(ack.journaled.size(), 2u) << child.output;
        ASSERT_FALSE(ack.saved.empty()) << child.output;
        const auto &revisionA = ack.committed[0].first;
        const auto &revisionC = ack.committed[2].first;

        // The last completed save opens: the previous file, or the new one when the kill came after the
        // rename.
        auto saved = readDocument(target);
        ASSERT_TRUE(saved) << base::format(saved.error());
        ASSERT_TRUE(saved->state);
        EXPECT_TRUE(saved->info.revisionId == revisionA || saved->info.revisionId == revisionC)
            << saved->info.revisionId;
        if (c.point == FaultPoint::DirSync) {
            EXPECT_EQ(saved->info.revisionId, revisionC) << "the rename had completed";
        } else {
            EXPECT_EQ(saved->info.revisionId, revisionA);
        }
        auto report = validateFile(target, ValidationLevel::Standard);
        ASSERT_TRUE(report);
        EXPECT_TRUE(report->valid());

        // The journal recovers on its own: the latest acknowledged journaled revision, or a later commit.
        auto candidates = listRecoveryCandidates(dir / "root");
        ASSERT_TRUE(candidates) << base::format(candidates.error());
        ASSERT_EQ(candidates->size(), 1u);
        const RecoveryCandidate &candidate = candidates->front();
        ASSERT_FALSE(candidate.problem) << base::format(*candidate.problem);
        auto recovered = recoverDocument(candidate);
        ASSERT_TRUE(recovered) << base::format(recovered.error());
        ASSERT_TRUE(recovered->state);
        const auto lastAcknowledged = ack.journaled.back();
        const auto committed = std::find_if(ack.committed.begin(), ack.committed.end(),
                                            [&](const auto &entry) { return entry.first == recovered->info.revisionId; });
        ASSERT_NE(committed, ack.committed.end()) << "recovered " << recovered->info.revisionId;
        EXPECT_GE(committed->second, lastAcknowledged.second)
            << "recovered revision " << committed->second << ", acknowledged " << lastAcknowledged.second;
        RecordProperty("recovered_sequence_" + std::to_string(repetition), static_cast<int>(committed->second));
    }
}

std::string caseName(const ::testing::TestParamInfo<Case> &param) {
    return std::string(faultPointName(param.param.point)) + "_" + param.param.phase;
}

INSTANTIATE_TEST_SUITE_P(FaultPoints, KillDuringSave,
                         ::testing::Values(Case{FaultPoint::JournalWrite, "journal", false},
                                           Case{FaultPoint::TileWrite, "journal", false},
                                           Case{FaultPoint::TxCommit, "journal", false},
                                           Case{FaultPoint::Compaction, "journal", false},
                                           Case{FaultPoint::Checkpoint, "journal", false},
                                           Case{FaultPoint::TileWrite, "save", false},
                                           Case{FaultPoint::TxCommit, "save", false},
                                           Case{FaultPoint::Backup, "save", true},
                                           Case{FaultPoint::Rename, "save", false},
                                           Case{FaultPoint::DirSync, "save", false}),
                         caseName);

} // namespace
} // namespace archmage::native
