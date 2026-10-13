#include "archmage/tiles/TileSpool.h"

#include "archmage/tiles/ResourceCoordinator.h"

#include <algorithm>
#include <set>

namespace archmage::tiles {

TileSpool &TileSpool::global() {
    static TileSpool spool;
    return spool;
}

TileSpool::TileSpool() {
    evictorHandle_ = ResourceCoordinator::global().addEvictor(Budget::Ram, EvictionClass::Spoolable,
                                                              [this](std::uint64_t bytes) { return evict(bytes); });
}

TileSpool::~TileSpool() { ResourceCoordinator::global().removeEvictor(evictorHandle_); }

void TileSpool::add(const std::shared_ptr<const Tile> &tile, const std::shared_ptr<SpoolSource> &source,
                    std::vector<SpoolPlane> planes) {
    if (!tile || tile->isUniform() || tile->isSpooled()) {
        return;
    }
    tile->setSpool(source, std::move(planes));
    const std::lock_guard lock(mutex_);
    entries_.push_back({tile, source.get()});
    if (entries_.size() % 4096 == 0) {
        pruneLocked();
    }
}

void TileSpool::pruneLocked() {
    std::erase_if(entries_, [](const Entry &entry) { return entry.tile.expired(); });
}

std::uint64_t TileSpool::evict(std::uint64_t bytes) {
    const std::lock_guard lock(mutex_);
    pruneLocked();
    std::vector<std::pair<std::uint64_t, std::shared_ptr<const Tile>>> candidates;
    candidates.reserve(entries_.size());
    for (const Entry &entry : entries_) {
        if (auto tile = entry.tile.lock(); tile && tile->isResident()) {
            candidates.emplace_back(tile->lastUse(), std::move(tile));
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const auto &a, const auto &b) { return a.first < b.first; });
    std::uint64_t freed = 0;
    for (const auto &[use, tile] : candidates) {
        if (freed >= bytes) {
            break;
        }
        if (const std::uint64_t released = tile->evict(); released > 0) {
            freed += released;
            ++evictions_;
        }
    }
    evictedBytes_ += freed;
    return freed;
}

std::vector<std::string> TileSpool::liveHashes(const SpoolSource *source) {
    std::set<std::string> hashes;
    const std::lock_guard lock(mutex_);
    pruneLocked();
    for (const Entry &entry : entries_) {
        if (entry.source != source) {
            continue;
        }
        if (const auto tile = entry.tile.lock()) {
            for (const SpoolPlane &plane : tile->spoolPlanes()) {
                if (!plane.resourceHash.empty()) {
                    hashes.insert(plane.resourceHash);
                }
            }
        }
    }
    return {hashes.begin(), hashes.end()};
}

base::Status TileSpool::detach(const SpoolSource *source) {
    std::vector<std::shared_ptr<const Tile>> tiles;
    {
        const std::lock_guard lock(mutex_);
        for (const Entry &entry : entries_) {
            if (entry.source == source) {
                if (auto tile = entry.tile.lock()) {
                    tiles.push_back(std::move(tile));
                }
            }
        }
        std::erase_if(entries_, [source](const Entry &entry) { return entry.source == source; });
    }
    // Read back outside the lock: a reload may need room, and room may need the evictor.
    base::Status status;
    for (const auto &tile : tiles) {
        if (auto detached = tile->detachSpool(); !detached && status) {
            status = std::move(detached);
        }
    }
    return status;
}

TileSpool::Stats TileSpool::stats() {
    const std::lock_guard lock(mutex_);
    pruneLocked();
    Stats stats;
    for (const Entry &entry : entries_) {
        if (const auto tile = entry.tile.lock()) {
            ++stats.tracked;
            stats.resident += tile->isResident() ? 1 : 0;
        }
    }
    stats.evictions = evictions_;
    stats.evictedBytes = evictedBytes_;
    stats.reloads = reloads_.load(std::memory_order_relaxed);
    return stats;
}

void TileSpool::noteReload() { reloads_.fetch_add(1, std::memory_order_relaxed); }

} // namespace archmage::tiles
