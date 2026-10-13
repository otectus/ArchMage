#include "archmage/native/SaveTransaction.h"

#include "archmage/base/Random.h"
#include "archmage/native/FaultPoints.h"
#include "archmage/native/Sqlite.h"
#include "Payloads.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <system_error>

namespace archmage::native {
namespace {

using base::ErrorCategory;

constexpr std::string_view kTemporaryMarker = ".tmp-";

std::filesystem::path temporaryBeside(const std::filesystem::path &target) {
    std::array<std::byte, 6> random{};
    base::secureRandomBytes(random);
    return target.parent_path() / (target.filename().string() + std::string(kTemporaryMarker) + hexOf(random));
}

// Prefixes an error with the step it happened in and the file it was saving (spec §26.2: identify the
// affected operation).
base::Error atStep(SaveStep step, const std::filesystem::path &target, base::Error error) {
    error.detail = std::string(saveStepName(step)) + " " + target.string() + ": " + error.detail;
    return error;
}

// Removes the temporary file and its possible SQLite sidecars on failure.
class TemporaryFile {
public:
    explicit TemporaryFile(std::filesystem::path path) : path_(std::move(path)) {}
    TemporaryFile(const TemporaryFile &) = delete;
    TemporaryFile &operator=(const TemporaryFile &) = delete;
    ~TemporaryFile() {
        if (!keep_) {
            std::error_code ignored;
            for (const char *suffix : {"", "-journal", "-wal", "-shm"}) {
                std::filesystem::remove(path_.string() + suffix, ignored);
            }
        }
    }
    [[nodiscard]] const std::filesystem::path &path() const { return path_; }
    void keep() { keep_ = true; }

private:
    std::filesystem::path path_;
    bool keep_ = false;
};

} // namespace

std::string_view saveStepName(SaveStep step) {
    switch (step) {
    case SaveStep::WriteTemporary:
        return "write_temporary";
    case SaveStep::CloseTemporary:
        return "close_temporary";
    case SaveStep::Validate:
        return "validate";
    case SaveStep::SyncTemporary:
        return "sync_temporary";
    case SaveStep::BackupPrevious:
        return "backup_previous";
    case SaveStep::Replace:
        return "replace";
    case SaveStep::SyncDirectory:
        return "sync_directory";
    }
    return "unknown";
}

FileIdentity identityForSave(const document::DocumentState &state, const std::optional<FileIdentity> &current) {
    return current ? *current : FileIdentity{state.id, {}};
}

FileIdentity identityForSaveAs(const document::DocumentState &state, const std::optional<FileIdentity> &current) {
    if (!current) {
        return {state.id, {}};
    }
    return {base::DocumentId::generate(), current->id};
}

base::Result<SaveOutcome> saveDocument(const SaveRequest &request) {
    const auto started = std::chrono::steady_clock::now();
    if (!request.revision || !request.revision->state) {
        return base::fail(ErrorCategory::Internal, "save.no_revision", "There is no document to save");
    }
    if (request.identity.id.isNull()) {
        return base::fail(ErrorCategory::Internal, "save.no_identity", "The document has no identity to save");
    }

    // Save through a symbolic link to the file it names, keeping the link.
    std::error_code ec;
    std::filesystem::path target = std::filesystem::absolute(request.target, ec);
    if (ec) {
        return base::fail(ErrorCategory::PermissionDenied, "save.invalid_path", "The save location is not valid",
                          request.target.string() + ": " + ec.message());
    }
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(target, ec))) {
        target = std::filesystem::weakly_canonical(target, ec);
        if (ec) {
            return base::fail(ErrorCategory::PermissionDenied, "save.invalid_path",
                              "The save location could not be resolved", request.target.string() + ": " + ec.message());
        }
    }
    const auto targetStatus = std::filesystem::status(target, ec);
    const bool replacing = std::filesystem::exists(targetStatus);
    if (replacing && !std::filesystem::is_regular_file(targetStatus)) {
        return base::fail(ErrorCategory::Validation, "save.target_not_file", "The save location is not a file",
                          target.string());
    }
    const std::filesystem::path directory = target.parent_path();
    if (!std::filesystem::is_directory(directory, ec)) {
        return base::fail(ErrorCategory::PermissionDenied, "save.no_directory", "The folder does not exist",
                          directory.string());
    }

    SaveOutcome outcome;
    outcome.path = target;
    outcome.revision = request.revision->id;
    outcome.sequence = request.revision->sequence;
    outcome.identity = request.identity;
    outcome.filesystem = request.filesystem ? *request.filesystem : filesystemInfo(directory);
    outcome.atomicReplace = outcome.filesystem.atomicRename;

    // Before each step: the caller's hook, then the step's fault point (D-27).
    const auto step = [&](SaveStep which) -> base::Status {
        if (request.hook) {
            if (auto status = request.hook(which); !status) {
                return base::unexpected(atStep(which, target, status.error()));
            }
        }
        std::optional<FaultPoint> point;
        switch (which) {
        case SaveStep::BackupPrevious:
            point = FaultPoint::Backup;
            break;
        case SaveStep::Replace:
            point = FaultPoint::Rename;
            break;
        case SaveStep::SyncDirectory:
            point = FaultPoint::DirSync;
            break;
        default:
            break;
        }
        if (point) {
            if (auto fault = faultPoint(*point); !fault) {
                return base::unexpected(atStep(which, target, fault.error()));
            }
        }
        return {};
    };
    const auto failed = [&](SaveStep which, base::Status status) {
        return base::Unexpected<base::Error>(atStep(which, target, std::move(status).error()));
    };

    // Step 2: materialize the revision beside the target.
    TemporaryFile temporary(temporaryBeside(target));
    if (auto status = step(SaveStep::WriteTemporary); !status) {
        return base::unexpected(status.error());
    }
    if (auto status = createExclusive(temporary.path()); !status) {
        return failed(SaveStep::WriteTemporary, status);
    }
    auto db = Database::open(temporary.path(), Database::Mode::ReadWriteCreate);
    if (!db) {
        return base::unexpected(atStep(SaveStep::WriteTemporary, target, db.error()));
    }
    auto written = writeDocument(*db, *request.revision,
                                 WriteOptions{request.identity.id, request.identity.derivedFrom, request.previewMaxSize});
    if (!written) {
        return base::unexpected(atStep(SaveStep::WriteTemporary, target, written.error()));
    }
    outcome.report = *written;

    // Step 3: a closed, standalone file.
    if (auto status = step(SaveStep::CloseTemporary); !status) {
        return base::unexpected(status.error());
    }
    if (auto status = db->close(); !status) {
        return failed(SaveStep::CloseTemporary, status);
    }
    for (const char *suffix : {"-journal", "-wal", "-shm"}) {
        if (std::filesystem::exists(temporary.path().string() + suffix, ec)) {
            return base::fail(ErrorCategory::Internal, "save.sidecar", "The saved file is not self-contained",
                              std::string("close_temporary: ") + suffix + " remained");
        }
    }

    // Step 4: what was written reads back as this revision of this document.
    if (auto status = step(SaveStep::Validate); !status) {
        return base::unexpected(status.error());
    }
    auto validation = validateFile(temporary.path(), ValidationLevel::Quick);
    if (!validation) {
        return base::unexpected(atStep(SaveStep::Validate, target, validation.error()));
    }
    if (!validation->valid()) {
        return base::unexpected(atStep(SaveStep::Validate, target, validation->problems.front()));
    }
    if (validation->info.documentId != request.identity.id ||
        validation->info.revisionId != request.revision->id.toString()) {
        return base::fail(ErrorCategory::Internal, "save.identity_mismatch", "The saved file did not read back",
                          "validate: identity or revision differs");
    }
    outcome.bytes = std::filesystem::file_size(temporary.path(), ec);
    if (replacing) {
        // Keep the previous file's permission bits; ownership stays with whoever saves.
        std::filesystem::permissions(temporary.path(), std::filesystem::status(target, ec).permissions(), ec);
    }

    // Step 5: durable data, then the replacement, then the directory entry.
    if (auto status = step(SaveStep::SyncTemporary); !status) {
        return base::unexpected(status.error());
    }
    if (auto status = syncFile(temporary.path()); !status) {
        return failed(SaveStep::SyncTemporary, status);
    }
    if (!outcome.atomicReplace && replacing) {
        // Step 7 fallback: the previous good file survives as <name>.bak whatever happens next.
        if (auto status = step(SaveStep::BackupPrevious); !status) {
            return base::unexpected(status.error());
        }
        outcome.backup = target.string() + ".bak";
        if (auto status = replaceFile(target, outcome.backup); !status) {
            return failed(SaveStep::BackupPrevious, status);
        }
    }
    if (auto status = step(SaveStep::Replace); !status) {
        return base::unexpected(status.error());
    }
    if (auto status = replaceFile(temporary.path(), target); !status) {
        base::Error error = atStep(SaveStep::Replace, target, status.error());
        if (!outcome.backup.empty()) {
            error.detail += "; the previous file is kept at " + outcome.backup.string();
        }
        return base::unexpected(std::move(error));
    }
    temporary.keep(); // it is the target now
    if (auto status = step(SaveStep::SyncDirectory); !status) {
        return base::unexpected(status.error());
    }
    if (auto status = syncDirectory(directory); !status) {
        // The new file is in place but its directory entry may not be durable yet: report, do not retry.
        return failed(SaveStep::SyncDirectory, status);
    }
    outcome.milliseconds =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return outcome;
}

std::vector<std::filesystem::path> strayTemporaries(const std::filesystem::path &target) {
    std::vector<std::filesystem::path> found;
    std::error_code ec;
    const std::string prefix = target.filename().string() + std::string(kTemporaryMarker);
    for (const auto &entry : std::filesystem::directory_iterator(target.parent_path(), ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() == prefix.size() + 12 && name.starts_with(prefix)) {
            found.push_back(entry.path());
        }
    }
    std::sort(found.begin(), found.end());
    return found;
}

} // namespace archmage::native
