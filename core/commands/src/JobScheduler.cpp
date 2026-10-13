#include "archmage/commands/JobScheduler.h"

namespace archmage::commands {
namespace {

using base::ErrorCategory;

} // namespace

std::string_view jobStateName(JobState state) {
    switch (state) {
    case JobState::Queued:
        return "queued";
    case JobState::Running:
        return "running";
    case JobState::Committed:
        return "committed";
    case JobState::Candidate:
        return "candidate";
    case JobState::Failed:
        return "failed";
    case JobState::Cancelled:
        return "cancelled";
    }
    return "unknown";
}

JobScheduler::JobScheduler(MutationExecutor &executor, std::size_t threads) : executor_(executor) {
    for (std::size_t i = 0; i < std::max<std::size_t>(1, threads); ++i) {
        workers_.emplace_back([this](std::stop_token stop) { worker(stop); });
    }
}

JobScheduler::~JobScheduler() {
    {
        const std::lock_guard lock(mutex_);
        for (auto &[id, entry] : jobs_) {
            entry.stop.request_stop();
        }
    }
    for (auto &worker : workers_) {
        worker.request_stop();
    }
    changed_.notify_all();
    workers_.clear();
}

JobScheduler::Fingerprint JobScheduler::fingerprint(const document::DocumentState &state,
                                                    base::LayerId layer) {
    const document::Layer *found = state.findLayer(layer);
    if (found == nullptr) {
        return {};
    }
    return {true, found->content->tiles.generation(), found->offset, found->locked};
}

base::JobId JobScheduler::submit(JobSpec spec) {
    Entry entry;
    entry.record.id = base::JobId::generate();
    entry.record.label = spec.label;
    entry.record.sourceRevision = spec.source->sequence;
    for (const base::LayerId target : spec.targets) {
        entry.fingerprints.push_back(fingerprint(*spec.source->state, target));
    }
    entry.spec = std::move(spec);
    const base::JobId id = entry.record.id;
    {
        const std::lock_guard lock(mutex_);
        jobs_.emplace(id, std::move(entry));
        queue_.push_back(id);
    }
    changed_.notify_all();
    return id;
}

void JobScheduler::cancel(base::JobId job) {
    const std::lock_guard lock(mutex_);
    if (const auto it = jobs_.find(job); it != jobs_.end()) {
        it->second.stop.request_stop();
    }
}

std::optional<JobRecord> JobScheduler::record(base::JobId job) const {
    const std::lock_guard lock(mutex_);
    const auto it = jobs_.find(job);
    if (it == jobs_.end()) {
        return std::nullopt;
    }
    return it->second.record;
}

std::vector<JobRecord> JobScheduler::candidates() const {
    const std::lock_guard lock(mutex_);
    std::vector<JobRecord> out;
    for (const auto &[id, entry] : jobs_) {
        if (entry.record.state == JobState::Candidate) {
            out.push_back(entry.record);
        }
    }
    return out;
}

void JobScheduler::waitIdle() {
    std::unique_lock lock(mutex_);
    changed_.wait(lock, [this] { return queue_.empty() && active_ == 0; });
}

void JobScheduler::worker(std::stop_token stop) {
    for (;;) {
        base::JobId id;
        std::stop_token jobStop;
        std::function<base::Result<CommandEnvelope>(std::stop_token, const document::DocumentState &)> run;
        std::shared_ptr<const document::DocumentState> source;
        {
            std::unique_lock lock(mutex_);
            if (!changed_.wait(lock, stop, [this] { return !queue_.empty(); })) {
                return;
            }
            id = queue_.front();
            queue_.pop_front();
            Entry &entry = jobs_.at(id);
            entry.record.state = JobState::Running;
            jobStop = entry.stop.get_token();
            run = entry.spec.run;
            source = entry.spec.source->state;
            ++active_;
        }
        base::Result<CommandEnvelope> outcome =
            jobStop.stop_requested()
                ? base::Result<CommandEnvelope>(
                      base::fail(ErrorCategory::Cancelled, "job.cancelled", "The job was cancelled"))
                : run(jobStop, *source);
        finish(id, std::move(outcome));
    }
}

void JobScheduler::finish(base::JobId id, base::Result<CommandEnvelope> outcome) {
    std::optional<CommandEnvelope> toCommit;
    std::vector<base::LayerId> targets;
    std::vector<Fingerprint> fingerprints;
    std::uint64_t sourceRevision = 0;
    {
        const std::lock_guard lock(mutex_);
        Entry &entry = jobs_.at(id);
        if (entry.stop.stop_requested() ||
            (!outcome && outcome.error().category == ErrorCategory::Cancelled)) {
            entry.record.state = JobState::Cancelled;
            entry.record.error =
                base::Error{ErrorCategory::Cancelled, "job.cancelled", "The job was cancelled", {}};
        } else if (!outcome) {
            entry.record.state = JobState::Failed;
            entry.record.error = std::move(outcome).error();
        } else {
            toCommit = std::move(*outcome);
            targets = entry.spec.targets;
            fingerprints = entry.fingerprints;
            sourceRevision = entry.record.sourceRevision;
        }
    }
    if (toCommit) {
        // Spec §9.3 steps 1-2: commit only if every target is exactly as the job saw it, checked together
        // with the commit on the executor thread.
        toCommit->baseRevision = 0;
        auto committed =
            executor_.executeIf(*toCommit, [&](const document::DocumentState &state) -> base::Status {
                for (std::size_t i = 0; i < targets.size(); ++i) {
                    if (!fingerprints[i].exists || !(fingerprint(state, targets[i]) == fingerprints[i])) {
                        return base::fail(ErrorCategory::Conflict, "job.stale",
                                          "The document changed while the job ran; its result was kept aside",
                                          "source revision " + std::to_string(sourceRevision) + ", target " +
                                              targets[i].toString());
                    }
                }
                return {};
            });
        const std::lock_guard lock(mutex_);
        Entry &entry = jobs_.at(id);
        if (committed) {
            entry.record.state = JobState::Committed;
            entry.record.committedRevision = committed->revision;
        } else {
            // Step 3: keep the result as a recoverable candidate, never an overwrite.
            entry.record.state = JobState::Candidate;
            entry.record.candidate = std::move(*toCommit);
            entry.record.error = std::move(committed).error();
        }
    }
    {
        const std::lock_guard lock(mutex_);
        --active_;
    }
    changed_.notify_all();
}

} // namespace archmage::commands
