// S1-2.9 (D-10): profiles, the document color policy and the named pipeline.

#include "archmage/color/ColorPipeline.h"
#include "archmage/color/ColorProfile.h"

#include <gtest/gtest.h>
#include <lcms2.h>

#include <cmath>
#include <vector>

namespace archmage::color {
namespace {

// Pinned for Little CMS 2.19 (manifest entry lcms2). A different hash means the generated profile
// changed, so documents written before the change would refer to a different sRGB resource.
constexpr const char *kPinnedSrgbHash = "de05479fb002967858d579f9aced8b6c6004e8542c8406fdf26e2d4ef304f656";

std::shared_ptr<const ColorProfile> wideGamutProfile() {
    // Adobe RGB (1998) primaries, D65 white, gamma 563/256 (spec §8.2 wide-gamut choice; built here, not
    // copied from a vendor file, and named neutrally because "Adobe RGB" is a trademark).
    const cmsCIExyY white{0.3127, 0.3290, 1.0};
    const cmsCIExyYTRIPLE primaries{{0.6400, 0.3300, 1.0}, {0.2100, 0.7100, 1.0}, {0.1500, 0.0600, 1.0}};
    cmsToneCurve *gamma = cmsBuildGamma(nullptr, 563.0 / 256.0);
    cmsToneCurve *curves[3] = {gamma, gamma, gamma};
    cmsHPROFILE handle = cmsCreateRGBProfile(&white, &primaries, curves);
    cmsFreeToneCurve(gamma);
    cmsUInt32Number size = 0;
    cmsSaveProfileToMem(handle, nullptr, &size);
    std::vector<std::byte> bytes(size);
    cmsSaveProfileToMem(handle, bytes.data(), &size);
    cmsCloseProfile(handle);
    return *ColorProfile::fromIcc(bytes);
}

TEST(ColorProfile, BuiltInSrgbIsStableAndPinned) {
    const auto srgb = ColorProfile::srgb();
    EXPECT_EQ(srgb, ColorProfile::srgb());
    EXPECT_EQ(srgb->model(), ColorModel::Rgb);
    EXPECT_NE(srgb->description().find("sRGB"), std::string::npos) << srgb->description();
    EXPECT_EQ(srgb->hash().hex(), kPinnedSrgbHash);
    const auto reparsed = ColorProfile::fromIcc(srgb->iccBytes());
    ASSERT_TRUE(reparsed);
    EXPECT_TRUE((*reparsed)->sameAs(*srgb));
}

TEST(ColorProfile, DamagedBytesAreRejected) {
    const std::vector<std::byte> tiny(16, std::byte{0});
    EXPECT_EQ(ColorProfile::fromIcc(tiny).error().code, "icc.too_short");
    std::vector<std::byte> garbage(400, std::byte{0x5a});
    const auto result = ColorProfile::fromIcc(garbage);
    ASSERT_FALSE(result);
    EXPECT_EQ(result.error().category, base::ErrorCategory::DamagedInput);
}

TEST(ColorPolicy, DefaultsAndStage1Restrictions) {
    const ColorPolicy policy = ColorPolicy::defaultPolicy();
    EXPECT_EQ(policy.blendPolicy, BlendPolicy::Encoded);
    EXPECT_TRUE(policy.profile->sameAs(*ColorProfile::srgb()));
    const auto linear = ColorPolicy::make(ColorProfile::srgb(), BlendPolicy::Linear);
    ASSERT_FALSE(linear);
    EXPECT_EQ(linear.error().code, "color.linear_blending_unsupported");
    EXPECT_FALSE(ColorPolicy::make(nullptr, BlendPolicy::Encoded));
    EXPECT_TRUE(ColorPolicy::make(wideGamutProfile(), BlendPolicy::Encoded));
}

TEST(ColorPipeline, SrgbDocumentPipelineIsIdentity) {
    const auto pipeline = ColorPipeline::forDocument(ColorPolicy::defaultPolicy());
    ASSERT_TRUE(pipeline);
    EXPECT_TRUE(pipeline->isIdentity());
    std::vector<float> pixels{0.1f, -0.2f, 1.7f, 0.5f, 0.0f, 0.0f, 0.0f, 0.0f};
    const std::vector<float> before = pixels;
    pipeline->apply(pixels, PipelineStage::Decode, PipelineStage::Display);
    EXPECT_EQ(pixels, before) << "identity stages do not touch samples, out-of-range ones included";
    EXPECT_EQ(stageName(PipelineStage::View), "view");
}

TEST(ColorPipeline, WideGamutDocumentsConvertForDisplayAndKeepAlpha) {
    const auto policy = ColorPolicy::make(wideGamutProfile(), BlendPolicy::Encoded);
    const auto pipeline = ColorPipeline::forDocument(*policy);
    ASSERT_TRUE(pipeline);
    EXPECT_FALSE(pipeline->isIdentity());
    std::vector<float> pixels{0.5f, 0.5f, 0.5f, 0.25f, 0.0f, 1.0f, 0.0f, 1.0f};
    pipeline->apply(pixels, PipelineStage::Decode, PipelineStage::Display);
    EXPECT_NEAR(pixels[0], pixels[1], 2e-3) << "a neutral stays neutral";
    EXPECT_NEAR(pixels[1], pixels[2], 2e-3);
    EXPECT_EQ(pixels[3], 0.25f) << "alpha passes through";
    EXPECT_LE(pixels[4], 1e-6f) << "saturated wide-gamut green lies outside sRGB (red goes to or below 0)";
    EXPECT_EQ(pixels[7], 1.0f);
}

} // namespace
} // namespace archmage::color
