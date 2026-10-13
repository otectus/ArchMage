// Reader steps shared by readDocument and the recovery workspace (io/native internal).
#pragma once

#include "archmage/native/NativeFile.h"
#include "archmage/native/Sqlite.h"

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace archmage::native::detail {

/// An identified file: the database, a migrated in-memory copy for an older minor, and the manifest facts.
struct OpenFile {
    Database file;
    std::optional<Database> migrated;
    FileInfo info;

    Database &source() { return migrated ? *migrated : file; }
};

/// Opens an existing file and checks identity, manifest, tables and revision; migrates an older minor in
/// memory. Recovery journals are opened read-write so that SQLite can recover their write-ahead log.
[[nodiscard]] base::Result<OpenFile> openAndIdentify(const std::filesystem::path &path, Database::Mode mode);
/// Loads the document and preview of an identified file.
[[nodiscard]] base::Result<OpenedDocument> readOpen(OpenFile &open, const ReadOptions &options);
[[nodiscard]] base::Result<std::vector<std::pair<std::string, std::string>>> loadManifest(Database &db);
/// The stored composite preview and its color space, if present and intact.
[[nodiscard]] OpenedDocument readPreview(OpenFile &open);

} // namespace archmage::native::detail
