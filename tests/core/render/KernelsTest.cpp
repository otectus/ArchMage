// S1-2.8: source-over and alpha conversions follow spec §7.4 and the D-09 policies.

#include "archmage/render/Kernels.h"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

namespace archmage::render {
namespace {

TEST(Kernels, OpaqueOverOpaqueIsTheSource) {
    float dst[4] = {0.2f, 0.4f, 0.6f, 1.0f};
    const float src[4] = {1.0f, 0.0f, 0.5f, 1.0f};
    sourceOver(src, dst);
    EXPECT_EQ(dst[0], 1.0f);
    EXPECT_EQ(dst[1], 0.0f);
    EXPECT_EQ(dst[2], 0.5f);
    EXPECT_EQ(dst[3], 1.0f);
}

TEST(Kernels, HalfAlphaOverTransparentAndOverOpaque) {
    float transparent[4] = {0, 0, 0, 0};
    const float halfRed[4] = {0.5f, 0.0f, 0.0f, 0.5f}; // premultiplied red at 50 %
    sourceOver(halfRed, transparent);
    EXPECT_EQ(transparent[0], 0.5f);
    EXPECT_EQ(transparent[3], 0.5f);
    float blue[4] = {0.0f, 0.0f, 1.0f, 1.0f};
    sourceOver(halfRed, blue);
    EXPECT_FLOAT_EQ(blue[0], 0.5f); // Cs + Cd(1 − As)
    EXPECT_FLOAT_EQ(blue[2], 0.5f);
    EXPECT_FLOAT_EQ(blue[3], 1.0f); // As + Ad(1 − As)
}

TEST(Kernels, ZeroAlphaContributesNothing) {
    std::vector<float> dst{0.1f, 0.2f, 0.3f, 0.4f};
    const float r = 1.0f, g = 1.0f, b = 1.0f, a = 0.0f; // hidden color under zero alpha
    std::uint64_t nonFinite = 0;
    premultiplyOverRow(&r, &g, &b, &a, 1, 1.0f, dst.data(), nonFinite);
    EXPECT_EQ(dst, (std::vector<float>{0.1f, 0.2f, 0.3f, 0.4f}));
    EXPECT_EQ(nonFinite, 0u);
}

TEST(Kernels, NonFiniteSamplesAreSanitizedAndCounted) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    const float r[2] = {nan, 0.5f}, g[2] = {0.5f, inf}, b[2] = {0.5f, 0.5f}, a[2] = {1.0f, 1.0f};
    std::vector<float> dst(8, 0.0f);
    std::uint64_t nonFinite = 0;
    premultiplyOverRow(r, g, b, a, 2, 1.0f, dst.data(), nonFinite);
    EXPECT_EQ(nonFinite, 2u);
    EXPECT_EQ(dst[0], 0.0f);
    EXPECT_EQ(dst[5], 0.0f);
    EXPECT_EQ(dst[1], 0.5f);
}

TEST(Kernels, NegativeColorKeptAlphaClamped) {
    const float r = -0.25f, g = 2.0f, b = 0.5f, a = 1.5f;
    std::vector<float> dst(4, 0.0f);
    std::uint64_t nonFinite = 0;
    premultiplyOverRow(&r, &g, &b, &a, 1, 0.5f, dst.data(), nonFinite);
    EXPECT_FLOAT_EQ(dst[3], 0.5f) << "alpha 1.5 clamps to 1, then opacity 0.5 scales it";
    EXPECT_FLOAT_EQ(dst[0], -0.125f) << "negative float color survives compositing (D-09)";
    EXPECT_FLOAT_EQ(dst[1], 1.0f);
}

TEST(Kernels, UniformMatchesPerPixel) {
    const float r[3] = {0.2f, 0.2f, 0.2f}, g[3] = {0.4f, 0.4f, 0.4f}, b[3] = {0.6f, 0.6f, 0.6f},
                a[3] = {0.7f, 0.7f, 0.7f};
    std::vector<float> perPixel(12, 0.25f), uniform(12, 0.25f);
    std::uint64_t n1 = 0, n2 = 0;
    premultiplyOverRow(r, g, b, a, 3, 0.8f, perPixel.data(), n1);
    premultiplyOverUniform(0.2f, 0.4f, 0.6f, 0.7f, 3, 0.8f, uniform.data(), n2);
    EXPECT_EQ(perPixel, uniform);
}

TEST(Kernels, UnpremultiplyRestoresStraightColor) {
    std::vector<float> row{0.25f, 0.125f, 0.0f, 0.5f, 0.3f, 0.3f, 0.3f, 0.0f};
    unpremultiplyRow(row);
    EXPECT_EQ(row, (std::vector<float>{0.5f, 0.25f, 0.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f}));
}

} // namespace
} // namespace archmage::render
