// The recovery workspace (D-14, ADR-0014; spec §10.2, §10.4; plan S1-5.2 to S1-5.4).
//
// Layout: <root>/sessions/<session-id>/session.lock, plus one "<document-id>.recovery" journal per tracked
// document. <root> is $XDG_STATE_HOME/archmage, else ~/.local/state/archmage, else /var/tmp/archmage-<uid>:
// the first on a local filesystem, because SQLite's write-ahead log must not live on a network share.
//
// A journal is an .archmage database (schema 0.1) in WAL mode with synchronous=FULL, holding the document's
// latest journaled revision. The session's thread writes each tracked document's current revision every
// two seconds while it is newer than the journal, encoding only tiles that changed. When the journal
// transaction commits, that revision is journaled: it survives a crash or power loss (spec §10.4, "the
// last acknowledged durable revision"). Journaled dense tiles are registered with TileSpool, so under RAM
// pressure their samples can leave memory and are read back from the journal.
//
// A running session holds an exclusive flock(2) on its session.lock. Discovery skips locked sessions and
// identifies every other journal by its manifest, never by its file name. Journals stay until the document
// is untracked with discard (after a save or a deliberate close) or the candidate is discarded.
//
// Thread safety: RecoverySession and DocumentJournal are safe to use from any thread. Untrack a journal
// before destroying the MutationExecutor it observes.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Id.h"
#include "archmage/commands/MutationExecutor.h"
#include "archmage/native/NativeFile.h"
#include "archmage/native/Platform.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace archmage::native {

/// Spec §10.4 durability status of a revision.
enum class Durability : std::uint8_t {
    InMemory,  // only in this process's memory
    Journaled, // committed to the recovery journal
    Saved,     // written to its file by a completed save
};
[[nodiscard]] std::string_view durabilityName(Durability durability);

struct RecoveryRoot {
    std::filesystem::path path;
    FilesystemInfo filesystem;
    std::vector<std::string> refused; // candidates skipped, with the reason
};

/// The first usable root (created with mode 0700). Fails when no candidate is on a local filesystem.
[[nodiscard]] base::Result<RecoveryRoot> locateRecoveryRoot();

class DocumentJournal;

class RecoverySession {
public:
    struct Options {
        std::chrono::milliseconds cadence{2000};          // spec §10.4: journal on a two-second cadence
        std::chrono::milliseconds previewInterval{30000}; // thumbnails are refreshed at most this often
        std::int64_t previewMaxSize = 256;                // 0: no thumbnails
        int compactEvery = 8;                             // journal writes between compactions
        bool runThread = true;                            // false: journal only on flush() (tests)
    };

    /// Creates <root>/sessions/<new session id>/, locks it and starts the journal thread.
    [[nodiscard]] static base::Result<std::unique_ptr<RecoverySession>> start(const std::filesystem::path &root,
                                                                             Options options);
    /// Stops the thread. Journals of documents still tracked stay on disk for recovery: only an untrack
    /// with discard deletes a journal. The session directory goes once it holds no journal.
    ~RecoverySession();
    RecoverySession(const RecoverySession &) = delete;
    RecoverySession &operator=(const RecoverySession &) = delete;

    [[nodiscard]] base::SessionId id() const { return id_; }
    [[nodiscard]] const std::filesystem::path &directory() const { return directory_; }
    [[nodiscard]] const Options &options() const { return options_; }

    /// Starts journaling the document `executor` holds. `sourcePath` is the file it was opened from, if any.
    [[nodiscard]] base::Result<std::shared_ptr<DocumentJournal>> track(commands::MutationExecutor &executor,
                                                                       std::filesystem::path sourcePath = {});
    /// Stops journaling. With `discard` the journal is deleted, after reading back any of its tiles that
    /// are still alive but evicted.
    [[nodiscard]] base::Status untrack(const std::shared_ptr<DocumentJournal> &journal, bool discard);
    /// Journals every tracked document whose current revision is newer than its journal, now.
    [[nodiscard]] base::Status flush();

    struct State;

private:
    RecoverySession(base::SessionId id, std::filesystem::path directory, Options options, int lockFd);
    void run(std::stop_token stop);

    base::SessionId id_;
    std::filesystem::path directory_;
    Options options_;
    std::unique_ptr<State> state_;
};

class DocumentJournal {
public:
    struct Stats {
        std::uint64_t writes = 0;
        std::uint64_t tilesWritten = 0;
        std::uint64_t compactions = 0;
        double lastWriteMs = 0.0;
    };

    ~DocumentJournal();
    DocumentJournal(const DocumentJournal &) = delete;
    DocumentJournal &operator=(const DocumentJournal &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }
    [[nodiscard]] base::DocumentId documentId() const { return documentId_; }
    [[nodiscard]] Durability durability(const base::RevisionId &revision) const;
    /// Durability of the executor's current revision.
    [[nodiscard]] Durability currentDurability() const;
    [[nodiscard]] std::optional<base::RevisionId> journaledRevision() const;
    /// The most recent journaling failure; cleared by the next successful write.
    [[nodiscard]] base::Status lastError() const;
    [[nodiscard]] Stats stats() const;

    /// Records a completed save of `revision` to `path` (spec §10.3 step 6); the path becomes the source.
    [[nodiscard]] base::Status markSaved(const document::Revision &revision, const std::filesystem::path &path);
    /// Writes the current revision now if the journal does not hold it yet.
    [[nodiscard]] base::Status flush();
    /// Drops blobs nothing references any more, reclaims their pages and checkpoints the log now.
    [[nodiscard]] base::Status compact();

    struct Impl;

private:
    friend class RecoverySession;
    DocumentJournal(std::filesystem::path path, base::DocumentId documentId, const RecoverySession &session);

    std::filesystem::path path_;
    base::DocumentId documentId_;
    std::unique_ptr<Impl> impl_;
};

struct RecoveryCandidate {
    base::DocumentId documentId;
    base::SessionId sessionId;
    std::string title;
    std::string journaledAt; // UTC, from the journal
    std::filesystem::path journalPath;
    std::filesystem::path sourcePath; // the file the document was opened from or last saved to; may be empty
    std::string revisionId;
    std::uint64_t revisionSequence = 0;
    std::string revisionLabel;
    std::string savedRevisionId; // the revision last saved, if any
    bool upToDate = false;       // the journaled revision is the saved one: recovering adds nothing
    std::optional<Preview> thumbnail;
    std::optional<base::Error> problem; // set when the journal could not be read
};

/// Journals of sessions that are no longer running, identified by their manifests.
[[nodiscard]] base::Result<std::vector<RecoveryCandidate>> listRecoveryCandidates(const std::filesystem::path &root);
/// Loads a candidate as a new, unsaved document. Neither the journal nor the original file is changed.
[[nodiscard]] base::Result<OpenedDocument> recoverDocument(const RecoveryCandidate &candidate);
/// Deletes a candidate's journal, and its session directory once no journal remains.
[[nodiscard]] base::Status discardRecovery(const RecoveryCandidate &candidate);

} // namespace archmage::native
