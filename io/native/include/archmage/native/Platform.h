// Filesystem durability primitives for saving (D-21, spec §10.3): POSIX implementations; other platforms
// are compile-time stubs until they are built (ADR-0021).
//
// Thread safety: functions are safe from any thread.
#pragma once

#include "archmage/base/Error.h"

#include <filesystem>
#include <string>

namespace archmage::native {

struct FilesystemInfo {
    std::string type;         // "ext4", "btrfs", "nfs", ...; "unknown" when unrecognized
    bool local = true;        // false for network and FUSE filesystems
    bool atomicRename = true; // rename(2) replaces the target atomically and durably with directory fsync;
                              // false for vfat, exfat, network, FUSE and unrecognized filesystems
};

[[nodiscard]] FilesystemInfo filesystemInfo(const std::filesystem::path &path);
/// fsync(2) of a file's data and metadata.
[[nodiscard]] base::Status syncFile(const std::filesystem::path &path);
/// fsync(2) of a directory, making a rename or creation in it durable.
[[nodiscard]] base::Status syncDirectory(const std::filesystem::path &directory);
/// Creates an empty file, failing if anything exists at `path` (O_CREAT | O_EXCL).
[[nodiscard]] base::Status createExclusive(const std::filesystem::path &path);
/// rename(2): atomically replaces `to` with `from` on the same filesystem.
[[nodiscard]] base::Status replaceFile(const std::filesystem::path &from, const std::filesystem::path &to);

} // namespace archmage::native
