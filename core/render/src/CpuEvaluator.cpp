#include "archmage/render/CpuEvaluator.h"

#include "archmage/base/Checked.h"
#include "archmage/color/ColorPipeline.h"
#include "archmage/render/Kernels.h"
#include "archmage/tiles/ResourceCoordinator.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <latch>

namespace archmage::render {
namespace {

using base::ErrorCategory;

std::size_t defaultThreads() {
    const unsigned hw = std::thread::hardware_concurrency();
    return std::max<std::size_t>(2, hw > 2 ? hw - 2 : 2);
}

} // namespace

float RenderResult::sample(std::int64_t x, std::int64_t y, int c) const {
    const std::size_t bytes = tiles::bytesPerSample(sampleType);
    const std::size_t offset =
        static_cast<std::size_t>(y - region.y0) * stride() +
        (static_cast<std::size_t>(x - region.x0) * 4 + static_cast<std::size_t>(c)) * bytes;
    return tiles::loadSample(sampleType, pixels.data() + offset);
}

std::size_t CpuEvaluator::CacheKeyHash::operator()(const CacheKey &k) const noexcept {
    std::uint64_t h = k.node;
    h ^= tiles::TileCoordHash{}(k.tile) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
    h ^= (static_cast<std::uint64_t>(k.quality) << 1) ^ static_cast<std::uint64_t>(k.time.numerator) * 31 ^
         static_cast<std::uint64_t>(k.time.denominator);
    return static_cast<std::size_t>(h);
}

CpuEvaluator::CpuEvaluator() : CpuEvaluator(Options{}) {}

CpuEvaluator::CpuEvaluator(Options options) : options_(options) {
    const std::size_t threads = options_.threads == 0 ? defaultThreads() : options_.threads;
    workers_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        workers_.emplace_back([this](std::stop_token stop) { workerLoop(stop); });
    }
    evictorHandle_ = tiles::ResourceCoordinator::global().addEvictor(
        tiles::Budget::Ram, tiles::EvictionClass::Disposable,
        [this](std::uint64_t want) { return evictCache(want); });
}

CpuEvaluator::~CpuEvaluator() {
    tiles::ResourceCoordinator::global().removeEvictor(evictorHandle_);
    for (auto &worker : workers_) {
        worker.request_stop();
    }
    queueReady_.notify_all();
    workers_.clear(); // joins
    clearCache();
}

void CpuEvaluator::workerLoop(std::stop_token stop) {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock lock(queueMutex_);
            if (!queueReady_.wait(lock, stop, [this] { return !queue_.empty(); })) {
                return;
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }
        task();
    }
}

void CpuEvaluator::post(std::function<void()> task) {
    {
        const std::lock_guard lock(queueMutex_);
        queue_.push_back(std::move(task));
    }
    queueReady_.notify_one();
}

std::uint64_t CpuEvaluator::evictCache(std::uint64_t bytesWanted) {
    const std::lock_guard lock(cacheMutex_);
    std::uint64_t freed = 0;
    while (freed < bytesWanted && !lru_.empty()) {
        const CacheKey victim = lru_.back();
        lru_.pop_back();
        const auto it = cache_.find(victim);
        const std::uint64_t bytes = it->second.tile->byteSize();
        cache_.erase(it);
        stats_.bytes -= bytes;
        ++stats_.evictions;
        tiles::ResourceCoordinator::global().release(tiles::Budget::Ram, bytes);
        freed += bytes;
    }
    stats_.entries = cache_.size();
    return freed;
}

void CpuEvaluator::insertCache(const CacheKey &key, std::shared_ptr<const ImageBuffer> tile) {
    const std::uint64_t bytes = tile->byteSize();
    if (bytes > options_.cacheBytes) {
        return;
    }
    {
        const std::lock_guard lock(cacheMutex_);
        if (cache_.contains(key)) {
            return;
        }
        lru_.push_front(key);
        cache_.emplace(key, CacheEntry{std::move(tile), lru_.begin()});
        stats_.bytes += bytes;
        stats_.entries = cache_.size();
        tiles::ResourceCoordinator::global().charge(tiles::Budget::Ram, bytes);
    }
    const std::uint64_t used = cacheStats().bytes;
    if (used > options_.cacheBytes) {
        evictCache(used - options_.cacheBytes);
    }
}

CpuEvaluator::CacheStats CpuEvaluator::cacheStats() const {
    const std::lock_guard lock(cacheMutex_);
    return stats_;
}

void CpuEvaluator::clearCache() {
    const std::lock_guard lock(cacheMutex_);
    tiles::ResourceCoordinator::global().release(tiles::Budget::Ram, stats_.bytes);
    cache_.clear();
    lru_.clear();
    stats_.bytes = 0;
    stats_.entries = 0;
}

base::Result<std::shared_ptr<const ImageBuffer>>
CpuEvaluator::renderTile(const GraphNode &root, tiles::TileCoord tile, const EvaluationContext &context) {
    const CacheKey key{root.cacheKey(tiles::tilePixelRect(tile)), tile, context.quality, context.time};
    {
        const std::lock_guard lock(cacheMutex_);
        if (const auto it = cache_.find(key); it != cache_.end()) {
            lru_.splice(lru_.begin(), lru_, it->second.lru);
            ++stats_.hits;
            return it->second.tile;
        }
        ++stats_.misses;
    }
    auto buffer = std::make_shared<ImageBuffer>(tiles::tilePixelRect(tile));
    if (root.bounds().intersects(buffer->region())) {
        if (auto status = root.evaluate(context, *buffer); !status) {
            return base::unexpected(std::move(status).error());
        }
    }
    evaluated_.fetch_add(1, std::memory_order_relaxed);
    std::shared_ptr<const ImageBuffer> result = std::move(buffer);
    insertCache(key, result);
    return result;
}

std::vector<base::Result<std::shared_ptr<const ImageBuffer>>>
CpuEvaluator::renderTiles(const GraphNode &root, std::span<const tiles::TileCoord> coords,
                          const EvaluationContext &context) {
    std::vector<base::Result<std::shared_ptr<const ImageBuffer>>> results;
    results.reserve(coords.size());
    for (std::size_t i = 0; i < coords.size(); ++i) {
        results.emplace_back(
            base::fail(ErrorCategory::Internal, "render.not_run", "The tile was not rendered"));
    }
    std::latch done(static_cast<std::ptrdiff_t>(coords.size()));
    for (std::size_t i = 0; i < coords.size(); ++i) {
        post([&, i] {
            results[i] = renderTile(root, coords[i], context);
            done.count_down();
        });
    }
    done.wait();
    return results;
}

base::Result<RenderResult> CpuEvaluator::render(const document::DocumentState &document,
                                                const RenderRequest &request, std::stop_token stop) {
    const std::shared_ptr<const GraphNode> root = compileGraph(document);
    return renderGraph(*root, document.colorPolicy, request, stop);
}

base::Result<RenderResult> CpuEvaluator::renderGraph(const GraphNode &root, const color::ColorPolicy &policy,
                                                     const RenderRequest &request, std::stop_token stop) {
    const RenderDestination &destination = request.destination;
    if (request.region.empty()) {
        return base::fail(ErrorCategory::Validation, "render.empty_region",
                          "Nothing to render: the region is empty");
    }
    if (destination.sampleType != tiles::SampleType::UInt8 &&
        destination.sampleType != tiles::SampleType::Float32) {
        return base::fail(ErrorCategory::UnsupportedInput, "render.precision_unsupported",
                          "Only 8-bit and 32-bit float output is supported in this version",
                          std::string(tiles::sampleTypeName(destination.sampleType)));
    }
    const auto area = request.region.checkedArea();
    const auto bytes = area ? base::checkedByteCount(*area, 4 * tiles::bytesPerSample(destination.sampleType))
                            : std::nullopt;
    if (!bytes) {
        return base::fail(ErrorCategory::ResourceExhausted, "render.too_large",
                          "The requested image is too large", "region size overflows");
    }
    if (!tiles::ResourceCoordinator::global().makeRoom(tiles::Budget::Ram, *bytes)) {
        return base::fail(ErrorCategory::ResourceExhausted, "render.memory",
                          "Not enough memory to render this image",
                          std::to_string(*bytes) + " bytes requested");
    }

    std::shared_ptr<const color::ColorTransform> conversion = color::identityTransform();
    if (destination.profile && policy.profile && !destination.profile->sameAs(*policy.profile)) {
        auto transform = color::iccTransform(*policy.profile, *destination.profile,
                                             color::RenderingIntent::RelativeColorimetric, true);
        if (!transform) {
            return base::unexpected(std::move(transform).error());
        }
        conversion = *transform;
    }

    RenderResult result;
    result.region = request.region;
    result.sampleType = destination.sampleType;
    result.alpha = destination.alpha;
    result.pixels.resize(*bytes);

    std::atomic<std::uint64_t> nonFinite{0};
    const EvaluationContext context{request.time, request.quality, stop, &nonFinite};
    const base::IRect range = tiles::tileRangeCovering(request.region);
    std::vector<tiles::TileCoord> coords;
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            coords.push_back({tx, ty});
        }
    }

    std::mutex errorMutex;
    std::optional<base::Error> firstError;
    std::latch done(static_cast<std::ptrdiff_t>(coords.size()));
    const std::size_t sampleBytes = tiles::bytesPerSample(destination.sampleType);
    for (const tiles::TileCoord coord : coords) {
        post([&, coord] {
            const auto finish = [&done] { done.count_down(); };
            if (stop.stop_requested()) {
                const std::lock_guard lock(errorMutex);
                if (!firstError) {
                    firstError = base::Error{
                        ErrorCategory::Cancelled, "render.cancelled", "Rendering was cancelled", {}};
                }
                finish();
                return;
            }
            auto tile = renderTile(root, coord, context);
            if (!tile) {
                const std::lock_guard lock(errorMutex);
                if (!firstError) {
                    firstError = std::move(tile).error();
                }
                finish();
                return;
            }
            const base::IRect part = (*tile)->region().intersected(request.region);
            std::vector<float> row(static_cast<std::size_t>(part.width()) * 4);
            for (std::int64_t y = part.y0; y < part.y1; ++y) {
                std::memcpy(row.data(), (*tile)->pixel(part.x0, y), row.size() * sizeof(float));
                if (destination.alpha == AlphaAssociation::Straight || !conversion->isIdentity()) {
                    unpremultiplyRow(row);
                }
                if (!conversion->isIdentity()) {
                    conversion->apply(row);
                    if (destination.alpha == AlphaAssociation::Premultiplied) {
                        for (std::size_t i = 0; i < row.size(); i += 4) {
                            row[i] *= row[i + 3];
                            row[i + 1] *= row[i + 3];
                            row[i + 2] *= row[i + 3];
                        }
                    }
                }
                std::byte *out = result.pixels.data() +
                                 static_cast<std::size_t>(y - request.region.y0) * result.stride() +
                                 static_cast<std::size_t>(part.x0 - request.region.x0) * 4 * sampleBytes;
                for (std::size_t i = 0; i < row.size(); ++i) {
                    tiles::storeSample(destination.sampleType, row[i], out + i * sampleBytes);
                }
            }
            finish();
        });
    }
    done.wait();
    if (firstError) {
        return base::unexpected(std::move(*firstError));
    }
    result.nonFiniteSamples = nonFinite.load();
    return result;
}

} // namespace archmage::render
