// The mutation executor (D-15, D-24, spec §9.1-§9.3): one thread per document commits every mutation in
// order. It enforces requestId idempotency, rejects commands made against a stale revision, keeps a
// linear undo history with a clean save point, coalesces commands that share an implicit transaction
// (one history entry for a slider drag), and holds explicit transactions whose provisional state only
// the canvas sees until commit (painting previews, spec §6.2).
//
// Undo is exact: history keeps whole immutable states, which share unchanged layers and tiles, so
// restoring a revision restores its pixels bit for bit (spec §9.2).
//
// Thread safety: every public method is safe from any thread. Observers run on the executor thread after
// each change; they may call back into the executor (calls from that thread run inline).
#pragma once

#include "archmage/commands/Command.h"
#include "archmage/document/Document.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <vector>

namespace archmage::commands {

struct CommandResult {
    std::uint64_t revision = 0; // sequence of the revision current afterwards
    base::RevisionId revisionId;
    std::string label;
    nlohmann::json result = nlohmann::json::object();
    bool replayed = false;    // repeated requestId: the stored result was returned, nothing was applied
    bool coalesced = false;   // merged into the previous history entry
    bool provisional = false; // applied inside an open transaction; no revision yet
};

struct HistoryEntry {
    std::uint64_t revision = 0;
    std::string label;
    bool current = false;
};

class MutationExecutor {
public:
    struct Options {
        std::size_t idempotencyWindow = 1024; // most recent successful request IDs remembered
    };
    using Observer = std::function<void(const std::shared_ptr<const document::Revision> &)>;

    /// Revision 1 is `initial`, labelled `initialLabel` ("New Document", "Open", ...).
    MutationExecutor(document::DocumentState initial, std::string initialLabel);
    MutationExecutor(document::DocumentState initial, std::string initialLabel, Options options,
                     const CommandRegistry &registry = CommandRegistry::builtIn());
    ~MutationExecutor();
    MutationExecutor(const MutationExecutor &) = delete;
    MutationExecutor &operator=(const MutationExecutor &) = delete;

    using Precondition = std::function<base::Status(const document::DocumentState &)>;

    std::future<base::Result<CommandResult>> submit(CommandEnvelope envelope);
    base::Result<CommandResult> execute(CommandEnvelope envelope);
    /// Applies `envelope` only if `precondition` holds for the current state; the check and the commit
    /// happen together on the executor thread, so nothing can land in between (spec §9.3 step 2).
    base::Result<CommandResult> executeIf(CommandEnvelope envelope, Precondition precondition);

    base::Result<CommandResult> undo();
    base::Result<CommandResult> redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    /// Oldest first; entry 0 is the initial revision.
    [[nodiscard]] std::vector<HistoryEntry> history() const;

    base::Result<base::TransactionId> beginTransaction(std::string label);
    base::Result<CommandResult> commitTransaction(base::TransactionId transaction);
    base::Status cancelTransaction(base::TransactionId transaction);
    /// The provisional state of an open transaction (null when it is not open).
    [[nodiscard]] std::shared_ptr<const document::DocumentState>
    transactionState(base::TransactionId transaction) const;

    [[nodiscard]] std::shared_ptr<const document::Revision> current() const;
    [[nodiscard]] base::DocumentId documentId() const { return documentId_; }

    /// Marks `revision` as the one last saved; the document is unmodified exactly while it is current.
    void markSaved(std::uint64_t revision);
    [[nodiscard]] bool isModified() const;
    [[nodiscard]] std::uint64_t savedRevision() const;

    std::uint64_t subscribe(Observer observer);
    void unsubscribe(std::uint64_t subscription);

private:
    struct OpenTransaction {
        std::string label;
        std::shared_ptr<const document::Revision> base;
        std::shared_ptr<const document::DocumentState> state;
        nlohmann::json provenance = nlohmann::json::array();
        nlohmann::json lastResult = nlohmann::json::object();
    };

    template <class F> auto runOnExecutor(F &&f) -> decltype(f());
    void loop(std::stop_token stop);

    base::Result<CommandResult> applyCommand(const CommandEnvelope &envelope);
    base::Result<CommandEffect> applyDefinition(const CommandEnvelope &envelope,
                                                const document::DocumentState &state) const;
    std::shared_ptr<const document::Revision> makeRevision(const document::Revision *parent,
                                                           document::DocumentState state, std::string label,
                                                           base::TransactionId transaction,
                                                           nlohmann::json provenance);
    void publish(std::shared_ptr<const document::Revision> revision);
    void remember(const CommandEnvelope &envelope, const CommandResult &result);
    CommandResult resultFor(const document::Revision &revision) const;

    const CommandRegistry &registry_;
    Options options_;
    base::DocumentId documentId_;

    // Executor-thread state.
    std::vector<std::shared_ptr<const document::Revision>> chain_;
    std::size_t cursor_ = 0;
    std::uint64_t nextSequence_ = 1;
    std::map<base::TransactionId, OpenTransaction> open_;
    std::map<base::RequestId, CommandResult> remembered_;
    std::deque<base::RequestId> rememberedOrder_;

    // Shared with readers.
    mutable std::mutex snapshotMutex_;
    std::shared_ptr<const document::Revision> current_;
    std::vector<HistoryEntry> historyView_;
    std::uint64_t saved_ = 1;
    std::map<std::uint64_t, Observer> observers_;
    std::uint64_t nextObserver_ = 1;
    std::map<base::TransactionId, std::shared_ptr<const document::DocumentState>> provisionalView_;

    std::mutex queueMutex_;
    std::condition_variable_any queueReady_;
    std::deque<std::function<void()>> queue_;
    std::thread::id executorThread_;
    std::jthread thread_;
};

} // namespace archmage::commands
