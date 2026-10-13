// Content-addressed blobs (D-19, spec §5.2): SHA-256 of the raw bytes identifies a blob; Zstandard
// compresses it when that saves space. Reading verifies the codec, the decoded length and the hash, so a
// tampered or truncated blob is reported as damaged input, never returned as wrong pixels.
//
// Thread safety: pure functions.
#pragma once

#include "archmage/base/Error.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace archmage::native {

/// Largest decoded blob a reader accepts (well above a 128² × 4 × float32 tile plane or an ICC profile).
inline constexpr std::uint64_t kMaxBlobBytes = 256ull << 20;

struct Blob {
    std::string hash;      // SHA-256 hex of the raw bytes
    std::string mediaType;
    std::string codec;     // "zstd" or "raw"
    int codecVersion = 0;  // ZSTD_versionNumber() of the writer; 0 for raw
    std::uint64_t rawLength = 0;
    std::vector<std::byte> data; // encoded bytes
};

/// Encodes `raw`: Zstandard level 3 when it is smaller, otherwise stored raw.
[[nodiscard]] Blob encodeBlob(std::span<const std::byte> raw, std::string mediaType);
/// Decodes and verifies a stored blob.
[[nodiscard]] base::Result<std::vector<std::byte>> decodeBlob(const Blob &blob);

} // namespace archmage::native
