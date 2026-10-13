#include "archmage/ui/canvas/DocumentFrameSource.h"

#include "archmage/ui/canvas/LatencyTrace.h"

#include <QImage>

#include <algorithm>
#include <cmath>

namespace archmage::ui::canvas {
namespace {

std::uint32_t pack(std::uint32_t r, std::uint32_t g, std::uint32_t b, std::uint32_t a) {
    return r | (g << 8u) | (b << 16u) | (a << 24u);
}

std::uint32_t to8(float v) {
    return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 255.0f));
}

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

} // namespace

DocumentFrameSource::DocumentFrameSource(render::CpuEvaluator &evaluator, std::uint64_t displayCacheBytes)
    : evaluator_(evaluator),
      capacityTiles_(std::max<std::uint64_t>(64, displayCacheBytes / (tiles::kTilePixels * 4))) {}

void DocumentFrameSource::setState(std::shared_ptr<const document::DocumentState> state) {
    if (state == state_) {
        return;
    }
    state_ = std::move(state);
    root_ = state_ ? render::compileGraph(*state_) : nullptr;
}

void DocumentFrameSource::insert(std::uint64_t key, Pixels pixels) {
    if (const auto it = cache_.find(key); it != cache_.end()) {
        lru_.splice(lru_.begin(), lru_, it->second.lru);
        it->second.pixels = std::move(pixels);
        return;
    }
    lru_.push_front(key);
    cache_.emplace(key, DisplayTile{std::move(pixels), lru_.begin()});
    while (cache_.size() > capacityTiles_) {
        cache_.erase(lru_.back());
        lru_.pop_back();
    }
}

void DocumentFrameSource::renderFrame(const render::ViewTransform &view, QImage &frame) {
    const std::int64_t started = LatencyTrace::nowNs();
    frame.fill(QColor::fromRgba(0xff3c3c3c));
    if (!state_ || !root_) {
        return;
    }
    const int width = frame.width();
    const int height = frame.height();
    const double scale = view.scale();
    const render::DPoint pan = view.pan();
    const base::IRect canvas = state_->canvasRect();

    // Device pixels whose centers fall inside the canvas.
    const auto firstDevice = [&](double edge) { return static_cast<std::int64_t>(std::ceil(edge - 0.5)); };
    const base::IRect device{
        std::max<std::int64_t>(0, firstDevice(static_cast<double>(canvas.x0) * scale + pan.x)),
        std::max<std::int64_t>(0, firstDevice(static_cast<double>(canvas.y0) * scale + pan.y)),
        std::min<std::int64_t>(width, firstDevice(static_cast<double>(canvas.x1) * scale + pan.x)),
        std::min<std::int64_t>(height, firstDevice(static_cast<double>(canvas.y1) * scale + pan.y))};
    if (device.empty()) {
        return;
    }

    // Document pixel sampled by each device column and row (pixel centers, nearest).
    std::vector<std::int64_t> columns(static_cast<std::size_t>(device.width()));
    for (std::int64_t x = device.x0; x < device.x1; ++x) {
        columns[static_cast<std::size_t>(x - device.x0)] = std::clamp<std::int64_t>(
            static_cast<std::int64_t>(std::floor((static_cast<double>(x) + 0.5 - pan.x) / scale)), canvas.x0,
            canvas.x1 - 1);
    }
    const auto rowPixel = [&](std::int64_t y) {
        return std::clamp<std::int64_t>(
            static_cast<std::int64_t>(std::floor((static_cast<double>(y) + 0.5 - pan.y) / scale)), canvas.y0,
            canvas.y1 - 1);
    };
    const base::IRect pixels{columns.front(), rowPixel(device.y0), columns.back() + 1,
                             rowPixel(device.y1 - 1) + 1};
    const base::IRect range = tiles::tileRangeCovering(pixels);

    // Display tiles for the visible range; evaluate the missing ones in parallel, once per distinct key.
    // The frame holds its tiles by shared pointer, so cache eviction cannot free one while it is drawn.
    const auto columnsInRange = static_cast<std::size_t>(range.width());
    std::vector<Pixels> lookup(columnsInRange * static_cast<std::size_t>(range.height()));
    std::vector<tiles::TileCoord> missing;
    std::vector<std::uint64_t> missingKeys;
    std::unordered_map<std::uint64_t, std::vector<std::size_t>> waiting; // key -> slots needing it
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            const tiles::TileCoord coord{tx, ty};
            const std::uint64_t key = root_->cacheKey(tiles::tilePixelRect(coord));
            const std::size_t slot = static_cast<std::size_t>(ty - range.y0) * columnsInRange +
                                     static_cast<std::size_t>(tx - range.x0);
            if (const auto it = cache_.find(key); it != cache_.end()) {
                lru_.splice(lru_.begin(), lru_, it->second.lru);
                lookup[slot] = it->second.pixels;
                ++stats_.displayHits;
                continue;
            }
            auto &targets = waiting[key];
            if (targets.empty()) {
                missing.push_back(coord);
                missingKeys.push_back(key);
                ++stats_.displayMisses;
            }
            targets.push_back(slot);
        }
    }
    if (!missing.empty()) {
        const render::EvaluationContext context{};
        auto rendered = evaluator_.renderTiles(*root_, missing, context);
        for (std::size_t i = 0; i < missing.size(); ++i) {
            auto display = std::make_shared<std::vector<std::uint32_t>>(tiles::kTilePixels, 0u);
            if (rendered[i]) {
                const std::vector<float> &source = (*rendered[i])->data();
                for (std::size_t p = 0; p < tiles::kTilePixels; ++p) {
                    (*display)[p] = pack(to8(source[p * 4]), to8(source[p * 4 + 1]), to8(source[p * 4 + 2]),
                                         to8(source[p * 4 + 3]));
                }
            }
            for (const std::size_t slot : waiting[missingKeys[i]]) {
                lookup[slot] = display;
            }
            insert(missingKeys[i], std::move(display));
        }
    }

    // Composite over the checkerboard, fixed to the view (8 × 8 logical pixels).
    const double checkCell = 8.0 * view.devicePixelRatio();
    for (std::int64_t y = device.y0; y < device.y1; ++y) {
        auto *row = reinterpret_cast<std::uint32_t *>(frame.scanLine(static_cast<int>(y)));
        const std::int64_t py = rowPixel(y);
        const std::int64_t ty = floorDiv(py, tiles::kTileSize);
        const std::int64_t ly = py - ty * tiles::kTileSize;
        const std::size_t rowSlot = static_cast<std::size_t>(ty - range.y0) * columnsInRange;
        const auto checkRow = static_cast<std::int64_t>(std::floor(static_cast<double>(y) / checkCell));
        for (std::int64_t x = device.x0; x < device.x1; ++x) {
            const std::int64_t px = columns[static_cast<std::size_t>(x - device.x0)];
            const std::int64_t tx = floorDiv(px, tiles::kTileSize);
            const std::uint32_t *tile = lookup[rowSlot + static_cast<std::size_t>(tx - range.x0)]->data();
            const std::uint32_t source = tile[ly * tiles::kTileSize + (px - tx * tiles::kTileSize)];
            const std::uint32_t a = source >> 24u;
            if (a == 255u) {
                row[x] = source;
                continue;
            }
            const bool light =
                ((static_cast<std::int64_t>(std::floor(static_cast<double>(x) / checkCell)) + checkRow) &
                 1) == 0;
            const std::uint32_t check = (light ? 0xccu : 0x99u) * (255u - a);
            const auto channel = [&](std::uint32_t shift) {
                return ((source >> shift) & 0xffu) + (check + 127u) / 255u;
            };
            row[x] = pack(channel(0), channel(8), channel(16), 255u);
        }
    }
    stats_.lastFrameMs = static_cast<double>(LatencyTrace::nowNs() - started) / 1.0e6;
}

} // namespace archmage::ui::canvas
