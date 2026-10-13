#include "archmage/commands/MutationExecutor.h"

namespace archmage::commands {
namespace {

using base::ErrorCategory;

} // namespace

MutationExecutor::MutationExecutor(document::DocumentState initial, std::string initialLabel)
    : MutationExecutor(std::move(initial), std::move(initialLabel), Options{}) {}

MutationExecutor::MutationExecutor(document::DocumentState initial, std::string initialLabel, Options options,
                                   const CommandRegistry &registry)
    : registry_(registry), options_(options), documentId_(initial.id) {
    auto first =
        makeRevision(nullptr, std::move(initial), std::move(initialLabel), {}, nlohmann::json::array());
    chain_.push_back(first);
    publish(first);
    thread_ = std::jthread([this](std::stop_token stop) { loop(stop); });
    executorThread_ = thread_.get_id(); // written before any other thread can call in
}

MutationExecutor::~MutationExecutor() {
    thread_.request_stop();
    queueReady_.notify_all();
}

void MutationExecutor::loop(std::stop_token stop) {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(queueMutex_);
            if (!queueReady_.wait(lock, stop, [this] { return !queue_.empty(); })) {
                return;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
    }
}

template <class F> auto MutationExecutor::runOnExecutor(F &&f) -> decltype(f()) {
    using R = decltype(f());
    if (std::this_thread::get_id() == executorThread_) {
        return f();
    }
    auto task = std::make_shared<std::packaged_task<R()>>(std::forward<F>(f));
    std::future<R> future = task->get_future();
    {
        const std::lock_guard lock(queueMutex_);
        queue_.emplace_back([task] { (*task)(); });
    }
    queueReady_.notify_one();
    return future.get();
}

std::future<base::Result<CommandResult>> MutationExecutor::submit(CommandEnvelope envelope) {
    auto task = std::make_shared<std::packaged_task<base::Result<CommandResult>()>>(
        [this, envelope = std::move(envelope)] { return applyCommand(envelope); });
    std::future<base::Result<CommandResult>> future = task->get_future();
    if (std::this_thread::get_id() == executorThread_) {
        (*task)();
        return future;
    }
    {
        const std::lock_guard lock(queueMutex_);
        queue_.emplace_back([task] { (*task)(); });
    }
    queueReady_.notify_one();
    return future;
}

base::Result<CommandResult> MutationExecutor::execute(CommandEnvelope envelope) {
    return runOnExecutor([this, envelope = std::move(envelope)] { return applyCommand(envelope); });
}

base::Result<CommandResult> MutationExecutor::executeIf(CommandEnvelope envelope, Precondition precondition) {
    return runOnExecutor([this, envelope = std::move(envelope),
                          precondition = std::move(precondition)]() -> base::Result<CommandResult> {
        if (auto holds = precondition(*chain_[cursor_]->state); !holds) {
            return base::unexpected(std::move(holds).error());
        }
        return applyCommand(envelope);
    });
}

std::shared_ptr<const document::Revision> MutationExecutor::makeRevision(const document::Revision *parent,
                                                                         document::DocumentState state,
                                                                         std::string label,
                                                                         base::TransactionId transaction,
                                                                         nlohmann::json provenance) {
    auto revision = std::make_shared<document::Revision>();
    revision->id = base::RevisionId::generate();
    revision->sequence = nextSequence_++;
    if (parent != nullptr) {
        revision->parent = parent->id;
    }
    revision->state = std::make_shared<const document::DocumentState>(std::move(state));
    revision->label = std::move(label);
    revision->transaction = transaction;
    revision->provenance = std::move(provenance);
    return revision;
}

void MutationExecutor::publish(std::shared_ptr<const document::Revision> revision) {
    std::vector<Observer> observers;
    {
        const std::lock_guard lock(snapshotMutex_);
        current_ = revision;
        historyView_.clear();
        for (std::size_t i = 0; i < chain_.size(); ++i) {
            historyView_.push_back({chain_[i]->sequence, chain_[i]->label, i == cursor_});
        }
        for (const auto &[id, observer] : observers_) {
            observers.push_back(observer);
        }
    }
    for (const Observer &observer : observers) {
        observer(revision);
    }
}

CommandResult MutationExecutor::resultFor(const document::Revision &revision) const {
    CommandResult result;
    result.revision = revision.sequence;
    result.revisionId = revision.id;
    result.label = revision.label;
    return result;
}

void MutationExecutor::remember(const CommandEnvelope &envelope, const CommandResult &result) {
    if (envelope.requestId.isNull() || options_.idempotencyWindow == 0) {
        return;
    }
    remembered_[envelope.requestId] = result;
    rememberedOrder_.push_back(envelope.requestId);
    while (rememberedOrder_.size() > options_.idempotencyWindow) {
        remembered_.erase(rememberedOrder_.front());
        rememberedOrder_.pop_front();
    }
}

base::Result<CommandEffect> MutationExecutor::applyDefinition(const CommandEnvelope &envelope,
                                                              const document::DocumentState &state) const {
    const CommandDefinition *definition = registry_.find(envelope.commandId);
    if (definition == nullptr) {
        return base::fail(ErrorCategory::Validation, "command.unknown", "Unknown command",
                          envelope.commandId);
    }
    if (envelope.parameterVersion != definition->parameterVersion()) {
        return base::fail(ErrorCategory::UnsupportedInput, "command.parameter_version",
                          "The command's parameter version is not supported",
                          envelope.commandId + " v" + std::to_string(envelope.parameterVersion));
    }
    auto effect = definition->apply(envelope, state);
    if (!effect) {
        return effect;
    }
    if (auto valid = document::validate(effect->state); !valid) {
        // A command must preserve the invariants; a violation is an ArchMage defect.
        return base::fail(ErrorCategory::Internal, "command.invariant",
                          "The command produced an invalid document",
                          envelope.commandId + ": " + base::format(valid.error()));
    }
    return effect;
}

base::Result<CommandResult> MutationExecutor::applyCommand(const CommandEnvelope &envelope) {
    if (!envelope.requestId.isNull()) {
        if (const auto it = remembered_.find(envelope.requestId); it != remembered_.end()) {
            CommandResult replay = it->second;
            replay.replayed = true;
            return replay;
        }
    }
    if (!envelope.documentId.isNull() && envelope.documentId != documentId_) {
        return base::fail(ErrorCategory::Validation, "command.wrong_document",
                          "The command targets another document", envelope.documentId.toString());
    }
    const std::shared_ptr<const document::Revision> head = chain_[cursor_];
    if (envelope.baseRevision != 0 && envelope.baseRevision != head->sequence) {
        return base::fail(ErrorCategory::Conflict, "command.stale_base",
                          "The document changed since this command was prepared",
                          "base " + std::to_string(envelope.baseRevision) + ", current " +
                              std::to_string(head->sequence));
    }

    // Inside an open transaction: update the provisional state only.
    if (!envelope.transactionId.isNull()) {
        if (const auto it = open_.find(envelope.transactionId); it != open_.end()) {
            OpenTransaction &tx = it->second;
            auto effect = applyDefinition(envelope, *tx.state);
            if (!effect) {
                return base::unexpected(std::move(effect).error());
            }
            tx.state = std::make_shared<const document::DocumentState>(std::move(effect->state));
            tx.provenance.push_back(envelope.toJson());
            tx.lastResult = effect->result;
            {
                const std::lock_guard lock(snapshotMutex_);
                provisionalView_[envelope.transactionId] = tx.state;
            }
            CommandResult result = resultFor(*head);
            result.provisional = true;
            result.result = effect->result;
            remember(envelope, result);
            return result;
        }
    }

    auto effect = applyDefinition(envelope, *head->state);
    if (!effect) {
        return base::unexpected(std::move(effect).error());
    }
    const std::string label = envelope.transactionLabel.empty() ? effect->label : envelope.transactionLabel;
    const bool coalesce = !envelope.transactionId.isNull() && cursor_ == chain_.size() - 1 && cursor_ > 0 &&
                          head->transaction == envelope.transactionId;
    std::shared_ptr<const document::Revision> revision;
    if (coalesce) {
        nlohmann::json provenance = head->provenance;
        provenance.push_back(envelope.toJson());
        const document::Revision &parent = *chain_[cursor_ - 1];
        revision = makeRevision(&parent, std::move(effect->state), label, envelope.transactionId,
                                std::move(provenance));
        chain_.back() = revision;
    } else {
        chain_.resize(cursor_ + 1); // a new command discards the redo branch (linear history, D-15)
        nlohmann::json provenance = nlohmann::json::array({envelope.toJson()});
        revision = makeRevision(head.get(), std::move(effect->state), label, envelope.transactionId,
                                std::move(provenance));
        chain_.push_back(revision);
        cursor_ = chain_.size() - 1;
    }
    publish(revision);
    CommandResult result = resultFor(*revision);
    result.result = effect->result;
    result.coalesced = coalesce;
    remember(envelope, result);
    return result;
}

base::Result<CommandResult> MutationExecutor::undo() {
    return runOnExecutor([this]() -> base::Result<CommandResult> {
        if (cursor_ == 0) {
            return base::fail(ErrorCategory::Validation, "history.nothing_to_undo",
                              "There is nothing to undo");
        }
        const std::string label = chain_[cursor_]->label;
        --cursor_;
        publish(chain_[cursor_]);
        CommandResult result = resultFor(*chain_[cursor_]);
        result.label = label;
        return result;
    });
}

base::Result<CommandResult> MutationExecutor::redo() {
    return runOnExecutor([this]() -> base::Result<CommandResult> {
        if (cursor_ + 1 >= chain_.size()) {
            return base::fail(ErrorCategory::Validation, "history.nothing_to_redo",
                              "There is nothing to redo");
        }
        ++cursor_;
        publish(chain_[cursor_]);
        return resultFor(*chain_[cursor_]);
    });
}

bool MutationExecutor::canUndo() const {
    const std::lock_guard lock(snapshotMutex_);
    return !historyView_.empty() && !historyView_.front().current;
}

bool MutationExecutor::canRedo() const {
    const std::lock_guard lock(snapshotMutex_);
    return !historyView_.empty() && !historyView_.back().current;
}

std::vector<HistoryEntry> MutationExecutor::history() const {
    const std::lock_guard lock(snapshotMutex_);
    return historyView_;
}

base::Result<base::TransactionId> MutationExecutor::beginTransaction(std::string label) {
    return runOnExecutor([this, label = std::move(label)]() -> base::Result<base::TransactionId> {
        const base::TransactionId id = base::TransactionId::generate();
        OpenTransaction tx;
        tx.label = label;
        tx.base = chain_[cursor_];
        tx.state = tx.base->state;
        open_.emplace(id, std::move(tx));
        const std::lock_guard lock(snapshotMutex_);
        provisionalView_[id] = chain_[cursor_]->state;
        return id;
    });
}

base::Result<CommandResult> MutationExecutor::commitTransaction(base::TransactionId transaction) {
    return runOnExecutor([this, transaction]() -> base::Result<CommandResult> {
        const auto it = open_.find(transaction);
        if (it == open_.end()) {
            return base::fail(ErrorCategory::Validation, "transaction.not_open",
                              "The transaction is not open", transaction.toString());
        }
        OpenTransaction tx = std::move(it->second);
        open_.erase(it);
        {
            const std::lock_guard lock(snapshotMutex_);
            provisionalView_.erase(transaction);
        }
        const std::shared_ptr<const document::Revision> head = chain_[cursor_];
        if (tx.provenance.empty()) {
            return resultFor(*head); // nothing was applied
        }
        document::DocumentState finalState = *tx.state;
        if (head != tx.base) {
            // Other commits landed meanwhile: replay the transaction's commands on the current state.
            document::DocumentState replay = *head->state;
            for (const nlohmann::json &recorded : tx.provenance) {
                auto envelope = CommandEnvelope::fromJson(recorded);
                if (!envelope) {
                    return base::unexpected(std::move(envelope).error());
                }
                auto effect = applyDefinition(*envelope, replay);
                if (!effect) {
                    return base::fail(ErrorCategory::Conflict, "transaction.conflict",
                                      "The document changed in a way that conflicts with this operation",
                                      base::format(effect.error()));
                }
                replay = std::move(effect->state);
            }
            finalState = std::move(replay);
        }
        chain_.resize(cursor_ + 1);
        auto revision =
            makeRevision(head.get(), std::move(finalState), tx.label, transaction, std::move(tx.provenance));
        chain_.push_back(revision);
        cursor_ = chain_.size() - 1;
        publish(revision);
        CommandResult result = resultFor(*revision);
        result.result = tx.lastResult;
        return result;
    });
}

base::Status MutationExecutor::cancelTransaction(base::TransactionId transaction) {
    return runOnExecutor([this, transaction]() -> base::Status {
        if (open_.erase(transaction) == 0) {
            return base::fail(ErrorCategory::Validation, "transaction.not_open",
                              "The transaction is not open", transaction.toString());
        }
        const std::lock_guard lock(snapshotMutex_);
        provisionalView_.erase(transaction);
        return {};
    });
}

std::shared_ptr<const document::DocumentState>
MutationExecutor::transactionState(base::TransactionId transaction) const {
    const std::lock_guard lock(snapshotMutex_);
    const auto it = provisionalView_.find(transaction);
    return it == provisionalView_.end() ? nullptr : it->second;
}

std::shared_ptr<const document::Revision> MutationExecutor::current() const {
    const std::lock_guard lock(snapshotMutex_);
    return current_;
}

void MutationExecutor::markSaved(std::uint64_t revision) {
    const std::lock_guard lock(snapshotMutex_);
    saved_ = revision;
}

bool MutationExecutor::isModified() const {
    const std::lock_guard lock(snapshotMutex_);
    return current_->sequence != saved_;
}

std::uint64_t MutationExecutor::savedRevision() const {
    const std::lock_guard lock(snapshotMutex_);
    return saved_;
}

std::uint64_t MutationExecutor::subscribe(Observer observer) {
    const std::lock_guard lock(snapshotMutex_);
    const std::uint64_t id = nextObserver_++;
    observers_.emplace(id, std::move(observer));
    return id;
}

void MutationExecutor::unsubscribe(std::uint64_t subscription) {
    const std::lock_guard lock(snapshotMutex_);
    observers_.erase(subscription);
}

} // namespace archmage::commands
