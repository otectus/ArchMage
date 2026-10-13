// The native .archmage document file (D-13, spec §10.1, §10.5): writing one revision into an empty
// database, reading a file back into a document state, composite previews, and validation for
// `archmage-cli validate` and `inspect`.
//
// docs/architecture/native-format.md describes schema 0.1. In short: typed JSON objects (document,
// layer.raster, raster) linked by ordered relations; tile planes either inline (one uniform sample) or as
// content-addressed Zstandard blobs; one revision row with its provenance; a composite preview. Content
// this version does not understand travels in DocumentState::preserved and is written back on save.
//
// Thread safety: functions are reentrant. A Database belongs to the calling thread.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Id.h"
#include "archmage/document/Document.h"
#include "archmage/native/Sqlite.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace archmage::native {

struct WriteOptions {
    base::DocumentId fileIdentity;      // null: the state's own ID
    base::DocumentId derivedFrom;       // set by Save As (spec §6.2)
    std::int64_t previewMaxSize = 512;  // longest preview edge; 0 writes no preview
};

struct WriteReport {
    std::size_t objects = 0;
    std::size_t tilePlanes = 0;
    std::size_t resources = 0;
    std::uint64_t rawBytes = 0;     // resources before compression
    std::uint64_t encodedBytes = 0; // resources as stored
    std::size_t droppedOpaqueRelations = 0; // preserved relations whose endpoint no longer exists
};

/// Writes `revision` into `db`, which must be empty, in one transaction. The database is left open.
[[nodiscard]] base::Result<WriteReport> writeDocument(Database &db, const document::Revision &revision,
                                                      const WriteOptions &options);

/// Straight RGBA8 rows in the document's color space.
struct Preview {
    std::int64_t width = 0;
    std::int64_t height = 0;
    std::vector<std::byte> rgba;
};

/// Composite of `state` no larger than `maxSize` on its longest edge, nearest-sampled at preview pixel
/// centers through the document's render graph. Cost grows with the preview height times the painted
/// width, not with the document area.
[[nodiscard]] base::Result<Preview> renderPreview(const document::DocumentState &state, std::int64_t maxSize);

struct FileInfo {
    int schemaMajor = 0;
    int schemaMinor = 0;
    std::string writer;
    std::string savedAt;
    base::DocumentId documentId;
    base::DocumentId derivedFrom;
    std::string revisionId;
    std::uint64_t revisionSequence = 0;
    std::string revisionLabel;
    std::vector<std::string> capabilities;
    /// Set when this version must not save over the file: a newer schema or an unknown capability
    /// (spec §10.5). `readOnlyReason` is one of "schema.newer_major", "schema.newer_minor",
    /// "capability.unknown"; `readOnlyExplanation` is user-facing text.
    bool readOnly = false;
    std::string readOnlyReason;
    std::string readOnlyExplanation;
};

struct OpenedDocument {
    FileInfo info;
    /// Null when the file cannot be understood (newer major schema, unknown capability): only the preview
    /// is available then.
    std::shared_ptr<const document::DocumentState> state;
    std::optional<Preview> preview;
    std::shared_ptr<const color::ColorProfile> previewProfile; // the preview's color space
};

struct ReadOptions {
    bool loadPreview = true;
};

/// Opens a file: checks identity and schema, migrates older minors in memory (the file is not changed),
/// loads and verifies every referenced tile blob, resolves references and validates the state.
[[nodiscard]] base::Result<OpenedDocument> readDocument(const std::filesystem::path &path,
                                                        const ReadOptions &options = {});
/// Manifest facts only; cheap.
[[nodiscard]] base::Result<FileInfo> readFileInfo(const std::filesystem::path &path);
/// Capabilities this version can open.
[[nodiscard]] const std::vector<std::string> &knownCapabilities();

struct ValidationReport {
    FileInfo info;
    std::vector<base::Error> problems; // empty: valid
    std::vector<std::string> notes;    // limits of what was checked
    std::size_t objects = 0;
    std::size_t relations = 0;
    std::size_t resources = 0;
    std::size_t tilePlanes = 0;
    std::size_t resourcesVerified = 0; // decoded and hash-checked (deep)

    [[nodiscard]] bool valid() const { return problems.empty(); }
};

enum class ValidationLevel {
    Quick,    // identity, manifest, tables, SQLite quick_check, references: the save path (spec §10.3 step 4)
    Standard, // + a full load as Open does, which verifies every tile blob it decodes: `archmage-cli validate`
    Deep,     // + SQLite integrity_check and every stored resource decoded and hash-checked: `validate --deep`
};

/// A file that cannot be examined at all (missing, unreadable) is an error; a file examined and found wrong
/// is a report with problems.
[[nodiscard]] base::Result<ValidationReport> validateFile(const std::filesystem::path &path,
                                                          ValidationLevel level);

/// Manifest, objects, relations, revision and a resource summary as JSON (`archmage-cli inspect`).
[[nodiscard]] base::Result<nlohmann::json> inspectFile(const std::filesystem::path &path);

} // namespace archmage::native
