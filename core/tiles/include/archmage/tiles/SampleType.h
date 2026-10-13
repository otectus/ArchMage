// Sample types and conversions (D-08, spec §8.1). Stage 1 exercises uint8 and float32 end to end; uint16
// and float16 exist in the type system with tested conversions.
//
// Thread safety: pure functions.
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>

namespace archmage::tiles {

enum class SampleType : std::uint8_t { UInt8, UInt16, Float16, Float32 };

[[nodiscard]] constexpr std::size_t bytesPerSample(SampleType type) {
    switch (type) {
    case SampleType::UInt8:
        return 1;
    case SampleType::UInt16:
    case SampleType::Float16:
        return 2;
    case SampleType::Float32:
        return 4;
    }
    return 0;
}

[[nodiscard]] std::string_view sampleTypeName(SampleType type);

/// IEEE 754 binary16 conversions (round to nearest even; infinities and NaN preserved).
[[nodiscard]] float halfToFloat(std::uint16_t half);
[[nodiscard]] std::uint16_t floatToHalf(float value);

/// Reads one sample as float: integers are normalized to [0, 1], floats are returned as stored.
[[nodiscard]] inline float loadSample(SampleType type, const std::byte *p) {
    switch (type) {
    case SampleType::UInt8:
        return static_cast<float>(static_cast<std::uint8_t>(*p)) * (1.0f / 255.0f);
    case SampleType::UInt16: {
        std::uint16_t v;
        std::memcpy(&v, p, 2);
        return static_cast<float>(v) * (1.0f / 65535.0f);
    }
    case SampleType::Float16: {
        std::uint16_t v;
        std::memcpy(&v, p, 2);
        return halfToFloat(v);
    }
    case SampleType::Float32: {
        float v;
        std::memcpy(&v, p, 4);
        return v;
    }
    }
    return 0.0f;
}

/// Writes one sample from float: integers clamp to [0, 1] and round to nearest (NaN becomes 0);
/// floats are stored as given (D-09: negative and above-one values survive in float documents).
void storeSample(SampleType type, float value, std::byte *p);

} // namespace archmage::tiles
