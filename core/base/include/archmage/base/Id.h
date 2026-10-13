// Typed 128-bit identifiers (D-16, spec §6.1): random from a CSPRNG, written as
// "<prefix>-<26 Crockford base32 characters>", e.g. "layer-01JABCDEFGHJKMNPQRSTVWXYZ0". Names are
// separate fields and never references. The all-zero value is the null identifier.
//
// Thread safety: value types; generate() is safe from any thread.
#pragma once

#include "archmage/base/Error.h"

#include <compare>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace archmage::base {

enum class IdKind : std::uint8_t {
    Document,
    Layer,
    Raster,
    Resource,
    Revision,
    Transaction,
    Request,
    Session,
    Job
};

/// Serialization prefix: doc, layer, raster, res, rev, tx, req, sess, job.
[[nodiscard]] std::string_view idPrefix(IdKind kind);

/// Untyped 128-bit value with the shared encoding helpers.
struct RawId {
    std::uint64_t high = 0;
    std::uint64_t low = 0;

    [[nodiscard]] static RawId random();
    /// The 26 Crockford base32 characters (no prefix).
    [[nodiscard]] std::string encode() const;
    /// Parses 26 characters (case-insensitive); nullopt on bad length, characters or overflow.
    [[nodiscard]] static std::optional<RawId> decode(std::string_view text);
    [[nodiscard]] bool isNull() const { return high == 0 && low == 0; }

    friend constexpr auto operator<=>(const RawId &, const RawId &) = default;
};

template <IdKind K> class Id {
public:
    static constexpr IdKind kind = K;

    constexpr Id() = default;
    [[nodiscard]] static Id generate() { return Id(RawId::random()); }
    [[nodiscard]] static constexpr Id fromRaw(RawId raw) { return Id(raw); }

    [[nodiscard]] static Result<Id> parse(std::string_view text) {
        const std::string_view prefix = idPrefix(K);
        if (text.size() != prefix.size() + 1 + 26 || text.substr(0, prefix.size()) != prefix ||
            text[prefix.size()] != '-') {
            return fail(ErrorCategory::DamagedInput, "id.malformed", "Malformed identifier",
                        std::string("expected ") + std::string(prefix) +
                            "-<26 base32>: " + std::string(text.substr(0, 64)));
        }
        const auto raw = RawId::decode(text.substr(prefix.size() + 1));
        if (!raw) {
            return fail(ErrorCategory::DamagedInput, "id.malformed", "Malformed identifier",
                        std::string(text.substr(0, 64)));
        }
        return Id(*raw);
    }

    [[nodiscard]] std::string toString() const {
        std::string text{idPrefix(K)};
        text += '-';
        text += raw_.encode();
        return text;
    }

    [[nodiscard]] constexpr bool isNull() const { return raw_.isNull(); }
    [[nodiscard]] constexpr RawId raw() const { return raw_; }

    friend constexpr auto operator<=>(const Id &, const Id &) = default;

private:
    constexpr explicit Id(RawId raw) : raw_(raw) {}
    RawId raw_;
};

using DocumentId = Id<IdKind::Document>;
using LayerId = Id<IdKind::Layer>;
using RasterId = Id<IdKind::Raster>;
using ResourceId = Id<IdKind::Resource>;
using RevisionId = Id<IdKind::Revision>;
using TransactionId = Id<IdKind::Transaction>;
using RequestId = Id<IdKind::Request>;
using SessionId = Id<IdKind::Session>;
using JobId = Id<IdKind::Job>;

} // namespace archmage::base

template <> struct std::hash<archmage::base::RawId> {
    std::size_t operator()(const archmage::base::RawId &id) const noexcept {
        return static_cast<std::size_t>(id.high ^ (id.low * 0x9e3779b97f4a7c15ULL));
    }
};

template <archmage::base::IdKind K> struct std::hash<archmage::base::Id<K>> {
    std::size_t operator()(const archmage::base::Id<K> &id) const noexcept {
        return std::hash<archmage::base::RawId>{}(id.raw());
    }
};
