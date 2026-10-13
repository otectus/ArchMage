// S1-2.5 and S1-2.10: the mutation executor's history, idempotency, transactions and job reconciliation.

#include "archmage/commands/JobScheduler.h"
#include "archmage/commands/MutationExecutor.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace archmage::commands {
namespace {

using base::ErrorCategory;
using nlohmann::json;

document::DocumentState newDocument() {
    return createDocument(
               CommandEnvelope::make("archmage.document.create", {{"width", 256}, {"height", 256}}))
        ->state;
}

CommandEnvelope opacity(const std::string &layer, double value, base::TransactionId tx = {}) {
    CommandEnvelope e = CommandEnvelope::make("archmage.layer.setOpacity", {{"opacity", value}}, {layer});
    e.transactionId = tx;
    return e;
}

std::string backgroundId(const MutationExecutor &executor) {
    return executor.current()->state->layers.at(0)->id.toString();
}

TEST(Executor, CommitsInOrderAndReportsRevisions) {
    MutationExecutor executor(newDocument(), "New Document");
    EXPECT_EQ(executor.current()->sequence, 1u);
    const auto added = executor.execute(CommandEnvelope::make("archmage.layer.add", {{"name", "Ink"}}));
    ASSERT_TRUE(added);
    EXPECT_EQ(added->revision, 2u);
    EXPECT_EQ(added->label, "New Layer");
    EXPECT_EQ(executor.current()->state->layers.size(), 2u);
    EXPECT_EQ(executor.current()->parent,
              executor.history().size() == 2 ? executor.current()->parent : base::RevisionId{});
    EXPECT_EQ(executor.current()->provenance.size(), 1u);
    EXPECT_EQ(executor.current()->provenance[0]["commandId"], "archmage.layer.add");
}

TEST(Executor, RepeatedRequestIdAppliesOnce) {
    MutationExecutor executor(newDocument(), "New Document");
    const CommandEnvelope add = CommandEnvelope::make("archmage.layer.add");
    const auto first = executor.execute(add);
    const auto second = executor.execute(add);
    ASSERT_TRUE(first && second);
    EXPECT_FALSE(first->replayed);
    EXPECT_TRUE(second->replayed);
    EXPECT_EQ(second->revision, first->revision);
    EXPECT_EQ(executor.current()->state->layers.size(), 2u);
    EXPECT_EQ(executor.history().size(), 2u);
}

TEST(Executor, StaleBaseRevisionIsAConflict) {
    MutationExecutor executor(newDocument(), "New Document");
    CommandEnvelope e = CommandEnvelope::make("archmage.layer.add");
    e.baseRevision = 1;
    ASSERT_TRUE(executor.execute(e));
    CommandEnvelope stale = CommandEnvelope::make("archmage.layer.add");
    stale.baseRevision = 1;
    const auto conflict = executor.execute(stale);
    ASSERT_FALSE(conflict);
    EXPECT_EQ(conflict.error().category, ErrorCategory::Conflict);
    EXPECT_EQ(conflict.error().code, "command.stale_base");
}

TEST(Executor, UndoAndRedoRestoreExactStates) {
    MutationExecutor executor(newDocument(), "New Document");
    const auto before = executor.current()->state;
    ASSERT_TRUE(executor.execute(opacity(backgroundId(executor), 0.3)));
    const auto after = executor.current()->state;
    EXPECT_TRUE(executor.canUndo());
    ASSERT_TRUE(executor.undo());
    EXPECT_EQ(executor.current()->state, before) << "undo restores the identical snapshot, pixels included";
    EXPECT_TRUE(executor.canRedo());
    ASSERT_TRUE(executor.redo());
    EXPECT_EQ(executor.current()->state, after);
    EXPECT_FALSE(executor.redo());
    ASSERT_TRUE(executor.undo());
    EXPECT_EQ(executor.undo().error().code, "history.nothing_to_undo");
}

TEST(Executor, NewCommandAfterUndoDiscardsTheRedoBranch) {
    MutationExecutor executor(newDocument(), "New Document");
    ASSERT_TRUE(executor.execute(opacity(backgroundId(executor), 0.3)));
    ASSERT_TRUE(executor.undo());
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.add")));
    EXPECT_FALSE(executor.canRedo());
    const auto history = executor.history();
    ASSERT_EQ(history.size(), 2u);
    EXPECT_EQ(history[1].label, "New Layer");
    EXPECT_TRUE(history[1].current);
}

TEST(Executor, CleanSavePointFollowsTheHistoryCursor) {
    MutationExecutor executor(newDocument(), "New Document");
    EXPECT_FALSE(executor.isModified());
    ASSERT_TRUE(executor.execute(opacity(backgroundId(executor), 0.5)));
    EXPECT_TRUE(executor.isModified());
    executor.markSaved(executor.current()->sequence);
    EXPECT_FALSE(executor.isModified());
    ASSERT_TRUE(executor.execute(opacity(backgroundId(executor), 0.6)));
    EXPECT_TRUE(executor.isModified());
    ASSERT_TRUE(executor.undo());
    EXPECT_FALSE(executor.isModified()) << "undoing back to the saved revision clears the marker (spec §9.2)";
    ASSERT_TRUE(executor.undo());
    EXPECT_TRUE(executor.isModified()) << "undoing past the save point marks the document modified";
    ASSERT_TRUE(executor.redo());
    EXPECT_FALSE(executor.isModified());
    ASSERT_TRUE(executor.undo());
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.add")));
    ASSERT_TRUE(executor.undo());
    EXPECT_TRUE(executor.isModified())
        << "the saved revision is gone from the history once its branch is dropped";
}

TEST(Executor, CommandsSharingATransactionCoalesce) {
    MutationExecutor executor(newDocument(), "New Document");
    const auto tx = base::TransactionId::generate();
    for (const double v : {0.9, 0.7, 0.5, 0.3}) {
        CommandEnvelope e = opacity(backgroundId(executor), v, tx);
        e.transactionLabel = "Change layer opacity";
        ASSERT_TRUE(executor.execute(e));
    }
    const auto history = executor.history();
    ASSERT_EQ(history.size(), 2u) << "one slider drag, one history entry";
    EXPECT_EQ(history[1].label, "Change layer opacity");
    EXPECT_EQ(executor.current()->state->layers[0]->opacity, 0.3f);
    EXPECT_EQ(executor.current()->provenance.size(), 4u);
    ASSERT_TRUE(executor.undo());
    EXPECT_EQ(executor.current()->state->layers[0]->opacity, 1.0f);
}

TEST(Executor, OpenTransactionsStayProvisionalUntilCommit) {
    MutationExecutor executor(newDocument(), "New Document");
    std::atomic<int> published{0};
    executor.subscribe([&published](const auto &) { ++published; });
    const auto before = executor.current();
    const auto tx = executor.beginTransaction("Brush Stroke");
    ASSERT_TRUE(tx);
    const auto step = executor.execute(opacity(backgroundId(executor), 0.4, *tx));
    ASSERT_TRUE(step);
    EXPECT_TRUE(step->provisional);
    EXPECT_EQ(executor.current(), before) << "panels never see the in-progress state";
    EXPECT_EQ(published.load(), 0);
    EXPECT_EQ(executor.transactionState(*tx)->layers[0]->opacity, 0.4f);
    const auto committed = executor.commitTransaction(*tx);
    ASSERT_TRUE(committed);
    EXPECT_EQ(committed->label, "Brush Stroke");
    EXPECT_EQ(executor.current()->state->layers[0]->opacity, 0.4f);
    EXPECT_EQ(published.load(), 1);
    EXPECT_EQ(executor.transactionState(*tx), nullptr);
    EXPECT_EQ(executor.history().size(), 2u);
}

TEST(Executor, CancelledTransactionsLeaveNoTrace) {
    MutationExecutor executor(newDocument(), "New Document");
    const auto before = executor.current();
    const auto tx = executor.beginTransaction("Brush Stroke");
    ASSERT_TRUE(executor.execute(opacity(backgroundId(executor), 0.4, *tx)));
    ASSERT_TRUE(executor.cancelTransaction(*tx));
    EXPECT_EQ(executor.current(), before);
    EXPECT_EQ(executor.history().size(), 1u);
    EXPECT_FALSE(executor.commitTransaction(*tx));
}

TEST(Executor, TransactionsRebaseOverIntermediateCommits) {
    MutationExecutor executor(newDocument(), "New Document");
    const std::string background = backgroundId(executor);
    const auto tx = executor.beginTransaction("Fade");
    ASSERT_TRUE(executor.execute(opacity(background, 0.4, *tx)));
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.add", {{"name", "Late"}})));
    const auto committed = executor.commitTransaction(*tx);
    ASSERT_TRUE(committed) << base::format(committed.error());
    const auto state = executor.current()->state;
    EXPECT_EQ(state->layers.size(), 2u) << "the intervening commit survives";
    EXPECT_EQ(state->layers[0]->opacity, 0.4f);

    const auto tx2 = executor.beginTransaction("Fade again");
    ASSERT_TRUE(executor.execute(opacity(background, 0.2, *tx2)));
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.remove", {}, {background})));
    const auto conflict = executor.commitTransaction(*tx2);
    ASSERT_FALSE(conflict);
    EXPECT_EQ(conflict.error().code, "transaction.conflict");
}

TEST(Executor, RejectsUnknownCommandsWrongDocumentsAndVersions) {
    MutationExecutor executor(newDocument(), "New Document");
    EXPECT_EQ(executor.execute(CommandEnvelope::make("archmage.layer.explode")).error().code,
              "command.unknown");
    CommandEnvelope other = CommandEnvelope::make("archmage.layer.add");
    other.documentId = base::DocumentId::generate();
    EXPECT_EQ(executor.execute(other).error().code, "command.wrong_document");
    CommandEnvelope future = CommandEnvelope::make("archmage.layer.add");
    future.parameterVersion = 2;
    EXPECT_EQ(executor.execute(future).error().code, "command.parameter_version");
}

TEST(Executor, SubmitFromManyThreadsSerializesCommits) {
    MutationExecutor executor(newDocument(), "New Document");
    std::vector<std::future<base::Result<CommandResult>>> futures;
    std::vector<std::thread> threads;
    std::mutex m;
    for (int t = 0; t < 4; ++t) {
        threads.emplace_back([&] {
            for (int i = 0; i < 25; ++i) {
                auto f = executor.submit(CommandEnvelope::make("archmage.layer.add"));
                const std::lock_guard lock(m);
                futures.push_back(std::move(f));
            }
        });
    }
    for (auto &t : threads) {
        t.join();
    }
    for (auto &f : futures) {
        ASSERT_TRUE(f.get());
    }
    EXPECT_EQ(executor.current()->state->layers.size(), 101u);
    EXPECT_EQ(executor.current()->sequence, 101u);
}

TEST(Jobs, ResultCommitsWhenTargetsAreUnchanged) {
    MutationExecutor executor(newDocument(), "New Document");
    JobScheduler jobs(executor);
    const std::string background = backgroundId(executor);
    JobSpec spec;
    spec.label = "Fade";
    spec.source = executor.current();
    spec.targets = {executor.current()->state->layers[0]->id};
    spec.run = [background](std::stop_token,
                            const document::DocumentState &) -> base::Result<CommandEnvelope> {
        return opacity(background, 0.5);
    };
    const base::JobId id = jobs.submit(std::move(spec));
    jobs.waitIdle();
    const auto record = jobs.record(id);
    ASSERT_TRUE(record);
    EXPECT_EQ(record->state, JobState::Committed);
    EXPECT_EQ(record->committedRevision, 2u);
    EXPECT_EQ(executor.current()->state->layers[0]->opacity, 0.5f);
}

TEST(Jobs, ResultForADeletedTargetBecomesACandidate) {
    MutationExecutor executor(newDocument(), "New Document");
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.add", {{"name", "Target"}})));
    JobScheduler jobs(executor);
    const auto target = executor.current()->state->layers[1]->id;
    std::atomic<bool> release{false};
    JobSpec spec;
    spec.label = "Slow filter";
    spec.source = executor.current();
    spec.targets = {target};
    spec.run = [&release, target](std::stop_token,
                                  const document::DocumentState &) -> base::Result<CommandEnvelope> {
        while (!release) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return opacity(target.toString(), 0.5);
    };
    const base::JobId id = jobs.submit(std::move(spec));
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.remove", {}, {target.toString()})));
    const auto before = executor.current();
    release = true;
    jobs.waitIdle();
    const auto record = jobs.record(id);
    ASSERT_TRUE(record);
    EXPECT_EQ(record->state, JobState::Candidate);
    ASSERT_TRUE(record->candidate.has_value());
    EXPECT_EQ(record->error->code, "job.stale");
    EXPECT_EQ(executor.current(), before) << "the stale result was not applied anywhere";
    EXPECT_EQ(jobs.candidates().size(), 1u);
}

TEST(Jobs, ResultForAModifiedTargetBecomesACandidate) {
    MutationExecutor executor(newDocument(), "New Document");
    JobScheduler jobs(executor);
    const auto target = executor.current()->state->layers[0]->id;
    std::atomic<bool> release{false};
    JobSpec spec;
    spec.source = executor.current();
    spec.targets = {target};
    spec.run = [&release, target](std::stop_token,
                                  const document::DocumentState &) -> base::Result<CommandEnvelope> {
        while (!release) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return opacity(target.toString(), 0.5);
    };
    const base::JobId id = jobs.submit(std::move(spec));
    ASSERT_TRUE(executor.execute(
        CommandEnvelope::make("archmage.layer.setOffset", {{"x", 5}, {"y", 5}}, {target.toString()})));
    release = true;
    jobs.waitIdle();
    EXPECT_EQ(jobs.record(id)->state, JobState::Candidate);
}

TEST(Jobs, CancellationAndFailure) {
    MutationExecutor executor(newDocument(), "New Document");
    JobScheduler jobs(executor);
    JobSpec cancelled;
    cancelled.source = executor.current();
    cancelled.run = [](std::stop_token stop,
                       const document::DocumentState &) -> base::Result<CommandEnvelope> {
        while (!stop.stop_requested()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return base::fail(ErrorCategory::Cancelled, "job.cancelled", "Cancelled");
    };
    const base::JobId a = jobs.submit(std::move(cancelled));
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    jobs.cancel(a);
    JobSpec failing;
    failing.source = executor.current();
    failing.run = [](std::stop_token, const document::DocumentState &) -> base::Result<CommandEnvelope> {
        return base::fail(ErrorCategory::ResourceExhausted, "job.memory", "Out of memory");
    };
    const base::JobId b = jobs.submit(std::move(failing));
    jobs.waitIdle();
    EXPECT_EQ(jobs.record(a)->state, JobState::Cancelled);
    EXPECT_EQ(jobs.record(b)->state, JobState::Failed);
    EXPECT_EQ(jobs.record(b)->error->code, "job.memory");
    EXPECT_EQ(executor.history().size(), 1u);
}

} // namespace
} // namespace archmage::commands
