// Immutable 128 × 128 channel-planar tiles (D-08, spec §7.1). A tile is either uniform (one sample value
// per plane, no pixel allocation) or dense (planar samples). Tiles are shared by pointer across
// revisions, duplicates and caches; a modification builds a new tile through TileBuffer.
//
// Dense tiles charge Budget::Ram on ResourceCoordinator::global() while their samples are in memory, and
// ask it for room first. Once the recovery journal holds a dense tile's samples (setSpool), the tile may
// be evicted under memory pressure and read back when pinned (D-26 spooling, TileSpool). Reading the
// planes of a dense tile therefore requires a Pin, which keeps the samples resident while it lives.
//
// Thread safety: a sealed Tile's content is immutable and safe to read from any thread while pinned;
// pinning, eviction and spool registration are internally synchronized. TileBuffer is not thread-safe.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/tiles/ChannelLayout.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

namespace archmage::tiles {

/// One plane of a dense tile as the spool stores it: a content-addressed resource, or (when every sample
/// of the plane is equal) that single sample.
struct SpoolPlane {
    std::string resourceHash;
    std::vector<std::byte> uniformSample;
};

/// Where evicted tile samples are read back from (the recovery journal implements it).
class SpoolSource {
public:
    virtual ~SpoolSource() = default;
    /// Fills `out` (planes × kTilePixels samples, planar) from `planes`.
    [[nodiscard]] virtual base::Status load(std::span<const SpoolPlane> planes, SampleType type,
                                            std::span<std::byte> out) = 0;
};

class Tile {
public:
    /// Keeps a dense tile's samples in memory while it lives. A default-constructed or uniform-tile pin
    /// holds nothing.
    class Pin {
    public:
        Pin() = default;
        Pin(Pin &&other) noexcept;
        Pin &operator=(Pin &&other) noexcept;
        Pin(const Pin &) = delete;
        Pin &operator=(const Pin &) = delete;
        ~Pin();

    private:
        friend class Tile;
        explicit Pin(const Tile *tile) : tile_(tile) {}
        const Tile *tile_ = nullptr;
    };

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

    /// Makes a dense tile's samples resident, reading them back from the spool if they were evicted, and
    /// keeps them so until the pin is destroyed. Fails only when an evicted tile cannot be read back.
    [[nodiscard]] base::Result<Pin> pin() const;
    /// Dense planar samples of one plane (kTilePixels samples). Preconditions: !isUniform() and a live Pin.
    [[nodiscard]] std::span<const std::byte> plane(std::size_t index) const;
    /// The single sample of one plane. Precondition: isUniform().
    [[nodiscard]] std::span<const std::byte> uniformSample(std::size_t index) const;
    /// One sample as float (slow path for tests and tools; pins internally; NaN if it cannot be read).
    [[nodiscard]] float sample(std::size_t plane, int x, int y) const;
    /// True when every sample of both tiles is identical (pins internally; false if either cannot be read).
    [[nodiscard]] bool sameContent(const Tile &other) const;
    /// Bytes of pixel storage a dense tile has when resident (0 when uniform).
    [[nodiscard]] std::size_t denseBytes() const { return uniform_ ? 0 : denseSize_; }

    /// Records that this dense tile's samples are stored in `source`; from then on it may be evicted. The
    /// first registration wins; uniform tiles ignore it. TileSpool calls this.
    void setSpool(std::shared_ptr<SpoolSource> source, std::vector<SpoolPlane> planes) const;
    /// Reads the samples back if needed and forgets the spool: the tile can no longer be evicted.
    [[nodiscard]] base::Status detachSpool() const;
    [[nodiscard]] bool isSpooled() const;
    [[nodiscard]] bool isResident() const { return resident_.load(std::memory_order_acquire); }
    [[nodiscard]] const SpoolSource *spoolSource() const;
    [[nodiscard]] std::vector<SpoolPlane> spoolPlanes() const;
    /// Frees the samples of a spooled, unpinned, resident tile; returns the bytes released (0 otherwise).
    std::uint64_t evict() const;
    /// Recency stamp of the last pin, for least-recently-used eviction.
    [[nodiscard]] std::uint64_t lastUse() const { return lastUse_.load(std::memory_order_relaxed); }

private:
    friend class TileBuffer;
    Tile(SampleType type, std::size_t planes, bool uniform, std::vector<std::byte> data);
    void unpin() const;

    SampleType type_;
    std::size_t planes_;
    bool uniform_;
    std::uint64_t version_;
    std::size_t denseSize_ = 0;

    // Residency (dense tiles only). `data_` is empty while evicted; pins are taken under `mutex_`.
    mutable std::mutex mutex_;
    mutable std::vector<std::byte> data_; // uniform: planes × sample; dense: planes × kTilePixels × sample
    mutable std::atomic<bool> resident_{true};
    mutable std::atomic<std::uint32_t> pins_{0};
    mutable std::atomic<std::uint64_t> lastUse_{0};
    mutable std::shared_ptr<SpoolSource> source_;
    mutable std::vector<SpoolPlane> spoolPlanes_;
};

/// Writable dense tile under construction.
class TileBuffer {
public:
    /// All samples zero.
    TileBuffer(SampleType type, std::size_t planes);
    /// A copy of `source` (a uniform tile is expanded); fails only when an evicted tile cannot be read back.
    [[nodiscard]] static base::Result<TileBuffer> copyOf(const Tile &source);

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
