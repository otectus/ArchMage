#include "archmage/base/Sha256.h"

#include <openssl/evp.h>

#include <cstdio>
#include <cstdlib>

namespace archmage::base {
namespace {

[[noreturn]] void opensslFailure(const char *what) {
    std::fprintf(stderr, "archmage: OpenSSL %s failed\n", what);
    std::abort();
}

EVP_MD_CTX *ctx(void *p) { return static_cast<EVP_MD_CTX *>(p); }

} // namespace

std::string Sha256Digest::hex() const {
    static constexpr char kHex[] = "0123456789abcdef";
    std::string text(64, '0');
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        text[2 * i] = kHex[bytes[i] >> 4];
        text[2 * i + 1] = kHex[bytes[i] & 15u];
    }
    return text;
}

std::optional<Sha256Digest> Sha256Digest::fromHex(std::string_view hex) {
    if (hex.size() != 64) {
        return std::nullopt;
    }
    const auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') {
            return c - '0';
        }
        if (c >= 'a' && c <= 'f') {
            return c - 'a' + 10;
        }
        return -1;
    };
    Sha256Digest digest;
    for (std::size_t i = 0; i < 32; ++i) {
        const int a = nibble(hex[2 * i]);
        const int b = nibble(hex[2 * i + 1]);
        if (a < 0 || b < 0) {
            return std::nullopt;
        }
        digest.bytes[i] = static_cast<std::uint8_t>((a << 4) | b);
    }
    return digest;
}

Sha256::Sha256() : context_(EVP_MD_CTX_new()) {
    if (context_ == nullptr || EVP_DigestInit_ex(ctx(context_), EVP_sha256(), nullptr) != 1) {
        opensslFailure("SHA-256 initialization");
    }
}

Sha256::~Sha256() { EVP_MD_CTX_free(ctx(context_)); }

void Sha256::update(std::span<const std::byte> data) {
    if (!data.empty() && EVP_DigestUpdate(ctx(context_), data.data(), data.size()) != 1) {
        opensslFailure("SHA-256 update");
    }
}

void Sha256::update(std::string_view text) { update(std::as_bytes(std::span(text.data(), text.size()))); }

Sha256Digest Sha256::finish() {
    Sha256Digest digest;
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(ctx(context_), digest.bytes.data(), &length) != 1 || length != 32) {
        opensslFailure("SHA-256 finalization");
    }
    return digest;
}

Sha256Digest Sha256::of(std::span<const std::byte> data) {
    Sha256 hasher;
    hasher.update(data);
    return hasher.finish();
}

} // namespace archmage::base
