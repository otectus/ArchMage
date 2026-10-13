#include "archmage/ui/canvas/SyntheticFrameSource.h"

#include <QImage>

#include <algorithm>
#include <climits>
#include <cmath>
#include <vector>

namespace archmage::ui::canvas {
namespace {

struct Rgba {
    std::uint32_t r, g, b, a; // premultiplied, 0..255
};

constexpr std::uint32_t pack(Rgba c) { return c.r | (c.g << 8u) | (c.b << 16u) | (c.a << 24u); }

Rgba tileColor(std::int64_t tx, std::int64_t ty) {
    const std::int64_t index = tx * 7 + ty * 13;
    const auto h = static_cast<double>(((index % 360) + 360) % 360);
    // HSV with s = 0.65, v = 0.9 to RGB.
    const double c = 0.9 * 0.65;
    const double hp = h / 60.0;
    const double x = c * (1.0 - std::abs(std::fmod(hp, 2.0) - 1.0));
    double r = 0, g = 0, b = 0;
    if (hp < 1) {
        r = c, g = x;
    } else if (hp < 2) {
        r = x, g = c;
    } else if (hp < 3) {
        g = c, b = x;
    } else if (hp < 4) {
        g = x, b = c;
    } else if (hp < 5) {
        r = x, b = c;
    } else {
        r = c, b = x;
    }
    const double m = 0.9 - c;
    const auto to8 = [](double v) {
        return static_cast<std::uint32_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
    };
    std::uint32_t alpha = 255;
    const std::int64_t kind = ((tx + ty * 3) % 15 + 15) % 15;
    if (kind % 3 == 0) {
        alpha = 0;
    } else if (kind % 5 == 0) {
        alpha = 128;
    }
    const auto premultiply = [alpha](std::uint32_t v) { return (v * alpha + 127) / 255; };
    return {premultiply(to8(r + m)), premultiply(to8(g + m)), premultiply(to8(b + m)), alpha};
}

std::int64_t floorDiv(std::int64_t a, std::int64_t b) {
    const std::int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

} // namespace

SyntheticFrameSource::SyntheticFrameSource(QSize documentSize, int tileSize)
    : documentSize_(documentSize), tileSize_(std::max(1, tileSize)) {}

void SyntheticFrameSource::renderFrame(const render::ViewTransform &view, QImage &frame) {
    const int width = frame.width();
    const int height = frame.height();
    const double scale = view.scale();
    const render::DPoint pan = view.pan();
    const double checkCell = 8.0 * view.devicePixelRatio();
    const std::int64_t docW = documentSize_.width();
    const std::int64_t docH = documentSize_.height();

    // Document pixel sampled by each device column (nearest, at the device pixel center).
    std::vector<std::int64_t> columnPixel(static_cast<std::size_t>(width));
    for (int x = 0; x < width; ++x) {
        columnPixel[static_cast<std::size_t>(x)] =
            static_cast<std::int64_t>(std::floor((static_cast<double>(x) + 0.5 - pan.x) / scale));
    }

    for (int y = 0; y < height; ++y) {
        auto *row = reinterpret_cast<std::uint32_t *>(frame.scanLine(y));
        const auto py = static_cast<std::int64_t>(std::floor((static_cast<double>(y) + 0.5 - pan.y) / scale));
        const bool rowInside = py >= 0 && py < docH;
        const std::int64_t ty = floorDiv(py, tileSize_);
        const bool rowGrid = py - ty * tileSize_ == 0;
        const auto checkRow = static_cast<std::int64_t>(std::floor(y / checkCell));
        std::int64_t cachedTile = INT64_MIN;
        Rgba cachedColor{};
        for (int x = 0; x < width; ++x) {
            const std::int64_t px = columnPixel[static_cast<std::size_t>(x)];
            if (!rowInside || px < 0 || px >= docW) {
                row[x] = kPasteboard;
                continue;
            }
            if ((px < 24 && py < 4) || (px < 4 && py < 24)) {
                row[x] = 0xffffffffu; // origin marker
                continue;
            }
            const std::int64_t tx = floorDiv(px, tileSize_);
            const bool grid = rowGrid || px - tx * tileSize_ == 0;
            const bool lightCheck =
                ((static_cast<std::int64_t>(std::floor(x / checkCell)) + checkRow) & 1) == 0;
            const std::uint32_t check = lightCheck ? 0xcc : 0x99;
            if (grid) {
                row[x] = pack({0x20, 0x20, 0x20, 0xff});
                continue;
            }
            if (tx != cachedTile) {
                cachedColor = tileColor(tx, ty);
                cachedTile = tx;
            }
            const Rgba &c = cachedColor;
            const std::uint32_t inverse = 255 - c.a;
            const auto over = [&](std::uint32_t channel) { return channel + (check * inverse + 127) / 255; };
            row[x] = pack({over(c.r), over(c.g), over(c.b), 255});
        }
    }
}

} // namespace archmage::ui::canvas
