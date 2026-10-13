// Spoolable authoritative tiles (D-26, ADR-0026, plan S1-5.4).
//
// The recovery journal registers every dense tile whose samples it has made durable. Under RAM pressure,
// ResourceCoordinator calls this registry's evictor (class Spoolable, after disposable caches), which
// frees the samples of the least recently pinned registered tiles; Tile::pin() reads them back from the
// journal. Tiles that are not journaled are never evicted, so nothing unsaved can be lost by eviction.
//
// Thread safety: all members are safe from any thread. The evictor runs under the coordinator's lock and
// only try-locks tiles, so it never waits on a thread that is reading a tile back.
#pragma once

#include "archmage/tiles/Tile.h"

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace archmage::tiles {

class TileSpool {
public:
    struct Stats {
        std::uint64_t tracked = 0;   // live registered tiles
        std::uint64_t resident = 0;  // of which in memory
        std::uint64_t evictions = 0; // since start
        std::uint64_t reloads = 0;   // since start
        std::uint64_t evictedBytes = 0;
    };

    /// The process-wide registry; registers its evictor with ResourceCoordinator::global().
    static TileSpool &global();

    TileSpool();
    ~TileSpool();
    TileSpool(const TileSpool &) = delete;
    TileSpool &operator=(const TileSpool &) = delete;

    /// Makes a dense tile evictable now that `source` holds its planes. Uniform tiles and tiles already
    /// registered are ignored.
    void add(const std::shared_ptr<const Tile> &tile, const std::shared_ptr<SpoolSource> &source,
             std::vector<SpoolPlane> planes);
    /// Frees least recently used unpinned tiles until `bytes` are released; returns the bytes released.
    std::uint64_t evict(std::uint64_t bytes);
    /// Resource hashes that live tiles of `source` may need to read back (compaction keeps them).
    [[nodiscard]] std::vector<std::string> liveHashes(const SpoolSource *source);
    /// Reads every live tile of `source` back and forgets the source, before its storage goes away.
    [[nodiscard]] base::Status detach(const SpoolSource *source);
    [[nodiscard]] Stats stats();
    void noteReload();

private:
    struct Entry {
        std::weak_ptr<const Tile> tile;
        const SpoolSource *source = nullptr;
    };
    void pruneLocked();

    std::mutex mutex_;
    std::vector<Entry> entries_;
    std::uint64_t evictions_ = 0;
    std::uint64_t evictedBytes_ = 0;
    std::atomic<std::uint64_t> reloads_{0};
    std::uint64_t evictorHandle_ = 0;
};

} // namespace archmage::tiles
