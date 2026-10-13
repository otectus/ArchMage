// The CPU evaluator (D-09, D-12, D-24, spec §7.2, §7.3, App. A.3): renders requested regions of a
// document through its compiled graph, tile by tile on a worker pool, with cancellation and a bounded
// cache of output tiles.
//
// Exports and saved composites use render() at final quality on the authoritative graph; nothing here
// reads a viewport (spec §7.3). The tile cache is a disposable RAM consumer registered with
// ResourceCoordinator::global() and evicts least-recently-used tiles first.
//
// Thread safety: render() and renderTile() may be called from several threads at once.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Id.h"
#include "archmage/color/ColorProfile.h"
#include "archmage/document/Document.h"
#include "archmage/render/Graph.h"
#include "archmage/tiles/TileMap.h"

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <span>
#include <stop_token>
#include <thread>
#include <unordered_map>
#include <vector>

namespace archmage::render {

enum class AlphaAssociation : std::uint8_t { Straight, Premultiplied };

struct RenderDestination {
    tiles::SampleType sampleType = tiles::SampleType::UInt8; // UInt8 or Float32 in Stage 1
    AlphaAssociation alpha = AlphaAssociation::Straight;
    std::shared_ptr<const color::ColorProfile> profile; // null: the document profile
};

/// Spec App. A.3. `revision` documents which revision the caller rendered; the state passed to render()
/// is authoritative.
struct RenderRequest {
    base::DocumentId document;
    std::uint64_t revision = 0;
    base::IRect region;
    RationalTime time;
    Quality quality = Quality::Final;
    RenderDestination destination;
};

struct RenderResult {
    base::IRect region;
    tiles::SampleType sampleType = tiles::SampleType::UInt8;
    AlphaAssociation alpha = AlphaAssociation::Straight;
    std::vector<std::byte> pixels; // interleaved RGBA rows, top to bottom
    std::uint64_t nonFiniteSamples = 0;

    [[nodiscard]] std::size_t stride() const {
        return static_cast<std::size_t>(region.width()) * 4 * tiles::bytesPerSample(sampleType);
    }
    /// Channel `c` (0..3) of the pixel at document coordinates as float (tests and tools).
    [[nodiscard]] float sample(std::int64_t x, std::int64_t y, int c) const;
};

class CpuEvaluator {
public:
    struct Options {
        std::size_t threads = 0;                 // 0: max(2, hardware_concurrency - 2) (D-24)
        std::uint64_t cacheBytes = 256ull << 20; // output tile cache budget
    };
    struct CacheStats {
        std::uint64_t hits = 0;
        std::uint64_t misses = 0;
        std::uint64_t entries = 0;
        std::uint64_t bytes = 0;
        std::uint64_t evictions = 0;
    };

    CpuEvaluator();
    explicit CpuEvaluator(Options options);
    ~CpuEvaluator();
    CpuEvaluator(const CpuEvaluator &) = delete;
    CpuEvaluator &operator=(const CpuEvaluator &) = delete;

    /// Renders `request` from `document` through its compiled graph.
    base::Result<RenderResult> render(const document::DocumentState &document, const RenderRequest &request,
                                      std::stop_token stop = {});
    /// Renders an arbitrary graph whose samples are in `policy`'s working space.
    base::Result<RenderResult> renderGraph(const GraphNode &root, const color::ColorPolicy &policy,
                                           const RenderRequest &request, std::stop_token stop = {});
    /// Premultiplied output of `root` for one document tile, from the cache when possible.
    base::Result<std::shared_ptr<const ImageBuffer>> renderTile(const GraphNode &root, tiles::TileCoord tile,
                                                                const EvaluationContext &context);
    /// Several tiles at once on the worker pool; results are in the order of `coords`. Must not be
    /// called from a worker thread.
    std::vector<base::Result<std::shared_ptr<const ImageBuffer>>>
    renderTiles(const GraphNode &root, std::span<const tiles::TileCoord> coords,
                const EvaluationContext &context);

    [[nodiscard]] CacheStats cacheStats() const;
    void clearCache();
    /// Tiles computed rather than served from the cache, since construction.
    [[nodiscard]] std::uint64_t tilesEvaluated() const { return evaluated_.load(); }
    [[nodiscard]] std::size_t threadCount() const { return workers_.size(); }

private:
    struct CacheKey {
        std::uint64_t node;
        tiles::TileCoord tile;
        Quality quality;
        RationalTime time;
        friend bool operator==(const CacheKey &, const CacheKey &) = default;
    };
    struct CacheKeyHash {
        std::size_t operator()(const CacheKey &k) const noexcept;
    };
    struct CacheEntry {
        std::shared_ptr<const ImageBuffer> tile;
        std::list<CacheKey>::iterator lru;
    };

    void workerLoop(std::stop_token stop);
    void post(std::function<void()> task);
    std::uint64_t evictCache(std::uint64_t bytesWanted);
    void insertCache(const CacheKey &key, std::shared_ptr<const ImageBuffer> tile);

    Options options_;
    std::vector<std::jthread> workers_;
    std::mutex queueMutex_;
    std::condition_variable_any queueReady_;
    std::deque<std::function<void()>> queue_;

    mutable std::mutex cacheMutex_;
    std::unordered_map<CacheKey, CacheEntry, CacheKeyHash> cache_;
    std::list<CacheKey> lru_;
    CacheStats stats_;
    std::uint64_t evictorHandle_ = 0;
    std::atomic<std::uint64_t> evaluated_{0};
};

} // namespace archmage::render
