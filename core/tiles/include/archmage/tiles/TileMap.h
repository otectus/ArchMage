// Sparse tile maps with copy-on-write sharing (D-08, spec §7.1).
//
// A TileMap stores tiles by integer tile coordinate; absent coordinates read as the map's default tile
// (usually transparent), so a 300,000-pixel-square empty layer allocates nothing. Copying a map is O(1):
// copies share their storage, and a modification clones only the 16 × 16-tile chunk it touches plus the
// chunk directory. Two maps can be diffed quickly by pointer comparison, which is how dirty regions
// between revisions are found.
//
// Thread safety: a const TileMap is safe to read from any thread. Mutating a map is not thread-safe, and
// a map must not be copied by one thread while another mutates it.
#pragma once

#include "archmage/base/Geometry.h"
#include "archmage/tiles/ChannelLayout.h"
#include "archmage/tiles/Tile.h"

#include <array>
#include <compare>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <unordered_map>
#include <vector>

namespace archmage::tiles {

struct TileCoord {
    std::int64_t tx = 0;
    std::int64_t ty = 0;

    friend constexpr auto operator<=>(const TileCoord &, const TileCoord &) = default;
};

struct TileCoordHash {
    std::size_t operator()(const TileCoord &c) const noexcept {
        return static_cast<std::size_t>(static_cast<std::uint64_t>(c.tx) * 0x9e3779b97f4a7c15ULL ^
                                        static_cast<std::uint64_t>(c.ty));
    }
};

/// Tile containing a pixel (floor division; negative coordinates are valid).
[[nodiscard]] TileCoord tileContaining(base::IPoint pixel);
/// Pixel rectangle covered by a tile.
[[nodiscard]] base::IRect tilePixelRect(TileCoord tile);
/// Half-open range of tile coordinates (in tile units) covering a pixel rectangle; empty for empty input.
[[nodiscard]] base::IRect tileRangeCovering(const base::IRect &pixels);
/// Bytes a dense allocation of `pixels` would take with `layout`, or nullopt on overflow (D-17).
[[nodiscard]] std::optional<std::uint64_t> denseBytes(const base::IRect &pixels, const ChannelLayout &layout);

class TileMap {
public:
    /// Absent tiles read as zero (transparent for straight RGBA).
    explicit TileMap(ChannelLayout layout);
    /// Absent tiles read as `defaultTile`, which must be uniform and match the layout.
    TileMap(ChannelLayout layout, std::shared_ptr<const Tile> defaultTile);

    [[nodiscard]] const ChannelLayout &layout() const { return *layout_; }
    [[nodiscard]] const std::shared_ptr<const Tile> &defaultTile() const { return default_; }

    /// The stored tile, or nullptr when the coordinate reads as the default.
    [[nodiscard]] std::shared_ptr<const Tile> find(TileCoord coord) const;
    /// The stored tile or the default tile; never null.
    [[nodiscard]] std::shared_ptr<const Tile> at(TileCoord coord) const;

    /// Stores a tile. A null tile, or a uniform tile with the default's content, erases the entry.
    void set(TileCoord coord, std::shared_ptr<const Tile> tile);
    void erase(TileCoord coord);

    /// Entries that differ from the default.
    [[nodiscard]] std::size_t tileCount() const { return tileCount_; }
    /// Entries with dense pixel storage.
    [[nodiscard]] std::size_t denseTileCount() const;
    /// Bytes of dense storage referenced by this map (shared tiles counted once per map).
    [[nodiscard]] std::uint64_t denseBytes() const;
    /// Bounding box of stored tiles in pixels; empty when the map is empty.
    [[nodiscard]] base::IRect pixelBounds() const;

    /// Visits stored tiles in a deterministic order (chunks by row, then tiles by row within a chunk).
    void forEach(const std::function<void(TileCoord, const std::shared_ptr<const Tile> &)> &visit) const;
    /// Tile coordinates whose stored tile differs (by identity) between two maps; sorted.
    [[nodiscard]] static std::vector<TileCoord> changedTiles(const TileMap &a, const TileMap &b);
    /// True when both maps share all storage (a copy that has not been modified).
    [[nodiscard]] bool sharesStorageWith(const TileMap &other) const { return dir_ == other.dir_; }

private:
    static constexpr int kChunkShift = 4; // 16 × 16 tiles per chunk
    static constexpr std::int64_t kChunkTiles = std::int64_t{1} << kChunkShift;

    struct Chunk {
        std::array<std::shared_ptr<const Tile>, kChunkTiles * kChunkTiles> tiles;
        std::uint16_t count = 0;
    };
    using Directory = std::unordered_map<TileCoord, std::shared_ptr<Chunk>, TileCoordHash>;

    static TileCoord chunkOf(TileCoord tile);
    static std::size_t indexInChunk(TileCoord tile);
    bool isDefaultContent(const Tile &tile) const;
    Directory &mutableDirectory();
    std::vector<TileCoord> sortedChunks() const;

    std::shared_ptr<const ChannelLayout> layout_;
    std::shared_ptr<const Tile> default_;
    std::shared_ptr<Directory> dir_;
    std::size_t tileCount_ = 0;
};

/// A set of tiles touched by an edit, in pixel or tile terms (sorted, deterministic).
class DirtyRegion {
public:
    void addPixels(const base::IRect &pixels);
    void addTile(TileCoord tile);
    void merge(const DirtyRegion &other);

    [[nodiscard]] bool empty() const { return tiles_.empty(); }
    [[nodiscard]] std::size_t tileCount() const { return tiles_.size(); }
    [[nodiscard]] bool containsTile(TileCoord tile) const { return tiles_.contains(tile); }
    [[nodiscard]] const std::set<TileCoord> &tiles() const { return tiles_; }
    [[nodiscard]] base::IRect pixelBounds() const;

private:
    std::set<TileCoord> tiles_;
};

} // namespace archmage::tiles
