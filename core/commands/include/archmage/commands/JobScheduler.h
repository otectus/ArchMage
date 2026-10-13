// Long-running jobs and stale-result reconciliation (D-24, spec §9.3).
//
// A job evaluates an immutable source revision on a worker thread and produces a command. When it
// finishes, the scheduler checks that every target still exists and is unchanged since the source
// revision (same content generation, offset and lock state). Only then is the command committed through
// the executor; otherwise the result is kept as a candidate the user can insert or re-run. A job never
// overwrites intervening edits or attaches to whichever layer happens to be selected.
//
// Thread safety: every public method is safe from any thread.
#pragma once

#include "archmage/commands/MutationExecutor.h"

#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <thread>
#include <vector>

namespace archmage::commands {

enum class JobState { Queued, Running, Committed, Candidate, Failed, Cancelled };

[[nodiscard]] std::string_view jobStateName(JobState state);

struct JobSpec {
    std::string label;
    std::shared_ptr<const document::Revision> source; // the revision the job reads
    std::vector<base::LayerId> targets;               // layers the result applies to
    /// Runs on a worker. Returns the command to commit, built from the source state.
    std::function<base::Result<CommandEnvelope>(std::stop_token, const document::DocumentState &)> run;
};

struct JobRecord {
    base::JobId id;
    std::string label;
    JobState state = JobState::Queued;
    std::uint64_t sourceRevision = 0;
    std::uint64_t committedRevision = 0;      // when Committed
    std::optional<CommandEnvelope> candidate; // when Candidate: the result, kept for the user
    std::optional<base::Error> error;         // when Failed or Cancelled, or why a result became a candidate
};

class JobScheduler {
public:
    explicit JobScheduler(MutationExecutor &executor, std::size_t threads = 2);
    ~JobScheduler();
    JobScheduler(const JobScheduler &) = delete;
    JobScheduler &operator=(const JobScheduler &) = delete;

    base::JobId submit(JobSpec spec);
    /// Requests cancellation; a running job stops at its next checkpoint.
    void cancel(base::JobId job);
    [[nodiscard]] std::optional<JobRecord> record(base::JobId job) const;
    [[nodiscard]] std::vector<JobRecord> candidates() const;
    /// Blocks until no job is queued or running.
    void waitIdle();

private:
    struct Fingerprint {
        bool exists = false;
        std::uint64_t generation = 0;
        base::IPoint offset;
        bool locked = false;
        friend bool operator==(const Fingerprint &, const Fingerprint &) = default;
    };
    struct Entry {
        JobSpec spec;
        JobRecord record;
        std::stop_source stop;
        std::vector<Fingerprint> fingerprints;
    };

    static Fingerprint fingerprint(const document::DocumentState &state, base::LayerId layer);
    void worker(std::stop_token stop);
    void finish(base::JobId id, base::Result<CommandEnvelope> outcome);

    MutationExecutor &executor_;
    mutable std::mutex mutex_;
    std::condition_variable_any changed_;
    std::deque<base::JobId> queue_;
    std::map<base::JobId, Entry> jobs_;
    std::size_t active_ = 0;
    std::vector<std::jthread> workers_;
};

} // namespace archmage::commands
