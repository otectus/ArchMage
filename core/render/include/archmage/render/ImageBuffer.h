// Premultiplied RGBA float32 pixels covering a document rectangle: the working representation of the
// CPU evaluator (D-09, spec §7.4).
//
// Thread safety: a plain value; not synchronized.
#pragma once

#include "archmage/base/Geometry.h"

#include <cstddef>
#include <span>
#include <vector>

namespace archmage::render {

class ImageBuffer {
public:
    ImageBuffer() = default;
    /// Transparent pixels covering `region` (document coordinates).
    explicit ImageBuffer(const base::IRect &region)
        : region_(region),
          data_(static_cast<std::size_t>(region.width()) * static_cast<std::size_t>(region.height()) * 4,
                0.0f) {}

    [[nodiscard]] const base::IRect &region() const { return region_; }
    [[nodiscard]] std::int64_t width() const { return region_.width(); }
    [[nodiscard]] std::int64_t height() const { return region_.height(); }

    /// Row `y` in document coordinates: 4 floats (r, g, b, a premultiplied) per pixel.
    [[nodiscard]] std::span<float> row(std::int64_t y) {
        return {data_.data() + rowOffset(y), static_cast<std::size_t>(width()) * 4};
    }
    [[nodiscard]] std::span<const float> row(std::int64_t y) const {
        return {data_.data() + rowOffset(y), static_cast<std::size_t>(width()) * 4};
    }
    /// Pixel at document coordinates (x, y); must lie inside region().
    [[nodiscard]] float *pixel(std::int64_t x, std::int64_t y) {
        return data_.data() + rowOffset(y) + static_cast<std::size_t>(x - region_.x0) * 4;
    }
    [[nodiscard]] const float *pixel(std::int64_t x, std::int64_t y) const {
        return data_.data() + rowOffset(y) + static_cast<std::size_t>(x - region_.x0) * 4;
    }

    void clear() { std::fill(data_.begin(), data_.end(), 0.0f); }
    [[nodiscard]] std::size_t byteSize() const { return data_.size() * sizeof(float); }
    [[nodiscard]] const std::vector<float> &data() const { return data_; }

private:
    [[nodiscard]] std::size_t rowOffset(std::int64_t y) const {
        return static_cast<std::size_t>(y - region_.y0) * static_cast<std::size_t>(width()) * 4;
    }

    base::IRect region_;
    std::vector<float> data_;
};

} // namespace archmage::render
