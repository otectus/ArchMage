#include "archmage/tiles/TileMap.h"

#include "archmage/base/Checked.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <unordered_set>

namespace archmage::tiles {
namespace {

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

std::int64_t floorDivShift(std::int64_t a, int shift) { return a >> shift; } // arithmetic shift floors

std::atomic<std::uint64_t> gNextGeneration{1};

std::uint64_t nextGeneration() { return gNextGeneration.fetch_add(1, std::memory_order_relaxed); }

} // namespace

TileCoord tileContaining(base::IPoint pixel) {
    return {floorDiv(pixel.x, kTileSize), floorDiv(pixel.y, kTileSize)};
}

base::IRect tilePixelRect(TileCoord tile) {
    return {tile.tx * kTileSize, tile.ty * kTileSize, (tile.tx + 1) * kTileSize, (tile.ty + 1) * kTileSize};
}

base::IRect tileRangeCovering(const base::IRect &pixels) {
    if (pixels.empty()) {
        return {};
    }
    return {floorDiv(pixels.x0, kTileSize), floorDiv(pixels.y0, kTileSize),
            floorDiv(pixels.x1 - 1, kTileSize) + 1, floorDiv(pixels.y1 - 1, kTileSize) + 1};
}

std::optional<std::uint64_t> denseBytes(const base::IRect &pixels, const ChannelLayout &layout) {
    const auto area = pixels.checkedArea();
    if (!area) {
        return std::nullopt;
    }
    const auto perPixel = base::checkedMul<std::uint64_t>(layout.sampleBytes(), layout.planeCount());
    if (!perPixel) {
        return std::nullopt;
    }
    return base::checkedMul<std::uint64_t>(static_cast<std::uint64_t>(*area), *perPixel);
}

TileMap::TileMap(ChannelLayout layout)
    : TileMap(layout, Tile::makeZero(layout.sampleType(), layout.planeCount())) {}

TileMap::TileMap(ChannelLayout layout, std::shared_ptr<const Tile> defaultTile)
    : layout_(std::make_shared<const ChannelLayout>(std::move(layout))), default_(std::move(defaultTile)),
      dir_(std::make_shared<Directory>()), generation_(nextGeneration()) {
    assert(default_ && default_->isUniform() && default_->planeCount() == layout_->planeCount() &&
           default_->sampleType() == layout_->sampleType());
}

TileCoord TileMap::chunkOf(TileCoord tile) {
    return {floorDivShift(tile.tx, kChunkShift), floorDivShift(tile.ty, kChunkShift)};
}

std::size_t TileMap::indexInChunk(TileCoord tile) {
    const std::int64_t lx = tile.tx & (kChunkTiles - 1);
    const std::int64_t ly = tile.ty & (kChunkTiles - 1);
    return static_cast<std::size_t>(ly * kChunkTiles + lx);
}

bool TileMap::isDefaultContent(const Tile &tile) const {
    return tile.isUniform() && tile.sameContent(*default_);
}

TileMap::Directory &TileMap::mutableDirectory() {
    if (dir_.use_count() > 1) {
        dir_ = std::make_shared<Directory>(*dir_);
    }
    return *dir_;
}

std::shared_ptr<const Tile> TileMap::find(TileCoord coord) const {
    const auto it = dir_->find(chunkOf(coord));
    if (it == dir_->end()) {
        return nullptr;
    }
    return it->second->tiles[indexInChunk(coord)];
}

std::shared_ptr<const Tile> TileMap::at(TileCoord coord) const {
    std::shared_ptr<const Tile> tile = find(coord);
    return tile ? tile : default_;
}

void TileMap::set(TileCoord coord, std::shared_ptr<const Tile> tile) {
    if (!tile || isDefaultContent(*tile)) {
        erase(coord);
        return;
    }
    assert(tile->planeCount() == layout_->planeCount() && tile->sampleType() == layout_->sampleType());
    Directory &dir = mutableDirectory();
    std::shared_ptr<Chunk> &chunk = dir[chunkOf(coord)];
    if (!chunk) {
        chunk = std::make_shared<Chunk>();
    } else if (chunk.use_count() > 1) {
        chunk = std::make_shared<Chunk>(*chunk);
    }
    std::shared_ptr<const Tile> &slot = chunk->tiles[indexInChunk(coord)];
    if (!slot) {
        ++chunk->count;
        ++tileCount_;
    }
    slot = std::move(tile);
    generation_ = nextGeneration();
}

void TileMap::erase(TileCoord coord) {
    const TileCoord chunkCoord = chunkOf(coord);
    const auto existing = dir_->find(chunkCoord);
    if (existing == dir_->end() || !existing->second->tiles[indexInChunk(coord)]) {
        return;
    }
    Directory &dir = mutableDirectory();
    std::shared_ptr<Chunk> &chunk = dir[chunkCoord];
    if (chunk.use_count() > 1) {
        chunk = std::make_shared<Chunk>(*chunk);
    }
    chunk->tiles[indexInChunk(coord)].reset();
    --chunk->count;
    --tileCount_;
    if (chunk->count == 0) {
        dir.erase(chunkCoord);
    }
    generation_ = nextGeneration();
}

std::vector<TileCoord> TileMap::sortedChunks() const {
    std::vector<TileCoord> chunks;
    chunks.reserve(dir_->size());
    for (const auto &[coord, chunk] : *dir_) {
        chunks.push_back(coord);
    }
    std::sort(chunks.begin(), chunks.end(),
              [](TileCoord a, TileCoord b) { return a.ty != b.ty ? a.ty < b.ty : a.tx < b.tx; });
    return chunks;
}

void TileMap::forEach(
    const std::function<void(TileCoord, const std::shared_ptr<const Tile> &)> &visit) const {
    for (const TileCoord chunkCoord : sortedChunks()) {
        const Chunk &chunk = *dir_->at(chunkCoord);
        for (std::int64_t ly = 0; ly < kChunkTiles; ++ly) {
            for (std::int64_t lx = 0; lx < kChunkTiles; ++lx) {
                const auto &tile = chunk.tiles[static_cast<std::size_t>(ly * kChunkTiles + lx)];
                if (tile) {
                    visit({chunkCoord.tx * kChunkTiles + lx, chunkCoord.ty * kChunkTiles + ly}, tile);
                }
            }
        }
    }
}

std::size_t TileMap::denseTileCount() const {
    std::size_t count = 0;
    forEach(
        [&count](TileCoord, const std::shared_ptr<const Tile> &tile) { count += tile->isUniform() ? 0 : 1; });
    return count;
}

std::uint64_t TileMap::denseBytes() const {
    std::uint64_t bytes = 0;
    std::unordered_set<const Tile *> seen;
    forEach([&](TileCoord, const std::shared_ptr<const Tile> &tile) {
        if (seen.insert(tile.get()).second) {
            bytes += tile->denseBytes();
        }
    });
    return bytes;
}

base::IRect TileMap::pixelBounds() const {
    base::IRect bounds;
    forEach([&bounds](TileCoord coord, const std::shared_ptr<const Tile> &) {
        bounds = bounds.united(tilePixelRect(coord));
    });
    return bounds;
}

std::vector<TileCoord> TileMap::changedTiles(const TileMap &a, const TileMap &b) {
    std::vector<TileCoord> changed;
    if (a.dir_ == b.dir_) {
        return changed;
    }
    std::set<TileCoord> chunks;
    for (const auto &[coord, chunk] : *a.dir_) {
        chunks.insert(coord);
    }
    for (const auto &[coord, chunk] : *b.dir_) {
        chunks.insert(coord);
    }
    for (const TileCoord chunkCoord : chunks) {
        const auto ia = a.dir_->find(chunkCoord);
        const auto ib = b.dir_->find(chunkCoord);
        const Chunk *ca = ia == a.dir_->end() ? nullptr : ia->second.get();
        const Chunk *cb = ib == b.dir_->end() ? nullptr : ib->second.get();
        if (ca == cb) {
            continue;
        }
        for (std::int64_t ly = 0; ly < kChunkTiles; ++ly) {
            for (std::int64_t lx = 0; lx < kChunkTiles; ++lx) {
                const auto index = static_cast<std::size_t>(ly * kChunkTiles + lx);
                const Tile *ta = ca ? ca->tiles[index].get() : nullptr;
                const Tile *tb = cb ? cb->tiles[index].get() : nullptr;
                if (ta != tb) {
                    changed.push_back({chunkCoord.tx * kChunkTiles + lx, chunkCoord.ty * kChunkTiles + ly});
                }
            }
        }
    }
    std::sort(changed.begin(), changed.end(),
              [](TileCoord x, TileCoord y) { return x.ty != y.ty ? x.ty < y.ty : x.tx < y.tx; });
    return changed;
}

void DirtyRegion::addPixels(const base::IRect &pixels) {
    const base::IRect range = tileRangeCovering(pixels);
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            tiles_.insert({tx, ty});
        }
    }
}

void DirtyRegion::addTile(TileCoord tile) { tiles_.insert(tile); }

void DirtyRegion::merge(const DirtyRegion &other) { tiles_.insert(other.tiles_.begin(), other.tiles_.end()); }

base::IRect DirtyRegion::pixelBounds() const {
    base::IRect bounds;
    for (const TileCoord tile : tiles_) {
        bounds = bounds.united(tilePixelRect(tile));
    }
    return bounds;
}

} // namespace archmage::tiles
