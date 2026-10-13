#include "archmage/base/Id.h"

#include "archmage/base/Random.h"

#include <array>
#include <cstring>

namespace archmage::base {
namespace {

constexpr std::string_view kAlphabet = "0123456789ABCDEFGHJKMNPQRSTVWXYZ";

int digitValue(char c) {
    if (c >= 'a' && c <= 'z') {
        c = static_cast<char>(c - 'a' + 'A');
    }
    const std::size_t index = kAlphabet.find(c);
    return index == std::string_view::npos ? -1 : static_cast<int>(index);
}

} // namespace

std::string_view idPrefix(IdKind kind) {
    switch (kind) {
    case IdKind::Document:
        return "doc";
    case IdKind::Layer:
        return "layer";
    case IdKind::Raster:
        return "raster";
    case IdKind::Resource:
        return "res";
    case IdKind::Revision:
        return "rev";
    case IdKind::Transaction:
        return "tx";
    case IdKind::Request:
        return "req";
    case IdKind::Session:
        return "sess";
    case IdKind::Job:
        return "job";
    }
    return "id";
}

RawId RawId::random() {
    for (;;) {
        std::array<std::byte, 16> bytes{};
        secureRandomBytes(bytes);
        RawId id;
        std::memcpy(&id.high, bytes.data(), 8);
        std::memcpy(&id.low, bytes.data() + 8, 8);
        if (!id.isNull()) {
            return id;
        }
    }
}

std::string RawId::encode() const {
    // 128 bits as a 130-bit big-endian number: 26 digits of 5 bits, the first digit holds 3 bits.
    std::string text(26, '0');
    std::uint64_t hi = high;
    std::uint64_t lo = low;
    for (int i = 25; i >= 0; --i) {
        text[static_cast<std::size_t>(i)] = kAlphabet[lo & 31u];
        lo = (lo >> 5) | (hi << 59);
        hi >>= 5;
    }
    return text;
}

std::optional<RawId> RawId::decode(std::string_view text) {
    if (text.size() != 26) {
        return std::nullopt;
    }
    RawId id;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const int digit = digitValue(text[i]);
        if (digit < 0 || (i == 0 && digit > 7)) {
            return std::nullopt;
        }
        id.high = (id.high << 5) | (id.low >> 59);
        id.low = (id.low << 5) | static_cast<std::uint64_t>(digit);
    }
    return id;
}

} // namespace archmage::base
