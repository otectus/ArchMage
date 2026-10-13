#include "archmage/native/Blob.h"

#include "archmage/base/Sha256.h"

#include <zstd.h>

namespace archmage::native {
namespace {

using base::ErrorCategory;

base::Unexpected<base::Error> damaged(const Blob &blob, std::string detail) {
    return base::fail(ErrorCategory::DamagedInput, "blob.damaged", "Part of the document file is damaged",
                      blob.hash.substr(0, 16) + ": " + std::move(detail));
}

} // namespace

Blob encodeBlob(std::span<const std::byte> raw, std::string mediaType) {
    Blob blob;
    blob.hash = base::Sha256::of(raw).hex();
    blob.mediaType = std::move(mediaType);
    blob.rawLength = raw.size();
    std::vector<std::byte> compressed(ZSTD_compressBound(raw.size()));
    const std::size_t size = ZSTD_compress(compressed.data(), compressed.size(), raw.data(), raw.size(), 3);
    if (!ZSTD_isError(size) && size < raw.size()) {
        compressed.resize(size);
        blob.codec = "zstd";
        blob.codecVersion = static_cast<int>(ZSTD_versionNumber());
        blob.data = std::move(compressed);
    } else {
        blob.codec = "raw";
        blob.data.assign(raw.begin(), raw.end());
    }
    return blob;
}

base::Result<std::vector<std::byte>> decodeBlob(const Blob &blob) {
    if (blob.rawLength > kMaxBlobBytes) {
        return damaged(blob, "declared length " + std::to_string(blob.rawLength) + " exceeds the limit");
    }
    std::vector<std::byte> raw;
    if (blob.codec == "raw") {
        raw = blob.data;
    } else if (blob.codec == "zstd") {
        const unsigned long long frame = ZSTD_getFrameContentSize(blob.data.data(), blob.data.size());
        if (frame == ZSTD_CONTENTSIZE_ERROR || frame == ZSTD_CONTENTSIZE_UNKNOWN || frame != blob.rawLength) {
            return damaged(blob, "compressed frame does not declare the stored length");
        }
        raw.resize(blob.rawLength);
        const std::size_t size = ZSTD_decompress(raw.data(), raw.size(), blob.data.data(), blob.data.size());
        if (ZSTD_isError(size)) {
            return damaged(blob, ZSTD_getErrorName(size));
        }
        raw.resize(size);
    } else {
        return base::fail(ErrorCategory::UnsupportedInput, "blob.codec", "The document uses an unknown compression",
                          blob.codec);
    }
    if (raw.size() != blob.rawLength) {
        return damaged(blob, "decoded " + std::to_string(raw.size()) + " bytes, expected " + std::to_string(blob.rawLength));
    }
    if (base::Sha256::of(raw).hex() != blob.hash) {
        return damaged(blob, "content hash mismatch");
    }
    return raw;
}

} // namespace archmage::native
