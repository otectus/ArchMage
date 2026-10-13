// Integer and floating-point geometry shared by every core module (D-17, spec §6.3; ADR-0102).
//
// Conventions:
// - Integer extents and tile addresses are int64_t; rectangles are half-open: [x0, x1) × [y0, y1).
// - Document geometry is double precision.
// - Pixel (i, j) covers [i, i + 1) × [j, j + 1); its center is (i + 0.5, j + 0.5).
// - Negative coordinates are valid everywhere.
//
// Ownership and thread safety: plain value types with no shared state; safe to copy between threads.
#pragma once

#include "archmage/base/Checked.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>

namespace archmage::base {

struct IPoint {
    std::int64_t x = 0;
    std::int64_t y = 0;

    friend constexpr bool operator==(const IPoint &, const IPoint &) = default;
};

/// Half-open integer rectangle [x0, x1) × [y0, y1). Empty when x1 <= x0 or y1 <= y0.
struct IRect {
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;

    [[nodiscard]] constexpr std::int64_t width() const { return x1 > x0 ? x1 - x0 : 0; }
    [[nodiscard]] constexpr std::int64_t height() const { return y1 > y0 ? y1 - y0 : 0; }
    [[nodiscard]] constexpr bool empty() const { return x1 <= x0 || y1 <= y0; }
    [[nodiscard]] constexpr bool contains(IPoint p) const {
        return p.x >= x0 && p.x < x1 && p.y >= y0 && p.y < y1;
    }

    [[nodiscard]] constexpr bool contains(const IRect &o) const {
        return o.empty() || (o.x0 >= x0 && o.y0 >= y0 && o.x1 <= x1 && o.y1 <= y1);
    }
    [[nodiscard]] constexpr bool intersects(const IRect &o) const { return !intersected(o).empty(); }
    [[nodiscard]] constexpr IRect translated(std::int64_t dx, std::int64_t dy) const {
        return {x0 + dx, y0 + dy, x1 + dx, y1 + dy};
    }
    /// width × height, or nullopt when it does not fit in int64 (D-17 checked allocation arithmetic).
    [[nodiscard]] std::optional<std::int64_t> checkedArea() const;

    [[nodiscard]] constexpr IRect intersected(const IRect &o) const {
        IRect r{std::max(x0, o.x0), std::max(y0, o.y0), std::min(x1, o.x1), std::min(y1, o.y1)};
        return r.empty() ? IRect{} : r;
    }

    /// Smallest rectangle containing both; an empty operand contributes nothing.
    [[nodiscard]] constexpr IRect united(const IRect &o) const {
        if (empty()) {
            return o.empty() ? IRect{} : o;
        }
        if (o.empty()) {
            return *this;
        }
        return {std::min(x0, o.x0), std::min(y0, o.y0), std::max(x1, o.x1), std::max(y1, o.y1)};
    }

    friend constexpr bool operator==(const IRect &, const IRect &) = default;
};

struct DPoint {
    double x = 0.0;
    double y = 0.0;

    friend constexpr DPoint operator+(DPoint a, DPoint b) { return {a.x + b.x, a.y + b.y}; }
    friend constexpr DPoint operator-(DPoint a, DPoint b) { return {a.x - b.x, a.y - b.y}; }
    friend constexpr DPoint operator*(DPoint a, double s) { return {a.x * s, a.y * s}; }
    friend constexpr DPoint operator/(DPoint a, double s) { return {a.x / s, a.y / s}; }
    friend constexpr bool operator==(const DPoint &, const DPoint &) = default;
};

struct DSize {
    double width = 0.0;
    double height = 0.0;

    friend constexpr bool operator==(const DSize &, const DSize &) = default;
};

/// Half-open floating-point rectangle [x, x + width) × [y, y + height).
struct DRect {
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;

    [[nodiscard]] constexpr double right() const { return x + width; }
    [[nodiscard]] constexpr double bottom() const { return y + height; }
    [[nodiscard]] constexpr bool empty() const { return !(width > 0.0) || !(height > 0.0); }
    [[nodiscard]] constexpr DPoint center() const { return {x + width * 0.5, y + height * 0.5}; }

    /// Smallest integer rectangle covering every pixel this rectangle touches.
    [[nodiscard]] IRect coveringPixels() const {
        if (empty()) {
            return {};
        }
        return {static_cast<std::int64_t>(std::floor(x)), static_cast<std::int64_t>(std::floor(y)),
                static_cast<std::int64_t>(std::ceil(right())),
                static_cast<std::int64_t>(std::ceil(bottom()))};
    }

    friend constexpr bool operator==(const DRect &, const DRect &) = default;
};

inline std::optional<std::int64_t> IRect::checkedArea() const { return checkedMul(width(), height()); }

/// Pixel containing a document point under the half-open convention.
[[nodiscard]] inline IPoint pixelAt(DPoint p) {
    return {static_cast<std::int64_t>(std::floor(p.x)), static_cast<std::int64_t>(std::floor(p.y))};
}

[[nodiscard]] constexpr DPoint pixelCenter(IPoint p) {
    return {static_cast<double>(p.x) + 0.5, static_cast<double>(p.y) + 0.5};
}

} // namespace archmage::base
