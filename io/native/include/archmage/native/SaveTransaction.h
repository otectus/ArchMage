// The save transaction (spec §10.3, plan S1-4.5, S1-4.6).
//
//   1. The caller captures an immutable committed revision.
//   2. The revision is written into "<name>.tmp-<random>" beside the target, created exclusively, in one
//      SQLite transaction with no journal or WAL (ADR-0103: a direct write is the validated equivalent of
//      the backup API while no recovery database exists).
//   3. The temporary database is closed; no -journal, -wal or -shm file may remain.
//   4. The temporary file is validated (ValidationLevel::Quick) and its identity checked.
//   5. fsync of the file, rename(2) over the target, fsync of the directory.
//   6. The caller marks exactly the saved revision saved (MutationExecutor::markSaved(outcome.sequence));
//      edits committed during the save leave the document modified.
//   7. The previous file stays untouched until the rename replaces it. On filesystems without a durable
//      atomic rename (vfat, exfat, network and FUSE filesystems) the previous file is first renamed to
//      "<name>.bak" and kept, and the outcome reports the fallback.
//
// On failure the temporary file is removed and the target is unchanged, except in the fallback after the
// previous file moved to its backup, which the error then names.
//
// Thread safety: saveDocument may run on any thread; it reads only the immutable revision it was given.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Id.h"
#include "archmage/document/Document.h"
#include "archmage/native/NativeFile.h"
#include "archmage/native/Platform.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace archmage::native {

enum class SaveStep { WriteTemporary, CloseTemporary, Validate, SyncTemporary, BackupPrevious, Replace, SyncDirectory };
[[nodiscard]] std::string_view saveStepName(SaveStep step);

/// A file's document identity (spec §6.2): a normal save keeps it; Save As starts a new one that records
/// where it came from, so links and collaboration can tell the copies apart.
struct FileIdentity {
    base::DocumentId id;          // the manifest's document_id
    base::DocumentId derivedFrom; // the identity a Save As copied; null otherwise
};

/// Save: the file's identity, or the document's own ID when it has never been saved.
[[nodiscard]] FileIdentity identityForSave(const document::DocumentState &state,
                                           const std::optional<FileIdentity> &current);
/// Save As: a new identity derived from the current file's; a never-saved document keeps its own ID.
[[nodiscard]] FileIdentity identityForSaveAs(const document::DocumentState &state,
                                             const std::optional<FileIdentity> &current);

struct SaveRequest {
    std::filesystem::path target;
    std::shared_ptr<const document::Revision> revision; // step 1: captured by the caller
    FileIdentity identity;
    std::int64_t previewMaxSize = 512;
    /// Replaces the detected filesystem facts, e.g. to force the backup fallback on a filesystem whose
    /// rename the user does not trust. Tests use it to exercise the fallback without a vfat mount.
    std::optional<FilesystemInfo> filesystem;
    /// Called before each step; an error fails the save there. Tests use it now; fault injection (D-27)
    /// drives it from S1-5.
    std::function<base::Status(SaveStep)> hook;
};

struct SaveOutcome {
    std::filesystem::path path; // the target, symlinks resolved
    base::RevisionId revision;
    std::uint64_t sequence = 0; // for MutationExecutor::markSaved
    FileIdentity identity;
    FilesystemInfo filesystem;
    bool atomicReplace = true;    // false: the backup fallback ran
    std::filesystem::path backup; // the previous file kept by the fallback; empty otherwise
    std::uint64_t bytes = 0;
    WriteReport report;
    double milliseconds = 0.0;
};

[[nodiscard]] base::Result<SaveOutcome> saveDocument(const SaveRequest &request);

/// Temporary files that interrupted saves left beside `target`.
[[nodiscard]] std::vector<std::filesystem::path> strayTemporaries(const std::filesystem::path &target);

} // namespace archmage::native
