// CPU reference kernels (D-09, D-12, spec §7.4, §7.5). The CPU path defines correctness; a GPU path must
// match it within the declared tolerances.
//
// Conventions: premultiplied rows hold r, g, b, a per pixel. Straight-to-premultiplied conversion
// sanitizes NaN and infinities to 0 and counts them; color samples outside [0, 1] are kept (they are
// clamped only when encoding to integers) while alpha, being coverage, is clamped to [0, 1]; a pixel with
// alpha 0 contributes nothing, while its stored color stays untouched in the layer's tiles.
//
// Thread safety: pure functions; counters are caller-owned.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>

namespace archmage::render {

/// Replaces a non-finite sample with 0 and counts it.
[[nodiscard]] inline float sanitizeSample(float value, std::uint64_t &nonFinite) {
    if (!std::isfinite(value)) {
        ++nonFinite;
        return 0.0f;
    }
    return value;
}

/// Spec §7.4 source-over on premultiplied pixels: Aout = As + Ad(1 − As), Cout = Cs + Cd(1 − As).
inline void sourceOver(const float *src, float *dst) {
    const float k = 1.0f - src[3];
    dst[0] = src[0] + dst[0] * k;
    dst[1] = src[1] + dst[1] * k;
    dst[2] = src[2] + dst[2] * k;
    dst[3] = src[3] + dst[3] * k;
}

/// Source-over of a whole premultiplied row onto another of the same length.
void sourceOverRow(std::span<const float> src, std::span<float> dst);

/// Straight planar samples of `count` pixels, scaled by `coverage` (layer opacity), composited with
/// source-over onto the premultiplied row `dst`.
void premultiplyOverRow(const float *r, const float *g, const float *b, const float *a, std::size_t count,
                        float coverage, float *dst, std::uint64_t &nonFinite);

/// One straight color (r, g, b, a) repeated `count` times, as above (uniform tiles).
void premultiplyOverUniform(float r, float g, float b, float a, std::size_t count, float coverage, float *dst,
                            std::uint64_t &nonFinite);

/// Premultiplied to straight, in place; alpha 0 gives color 0.
void unpremultiplyRow(std::span<float> rgba);

} // namespace archmage::render
