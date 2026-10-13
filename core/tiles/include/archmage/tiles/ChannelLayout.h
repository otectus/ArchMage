// Channel-planar tile layout (D-08, spec §7.1, §6.4): an ordered list of planes with semantic tags and
// one sample type. Masks, CMYK, spots and data channels use the same interface as RGBA.
//
// Thread safety: an immutable value type.
#pragma once

#include "archmage/tiles/SampleType.h"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace archmage::tiles {

/// Tile edge in pixels (D-08). Recorded in every saved document (S1-4); a different size would be a new
/// format capability, not a silent change.
inline constexpr int kTileSize = 128;
inline constexpr std::size_t kTilePixels = static_cast<std::size_t>(kTileSize) * kTileSize;

enum class ChannelSemantic : std::uint8_t {
    Red,
    Green,
    Blue,
    Alpha,
    Gray,
    Coverage, // selection or mask coverage
    Cyan,
    Magenta,
    Yellow,
    Black,
    Spot,
    Data, // non-color data: normals, heights, measurements (spec §8.4)
    Index,
};

[[nodiscard]] std::string_view channelName(ChannelSemantic semantic);
[[nodiscard]] std::optional<ChannelSemantic> channelFromName(std::string_view name);

class ChannelLayout {
public:
    ChannelLayout(std::vector<ChannelSemantic> planes, SampleType type);

    /// Red, green, blue and straight alpha (D-09 stores unassociated samples).
    [[nodiscard]] static ChannelLayout rgba(SampleType type);
    /// One coverage plane (selections and masks, D-20).
    [[nodiscard]] static ChannelLayout coverage(SampleType type);

    [[nodiscard]] std::size_t planeCount() const { return planes_.size(); }
    [[nodiscard]] SampleType sampleType() const { return type_; }
    [[nodiscard]] ChannelSemantic semantic(std::size_t plane) const { return planes_[plane]; }
    [[nodiscard]] const std::vector<ChannelSemantic> &planes() const { return planes_; }
    [[nodiscard]] std::optional<std::size_t> planeOf(ChannelSemantic semantic) const;

    [[nodiscard]] std::size_t sampleBytes() const { return bytesPerSample(type_); }
    [[nodiscard]] std::size_t planeBytes() const { return kTilePixels * sampleBytes(); }
    [[nodiscard]] std::size_t tileBytes() const { return planeBytes() * planeCount(); }

    friend bool operator==(const ChannelLayout &, const ChannelLayout &) = default;

private:
    std::vector<ChannelSemantic> planes_;
    SampleType type_;
};

} // namespace archmage::tiles
