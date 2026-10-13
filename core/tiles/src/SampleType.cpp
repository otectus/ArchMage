#include "archmage/tiles/SampleType.h"

#include <bit>
#include <cmath>

namespace archmage::tiles {

std::string_view sampleTypeName(SampleType type) {
    switch (type) {
    case SampleType::UInt8:
        return "uint8";
    case SampleType::UInt16:
        return "uint16";
    case SampleType::Float16:
        return "float16";
    case SampleType::Float32:
        return "float32";
    }
    return "unknown";
}

std::optional<SampleType> sampleTypeFromName(std::string_view name) {
    for (const SampleType type : {SampleType::UInt8, SampleType::UInt16, SampleType::Float16, SampleType::Float32}) {
        if (sampleTypeName(type) == name) {
            return type;
        }
    }
    return std::nullopt;
}

float halfToFloat(std::uint16_t half) {
    const std::uint32_t sign = static_cast<std::uint32_t>(half & 0x8000u) << 16;
    const std::uint32_t exponent = (half >> 10) & 0x1fu;
    std::uint32_t mantissa = half & 0x3ffu;
    std::uint32_t bits = 0;
    if (exponent == 0) {
        if (mantissa == 0) {
            bits = sign;
        } else { // subnormal: normalize
            int e = -1;
            do {
                ++e;
                mantissa <<= 1;
            } while ((mantissa & 0x400u) == 0);
            bits = sign | static_cast<std::uint32_t>(127 - 15 - e) << 23 | (mantissa & 0x3ffu) << 13;
        }
    } else if (exponent == 31) {
        bits = sign | 0x7f800000u | mantissa << 13;
    } else {
        bits = sign | (exponent + 127 - 15) << 23 | mantissa << 13;
    }
    return std::bit_cast<float>(bits);
}

std::uint16_t floatToHalf(float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t absBits = bits & 0x7fffffffu;
    if (absBits >= 0x7f800000u) { // infinity or NaN
        return static_cast<std::uint16_t>(sign | 0x7c00u | (absBits > 0x7f800000u ? 0x200u : 0u));
    }
    if (absBits >= 0x477ff000u) { // rounds to a value beyond the largest half: infinity
        return static_cast<std::uint16_t>(sign | 0x7c00u);
    }
    if (absBits < 0x38800000u) { // below the smallest normal half: subnormal or zero
        if (absBits < 0x33000000u) {
            return static_cast<std::uint16_t>(sign);
        }
        const std::uint32_t exponent = absBits >> 23;
        const std::uint32_t mantissa = (absBits & 0x7fffffu) | 0x800000u;
        const std::uint32_t shift = 126u - exponent; // 14..24
        std::uint32_t half = mantissa >> shift;
        const std::uint32_t remainder = mantissa & ((1u << shift) - 1u);
        const std::uint32_t halfway = 1u << (shift - 1u);
        if (remainder > halfway || (remainder == halfway && (half & 1u) != 0)) {
            ++half;
        }
        return static_cast<std::uint16_t>(sign | half);
    }
    std::uint32_t half = ((absBits >> 13) - (112u << 10));
    const std::uint32_t remainder = absBits & 0x1fffu;
    if (remainder > 0x1000u || (remainder == 0x1000u && (half & 1u) != 0)) {
        ++half;
    }
    return static_cast<std::uint16_t>(sign | half);
}

void storeSample(SampleType type, float value, std::byte *p) {
    switch (type) {
    case SampleType::UInt8: {
        const float v = std::isnan(value) ? 0.0f : value;
        const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        *p = static_cast<std::byte>(static_cast<std::uint8_t>(std::lround(clamped * 255.0f)));
        return;
    }
    case SampleType::UInt16: {
        const float v = std::isnan(value) ? 0.0f : value;
        const float clamped = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
        const auto s = static_cast<std::uint16_t>(std::lround(clamped * 65535.0f));
        std::memcpy(p, &s, 2);
        return;
    }
    case SampleType::Float16: {
        const std::uint16_t h = floatToHalf(value);
        std::memcpy(p, &h, 2);
        return;
    }
    case SampleType::Float32:
        std::memcpy(p, &value, 4);
        return;
    }
}

} // namespace archmage::tiles
