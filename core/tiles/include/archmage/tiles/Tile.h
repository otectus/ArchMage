// Immutable 128 × 128 channel-planar tiles (D-08, spec §7.1). A tile is either uniform (one sample value
// per plane, no pixel allocation) or dense (planar samples). Tiles are shared by pointer across
// revisions, duplicates and caches; a modification builds a new tile through TileBuffer.
//
// Dense tiles charge Budget::Ram on ResourceCoordinator::global() while they live.
//
// Thread safety: a sealed Tile is immutable and safe to read from any thread. TileBuffer is not
// thread-safe.
#pragma once

#include "archmage/tiles/ChannelLayout.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace archmage::tiles {

class Tile {
public:
    /// Uniform tile; `value` holds one sample per plane (planes × bytesPerSample bytes).
    [[nodiscard]] static std::shared_ptr<const Tile> makeUniform(SampleType type, std::size_t planes,
                                                                 std::span<const std::byte> value);
    /// Uniform tile with every sample zero (transparent for straight RGBA).
    [[nodiscard]] static std::shared_ptr<const Tile> makeZero(SampleType type, std::size_t planes);

    ~Tile();
    Tile(const Tile &) = delete;
    Tile &operator=(const Tile &) = delete;

    [[nodiscard]] SampleType sampleType() const { return type_; }
    [[nodiscard]] std::size_t planeCount() const { return planes_; }
    [[nodiscard]] bool isUniform() const { return uniform_; }
    /// Globally unique content version, assigned when the tile is sealed.
    [[nodiscard]] std::uint64_t version() const { return version_; }

    /// Dense planar samples of one plane (kTilePixels samples). Precondition: !isUniform().
    [[nodiscard]] std::span<const std::byte> plane(std::size_t index) const;
    /// The single sample of one plane. Precondition: isUniform().
    [[nodiscard]] std::span<const std::byte> uniformSample(std::size_t index) const;
    /// One sample as float (slow path for tests and tools; kernels read planes directly).
    [[nodiscard]] float sample(std::size_t plane, int x, int y) const;
    /// True when every sample of both tiles is identical (same type and plane count required).
    [[nodiscard]] bool sameContent(const Tile &other) const;
    /// Bytes of pixel storage this tile owns (0 when uniform).
    [[nodiscard]] std::size_t denseBytes() const { return uniform_ ? 0 : data_.size(); }

private:
    friend class TileBuffer;
    Tile(SampleType type, std::size_t planes, bool uniform, std::vector<std::byte> data);

    SampleType type_;
    std::size_t planes_;
    bool uniform_;
    std::uint64_t version_;
    std::vector<std::byte> data_; // uniform: planes × sample; dense: planes × kTilePixels × sample
};

/// Writable dense tile under construction.
class TileBuffer {
public:
    /// All samples zero.
    TileBuffer(SampleType type, std::size_t planes);
    /// A copy of `source` (a uniform tile is expanded).
    explicit TileBuffer(const Tile &source);

    [[nodiscard]] SampleType sampleType() const { return type_; }
    [[nodiscard]] std::size_t planeCount() const { return planes_; }
    [[nodiscard]] std::span<std::byte> plane(std::size_t index);
    [[nodiscard]] std::span<const std::byte> plane(std::size_t index) const;

    /// Typed view of one plane; T must match the sample type (std::uint8_t, std::uint16_t, float).
    template <class T> [[nodiscard]] std::span<T> planeAs(std::size_t index) {
        const std::span<std::byte> bytes = plane(index);
        return {reinterpret_cast<T *>(bytes.data()), bytes.size() / sizeof(T)};
    }

    void fill(std::size_t plane, float value);
    [[nodiscard]] float sample(std::size_t plane, int x, int y) const;
    void setSample(std::size_t plane, int x, int y, float value);

    /// Seals into an immutable tile; compacts to a uniform tile when every plane is uniform.
    [[nodiscard]] std::shared_ptr<const Tile> seal() &&;

private:
    SampleType type_;
    std::size_t planes_;
    std::vector<std::byte> data_;
};

} // namespace archmage::tiles
