// SHA-256 content hashing (D-19, spec §5.2): the persisted identity of tiles and resources.
// Thread safety: a Sha256 instance belongs to one thread; the static helper is safe from any thread.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace archmage::base {

struct Sha256Digest {
    std::array<std::uint8_t, 32> bytes{};

    /// 64 lowercase hex characters.
    [[nodiscard]] std::string hex() const;
    [[nodiscard]] static std::optional<Sha256Digest> fromHex(std::string_view hex);

    friend bool operator==(const Sha256Digest &, const Sha256Digest &) = default;
    friend auto operator<=>(const Sha256Digest &, const Sha256Digest &) = default;
};

class Sha256 {
public:
    Sha256();
    ~Sha256();
    Sha256(const Sha256 &) = delete;
    Sha256 &operator=(const Sha256 &) = delete;

    void update(std::span<const std::byte> data);
    void update(std::string_view text);
    /// Finishes the hash; the instance must not be updated afterwards.
    [[nodiscard]] Sha256Digest finish();

    [[nodiscard]] static Sha256Digest of(std::span<const std::byte> data);

private:
    void *context_; // EVP_MD_CTX, kept opaque so OpenSSL headers stay out of public includes
};

} // namespace archmage::base
