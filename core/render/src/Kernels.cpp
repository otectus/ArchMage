#include "archmage/render/Kernels.h"

#include <algorithm>
#include <cassert>

namespace archmage::render {

void sourceOverRow(std::span<const float> src, std::span<float> dst) {
    assert(src.size() == dst.size() && src.size() % 4 == 0);
    for (std::size_t i = 0; i < src.size(); i += 4) {
        sourceOver(src.data() + i, dst.data() + i);
    }
}

void premultiplyOverRow(const float *r, const float *g, const float *b, const float *a, std::size_t count,
                        float coverage, float *dst, std::uint64_t &nonFinite) {
    for (std::size_t i = 0; i < count; ++i) {
        const float alpha = std::clamp(sanitizeSample(a[i], nonFinite), 0.0f, 1.0f) * coverage;
        if (alpha == 0.0f) {
            // Still count non-finite color hidden under zero alpha so diagnostics stay complete.
            (void)sanitizeSample(r[i], nonFinite);
            (void)sanitizeSample(g[i], nonFinite);
            (void)sanitizeSample(b[i], nonFinite);
            continue;
        }
        const float src[4] = {sanitizeSample(r[i], nonFinite) * alpha,
                              sanitizeSample(g[i], nonFinite) * alpha,
                              sanitizeSample(b[i], nonFinite) * alpha, alpha};
        sourceOver(src, dst + i * 4);
    }
}

void premultiplyOverUniform(float r, float g, float b, float a, std::size_t count, float coverage, float *dst,
                            std::uint64_t &nonFinite) {
    std::uint64_t perPixelNonFinite = 0;
    const float alpha = std::clamp(sanitizeSample(a, perPixelNonFinite), 0.0f, 1.0f) * coverage;
    const float src[4] = {sanitizeSample(r, perPixelNonFinite) * alpha,
                          sanitizeSample(g, perPixelNonFinite) * alpha,
                          sanitizeSample(b, perPixelNonFinite) * alpha, alpha};
    nonFinite += perPixelNonFinite * count;
    if (alpha == 0.0f) {
        return;
    }
    for (std::size_t i = 0; i < count; ++i) {
        sourceOver(src, dst + i * 4);
    }
}

void unpremultiplyRow(std::span<float> rgba) {
    assert(rgba.size() % 4 == 0);
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        const float alpha = rgba[i + 3];
        if (alpha == 0.0f) {
            rgba[i] = rgba[i + 1] = rgba[i + 2] = 0.0f;
            continue;
        }
        const float inverse = 1.0f / alpha;
        rgba[i] *= inverse;
        rgba[i + 1] *= inverse;
        rgba[i + 2] *= inverse;
    }
}

} // namespace archmage::render
