#include "archmage/native/Platform.h"

#include <cerrno>
#include <cstring>

#if defined(__linux__)
#include <fcntl.h>
#include <sys/statfs.h>
#include <unistd.h>
#endif

namespace archmage::native {
namespace {

using base::ErrorCategory;

base::Unexpected<base::Error> ioFailure(std::string_view what, const std::filesystem::path &path, int error) {
    ErrorCategory category = ErrorCategory::Internal;
    std::string message = "A file operation failed";
    if (error == ENOSPC || error == EDQUOT || error == EFBIG) {
        category = ErrorCategory::ResourceExhausted;
        message = "There is not enough disk space";
    } else if (error == EACCES || error == EPERM || error == EROFS) {
        category = ErrorCategory::PermissionDenied;
        message = "The file cannot be written here";
    }
    return base::fail(category, "io." + std::string(what), message, path.string() + ": " + std::strerror(error));
}

} // namespace

#if defined(__linux__)

FilesystemInfo filesystemInfo(const std::filesystem::path &path) {
    struct statfs info {};
    std::filesystem::path probe = path;
    while (statfs(probe.c_str(), &info) != 0 && probe.has_parent_path() && probe != probe.parent_path()) {
        probe = probe.parent_path(); // the file may not exist yet: ask its directory
    }
    switch (static_cast<unsigned long>(info.f_type)) {
    case 0xEF53:
        return {"ext4", true, true};
    case 0x9123683E:
        return {"btrfs", true, true};
    case 0x58465342:
        return {"xfs", true, true};
    case 0x01021994:
        return {"tmpfs", true, true};
    case 0xF2F52010:
        return {"f2fs", true, true};
    case 0x2FC12FC1:
        return {"zfs", true, true};
    case 0xCA451A4E:
        return {"bcachefs", true, true};
    case 0x794C7630:
        return {"overlayfs", true, true};
    case 0x3153464A:
        return {"jfs", true, true};
    case 0x3434:
        return {"nilfs2", true, true};
    case 0x858458F6:
        return {"ramfs", true, true};
    case 0x6969:
        return {"nfs", false, false};
    case 0x517B:
        return {"smb", false, false};
    case 0xFF534D42:
        return {"cifs", false, false};
    case 0xFE534D42:
        return {"smb2", false, false};
    case 0x65735546:
        return {"fuse", false, false};
    case 0x01021997:
        return {"9p", false, false};
    case 0x4D44:
        return {"vfat", true, false}; // no journal: a crash can lose a rename
    case 0x2011BAB0:
        return {"exfat", true, false};
    default:
        return {"unknown", true, false}; // unverified: keep the previous file (spec §10.3 fallback)
    }
}

base::Status syncFile(const std::filesystem::path &path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return ioFailure("open", path, errno);
    }
    const int result = ::fsync(fd);
    const int error = errno;
    ::close(fd);
    if (result != 0) {
        return ioFailure("fsync", path, error);
    }
    return {};
}

base::Status syncDirectory(const std::filesystem::path &directory) {
    const int fd = ::open(directory.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        return ioFailure("open", directory, errno);
    }
    const int result = ::fsync(fd);
    const int error = errno;
    ::close(fd);
    if (result != 0 && error != EINVAL) { // some filesystems do not support directory fsync
        return ioFailure("fsync", directory, error);
    }
    return {};
}

base::Status createExclusive(const std::filesystem::path &path) {
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0666);
    if (fd < 0) {
        return ioFailure("create", path, errno);
    }
    ::close(fd);
    return {};
}

base::Status replaceFile(const std::filesystem::path &from, const std::filesystem::path &to) {
    if (::rename(from.c_str(), to.c_str()) != 0) {
        return ioFailure("rename", to, errno);
    }
    return {};
}

#else

// Compile-time stubs until the platform is built and tested (D-21): saving reports the gap instead of
// claiming durability it cannot verify.
namespace {
base::Unexpected<base::Error> unsupportedPlatform(const std::filesystem::path &path) {
    return base::fail(ErrorCategory::ProviderUnavailable, "io.platform", "Saving is not available on this platform",
                      path.string());
}
} // namespace

FilesystemInfo filesystemInfo(const std::filesystem::path &) { return {"unknown", true, false}; }
base::Status syncFile(const std::filesystem::path &path) { return unsupportedPlatform(path); }
base::Status syncDirectory(const std::filesystem::path &directory) { return unsupportedPlatform(directory); }
base::Status createExclusive(const std::filesystem::path &path) { return unsupportedPlatform(path); }
base::Status replaceFile(const std::filesystem::path &, const std::filesystem::path &to) {
    return unsupportedPlatform(to);
}

#endif

} // namespace archmage::native
