// S1-6.4 (M0-E2 profile conversion; spec §8.2, §8.5; ADR-0010): sRGB to a wide-gamut matrix/TRC profile built
// from the published 1998 wide-gamut primaries, with perceptual and relative colorimetric intents and black
// point compensation, on a neutral ramp and saturated patches; assigning a profile versus converting to it.
// Little CMS is checked against an independent double-precision computation. The printed table is the
// evidence in docs/decisions/evidence/S1-6/color.md.

#include "archmage/color/ColorPipeline.h"

#include <gtest/gtest.h>

#include <lcms2.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <vector>

namespace archmage::color {
namespace {

using Vec3 = std::array<double, 3>;
using Mat3 = std::array<Vec3, 3>;

constexpr double kWideGamma = 563.0 / 256.0; // 2.19921875, as the published 1998 wide-gamut space states

std::shared_ptr<const ColorProfile> wideProfile() {
    // A neutral name: the primaries are those published for Adobe RGB (1998), a trademark (corpus plan).
    auto profile = ColorProfile::fromPrimaries("ArchMage wide-gamut test RGB (1998 primaries, D65, gamma 563/256)",
                                               {0.3127, 0.3290}, {0.6400, 0.3300}, {0.2100, 0.7100},
                                               {0.1500, 0.0600}, kWideGamma);
    EXPECT_TRUE(profile) << base::format(profile.error());
    return profile ? *profile : nullptr;
}

Mat3 inverse(const Mat3 &m) {
    const double det = m[0][0] * (m[1][1] * m[2][2] - m[1][2] * m[2][1]) -
                       m[0][1] * (m[1][0] * m[2][2] - m[1][2] * m[2][0]) +
                       m[0][2] * (m[1][0] * m[2][1] - m[1][1] * m[2][0]);
    Mat3 r{};
    r[0][0] = (m[1][1] * m[2][2] - m[1][2] * m[2][1]) / det;
    r[0][1] = (m[0][2] * m[2][1] - m[0][1] * m[2][2]) / det;
    r[0][2] = (m[0][1] * m[1][2] - m[0][2] * m[1][1]) / det;
    r[1][0] = (m[1][2] * m[2][0] - m[1][0] * m[2][2]) / det;
    r[1][1] = (m[0][0] * m[2][2] - m[0][2] * m[2][0]) / det;
    r[1][2] = (m[0][2] * m[1][0] - m[0][0] * m[1][2]) / det;
    r[2][0] = (m[1][0] * m[2][1] - m[1][1] * m[2][0]) / det;
    r[2][1] = (m[0][1] * m[2][0] - m[0][0] * m[2][1]) / det;
    r[2][2] = (m[0][0] * m[1][1] - m[0][1] * m[1][0]) / det;
    return r;
}

Vec3 times(const Mat3 &m, const Vec3 &v) {
    return {m[0][0] * v[0] + m[0][1] * v[1] + m[0][2] * v[2], m[1][0] * v[0] + m[1][1] * v[1] + m[1][2] * v[2],
            m[2][0] * v[0] + m[2][1] * v[1] + m[2][2] * v[2]};
}

// RGB-to-XYZ matrix from xy primaries and white (both spaces share D65, so no adaptation is needed).
Mat3 rgbToXyz(Vec3 xr, Vec3 xg, Vec3 xb, Vec3 white) {
    const auto xyz = [](Vec3 c) { return Vec3{c[0] / c[1], 1.0, (1.0 - c[0] - c[1]) / c[1]}; };
    const Vec3 r = xyz(xr), g = xyz(xg), b = xyz(xb), w = xyz(white);
    const Mat3 primaries{{{r[0], g[0], b[0]}, {r[1], g[1], b[1]}, {r[2], g[2], b[2]}}};
    const Vec3 s = times(inverse(primaries), w);
    return {{{s[0] * r[0], s[1] * g[0], s[2] * b[0]}, {s[0] * r[1], s[1] * g[1], s[2] * b[1]},
             {s[0] * r[2], s[1] * g[2], s[2] * b[2]}}};
}

double srgbDecode(double c) { return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); }

// sRGB-encoded to wide-encoded, relative colorimetric, independently of Little CMS.
Vec3 reference(const Vec3 &srgb) {
    static const Mat3 fromSrgb =
        rgbToXyz({0.64, 0.33, 0}, {0.30, 0.60, 0}, {0.15, 0.06, 0}, {0.3127, 0.3290, 0});
    static const Mat3 toWide =
        inverse(rgbToXyz({0.64, 0.33, 0}, {0.21, 0.71, 0}, {0.15, 0.06, 0}, {0.3127, 0.3290, 0}));
    const Vec3 linear = times(toWide, times(fromSrgb, {srgbDecode(srgb[0]), srgbDecode(srgb[1]), srgbDecode(srgb[2])}));
    return {std::pow(std::max(linear[0], 0.0), 1.0 / kWideGamma), std::pow(std::max(linear[1], 0.0), 1.0 / kWideGamma),
            std::pow(std::max(linear[2], 0.0), 1.0 / kWideGamma)};
}

std::vector<float> convert(const ColorProfile &from, const ColorProfile &to, RenderingIntent intent, bool bpc,
                           std::vector<float> rgba) {
    auto transform = iccTransform(from, to, intent, bpc);
    EXPECT_TRUE(transform) << base::format(transform.error());
    if (transform) {
        (*transform)->apply(rgba);
    }
    return rgba;
}

// PCS XYZ (D50) of RGB samples interpreted in `profile`: what the samples look like.
Vec3 appearance(const ColorProfile &profile, const float *rgb) {
    cmsHPROFILE source = cmsOpenProfileFromMem(profile.iccBytes().data(),
                                               static_cast<cmsUInt32Number>(profile.iccBytes().size()));
    cmsHPROFILE xyz = cmsCreateXYZProfile();
    cmsHTRANSFORM transform = cmsCreateTransform(source, TYPE_RGB_DBL, xyz, TYPE_XYZ_DBL,
                                                 INTENT_RELATIVE_COLORIMETRIC, cmsFLAGS_NOCACHE);
    const double in[3] = {rgb[0], rgb[1], rgb[2]};
    cmsCIEXYZ out{};
    cmsDoTransform(transform, in, &out, 1);
    cmsDeleteTransform(transform);
    cmsCloseProfile(xyz);
    cmsCloseProfile(source);
    return {out.X, out.Y, out.Z};
}

struct Patch {
    const char *name;
    Vec3 rgb;
};

std::vector<Patch> patches() {
    std::vector<Patch> list;
    static const char *ramp[] = {"gray 0.0", "gray 0.1", "gray 0.2", "gray 0.3", "gray 0.4", "gray 0.5",
                                 "gray 0.6", "gray 0.7", "gray 0.8", "gray 0.9", "gray 1.0"};
    for (int k = 0; k <= 10; ++k) {
        const double v = k / 10.0;
        list.push_back({ramp[k], {v, v, v}});
    }
    list.push_back({"red", {1, 0, 0}});
    list.push_back({"green", {0, 1, 0}});
    list.push_back({"blue", {0, 0, 1}});
    list.push_back({"cyan", {0, 1, 1}});
    list.push_back({"magenta", {1, 0, 1}});
    list.push_back({"yellow", {1, 1, 0}});
    list.push_back({"orange", {1, 0.5, 0}});
    return list;
}

std::vector<float> rgbaOf(const std::vector<Patch> &list) {
    std::vector<float> rgba;
    for (const Patch &patch : list) {
        rgba.insert(rgba.end(), {static_cast<float>(patch.rgb[0]), static_cast<float>(patch.rgb[1]),
                                 static_cast<float>(patch.rgb[2]), 1.0f});
    }
    return rgba;
}

TEST(ColorExperiment, SrgbToWideGamutMatchesAnIndependentComputation) {
    const auto srgb = ColorProfile::srgb();
    const auto wide = wideProfile();
    ASSERT_TRUE(wide);
    const auto list = patches();
    const std::vector<float> input = rgbaOf(list);
    const auto relative = convert(*srgb, *wide, RenderingIntent::RelativeColorimetric, true, input);
    const auto perceptual = convert(*srgb, *wide, RenderingIntent::Perceptual, true, input);
    const auto noBpc = convert(*srgb, *wide, RenderingIntent::RelativeColorimetric, false, input);
    double worst = 0.0;       // encoded
    double worstLinear = 0.0; // in linear light
    double intentDifference = 0.0;
    double bpcDifference = 0.0;
    const auto linear = [](double encoded) { return std::pow(std::max(encoded, 0.0), kWideGamma); };
    std::printf("| Patch | sRGB | wide, relative + BPC (Little CMS) | wide, independent |\n|---|---|---|---|\n");
    for (std::size_t i = 0; i < list.size(); ++i) {
        const Vec3 expected = reference(list[i].rgb);
        std::printf("| %s | %.3f %.3f %.3f | %.6f %.6f %.6f | %.6f %.6f %.6f |\n", list[i].name, list[i].rgb[0],
                    list[i].rgb[1], list[i].rgb[2], relative[i * 4], relative[i * 4 + 1], relative[i * 4 + 2],
                    expected[0], expected[1], expected[2]);
        for (std::size_t c = 0; c < 3; ++c) {
            worst = std::max(worst, std::fabs(relative[i * 4 + c] - expected[c]));
            worstLinear = std::max(worstLinear, std::fabs(linear(relative[i * 4 + c]) - linear(expected[c])));
            intentDifference = std::max(intentDifference, static_cast<double>(std::fabs(relative[i * 4 + c] - perceptual[i * 4 + c])));
            bpcDifference = std::max(bpcDifference, static_cast<double>(std::fabs(relative[i * 4 + c] - noBpc[i * 4 + c])));
        }
        EXPECT_EQ(relative[i * 4 + 3], 1.0f) << "alpha passes through";
    }
    std::printf("max |Little CMS - independent|: %.3g encoded, %.3g linear; |perceptual - relative| = %.3g; "
                "|BPC - no BPC| = %.3g\n",
                worst, worstLinear, intentDifference, bpcDifference);
    RecordProperty("max_error_encoded", std::to_string(worst));
    RecordProperty("max_error_linear", std::to_string(worstLinear));
    // ICC stores colorant matrices as s15Fixed16 numbers (resolution 1.5e-5): channels that should be 0 come
    // out near 1e-5 in linear light, which the 1/2.2 power lifts to about 0.006 when encoded. Compare in
    // linear light, and bound the encoded difference loosely.
    EXPECT_LT(worstLinear, 1e-4);
    EXPECT_LT(worst, 1e-2);
    // Matrix/TRC profiles carry no perceptual tables: Little CMS uses the colorimetric path for both, and
    // both profiles have a zero black point, so black point compensation changes nothing.
    EXPECT_LT(intentDifference, 1e-6);
    EXPECT_LT(bpcDifference, 1e-6);
    // Neutrals stay neutral.
    for (std::size_t i = 0; i <= 10; ++i) {
        EXPECT_NEAR(relative[i * 4], relative[i * 4 + 1], 2e-4) << list[i].name;
        EXPECT_NEAR(relative[i * 4 + 1], relative[i * 4 + 2], 2e-4) << list[i].name;
    }
    // sRGB red has the same chromaticity as the wide red primary: only its level changes (219/255 in 8 bits).
    EXPECT_NEAR(relative[11 * 4], 219.0 / 255.0, 2e-3);
    EXPECT_LT(linear(relative[11 * 4 + 1]), 1e-4) << "green of red is 0 up to matrix quantization";
}

TEST(ColorExperiment, RoundTripsAndOutOfGamutColors) {
    const auto srgb = ColorProfile::srgb();
    const auto wide = wideProfile();
    ASSERT_TRUE(wide);
    const std::vector<float> input = rgbaOf(patches());
    const auto there = convert(*srgb, *wide, RenderingIntent::RelativeColorimetric, true, input);
    const auto back = convert(*wide, *srgb, RenderingIntent::RelativeColorimetric, true, there);
    double worst = 0.0;
    for (std::size_t i = 0; i < input.size(); ++i) {
        worst = std::max(worst, static_cast<double>(std::fabs(back[i] - input[i])));
    }
    std::printf("sRGB -> wide -> sRGB: max error %.3g\n", worst);
    RecordProperty("round_trip_error", std::to_string(worst));
    EXPECT_LT(worst, 1e-3);
    // The wide green primary lies outside sRGB: the float transform keeps it unbounded (D-09) rather than
    // clipping, so the out-of-range value survives in a float document. Below zero, Little CMS extends the
    // sRGB curve's linear segment (12.92 × linear), so the encoded red is strongly negative.
    const auto green = convert(*wide, *srgb, RenderingIntent::RelativeColorimetric, true, {0.0f, 1.0f, 0.0f, 1.0f});
    std::printf("wide green (0, 1, 0) in sRGB: %.6f %.6f %.6f\n", green[0], green[1], green[2]);
    EXPECT_LT(green[0], 0.0f) << "outside sRGB: a negative red";
}

// Assign keeps the samples and changes what they look like; convert changes the samples and keeps the look.
TEST(ColorExperiment, AssignVersusConvert) {
    const auto srgb = ColorProfile::srgb();
    const auto wide = wideProfile();
    ASSERT_TRUE(wide);
    std::printf("| Patch | look as sRGB (XYZ) | assigned wide: look (XYZ) | converted samples | converted look (XYZ) |\n"
                "|---|---|---|---|---|\n");
    for (const Patch &patch : patches()) {
        const std::vector<float> samples{static_cast<float>(patch.rgb[0]), static_cast<float>(patch.rgb[1]),
                                         static_cast<float>(patch.rgb[2]), 1.0f};
        const Vec3 original = appearance(*srgb, samples.data());
        // Assign: the same samples, now read as the wide profile.
        const Vec3 assigned = appearance(*wide, samples.data());
        // Convert: new samples that look the same.
        const auto converted = convert(*srgb, *wide, RenderingIntent::RelativeColorimetric, true, samples);
        const Vec3 kept = appearance(*wide, converted.data());
        std::printf("| %s | %.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f | %.4f %.4f %.4f |\n", patch.name,
                    original[0], original[1], original[2], assigned[0], assigned[1], assigned[2], converted[0],
                    converted[1], converted[2], kept[0], kept[1], kept[2]);
        for (std::size_t c = 0; c < 3; ++c) {
            EXPECT_NEAR(kept[c], original[c], 2e-4) << patch.name << ": converting preserves the look";
        }
        const bool saturated = patch.rgb[0] != patch.rgb[1] || patch.rgb[1] != patch.rgb[2];
        double moved = 0.0;
        for (std::size_t c = 0; c < 3; ++c) {
            moved = std::max(moved, std::fabs(assigned[c] - original[c]));
        }
        if (saturated) {
            EXPECT_GT(moved, 1e-2) << patch.name << ": assigning changes the look of saturated colors";
        } else if (patch.rgb[0] == 0.0 || patch.rgb[0] == 1.0) {
            EXPECT_LT(moved, 1e-4) << patch.name << ": white and black look the same in both";
        }
    }
}

} // namespace
} // namespace archmage::color
