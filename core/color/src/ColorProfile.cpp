#include "archmage/color/ColorProfile.h"

#include <lcms2.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace archmage::color {
namespace {

ColorModel modelOf(cmsColorSpaceSignature signature) {
    switch (signature) {
    case cmsSigRgbData:
        return ColorModel::Rgb;
    case cmsSigGrayData:
        return ColorModel::Gray;
    case cmsSigCmykData:
        return ColorModel::Cmyk;
    case cmsSigLabData:
        return ColorModel::Lab;
    default:
        return ColorModel::Other;
    }
}

std::string descriptionOf(cmsHPROFILE profile) {
    std::array<char, 256> text{};
    const cmsUInt32Number n = cmsGetProfileInfoASCII(profile, cmsInfoDescription, "en", "US", text.data(),
                                                     static_cast<cmsUInt32Number>(text.size()));
    return n > 0 ? std::string(text.data()) : std::string("(no description)");
}

// The ICC header's creation date (bytes 24-35, six big-endian uint16) is fixed so the generated profile
// is reproducible; Little CMS stamps the current time otherwise.
void fixCreationDate(std::vector<std::byte> &bytes) {
    const std::array<std::uint16_t, 6> date{2026, 10, 11, 0, 0, 0};
    for (std::size_t i = 0; i < date.size(); ++i) {
        bytes[24 + 2 * i] = static_cast<std::byte>(date[i] >> 8);
        bytes[25 + 2 * i] = static_cast<std::byte>(date[i] & 0xffu);
    }
}

} // namespace

base::Result<std::shared_ptr<const ColorProfile>> ColorProfile::fromIcc(std::span<const std::byte> bytes) {
    if (bytes.size() < 128) {
        return base::fail(base::ErrorCategory::DamagedInput, "icc.too_short", "The color profile is damaged",
                          std::to_string(bytes.size()) + " bytes is shorter than an ICC header");
    }
    cmsHPROFILE handle = cmsOpenProfileFromMem(bytes.data(), static_cast<cmsUInt32Number>(bytes.size()));
    if (handle == nullptr) {
        return base::fail(base::ErrorCategory::DamagedInput, "icc.invalid", "The color profile is damaged",
                          "Little CMS could not parse the ICC data");
    }
    auto profile = std::shared_ptr<ColorProfile>(new ColorProfile());
    profile->bytes_.assign(bytes.begin(), bytes.end());
    profile->hash_ = base::Sha256::of(bytes);
    profile->description_ = descriptionOf(handle);
    profile->model_ = modelOf(cmsGetColorSpace(handle));
    cmsCloseProfile(handle);
    return std::shared_ptr<const ColorProfile>(std::move(profile));
}

std::shared_ptr<const ColorProfile> ColorProfile::srgb() {
    static const std::shared_ptr<const ColorProfile> instance = [] {
        cmsHPROFILE handle = cmsCreate_sRGBProfile();
        cmsUInt32Number size = 0;
        cmsSaveProfileToMem(handle, nullptr, &size);
        std::vector<std::byte> bytes(size);
        cmsSaveProfileToMem(handle, bytes.data(), &size);
        cmsCloseProfile(handle);
        fixCreationDate(bytes);
        auto parsed = fromIcc(bytes);
        if (!parsed) {
            std::fprintf(stderr, "archmage: the generated sRGB profile did not parse: %s\n",
                         base::format(parsed.error()).c_str());
            std::abort();
        }
        return *parsed;
    }();
    return instance;
}

ColorPolicy ColorPolicy::defaultPolicy() { return {ColorProfile::srgb(), BlendPolicy::Encoded}; }

base::Result<ColorPolicy> ColorPolicy::make(std::shared_ptr<const ColorProfile> profile,
                                            BlendPolicy blendPolicy) {
    if (!profile) {
        return base::fail(base::ErrorCategory::Validation, "color.no_profile",
                          "A document needs a color profile");
    }
    if (profile->model() != ColorModel::Rgb) {
        return base::fail(base::ErrorCategory::UnsupportedInput, "color.mode_unsupported",
                          "Only RGB documents are supported in this version",
                          "profile color space is not RGB: " + profile->description());
    }
    if (blendPolicy == BlendPolicy::Linear) {
        return base::fail(base::ErrorCategory::UnsupportedInput, "color.linear_blending_unsupported",
                          "Linear-light blending is not available yet", "ADR-0009: implemented in M2");
    }
    return ColorPolicy{std::move(profile), blendPolicy};
}

std::string_view blendPolicyName(BlendPolicy policy) {
    return policy == BlendPolicy::Encoded ? "encoded" : "linear";
}

std::optional<BlendPolicy> blendPolicyFromName(std::string_view name) {
    if (name == "encoded") {
        return BlendPolicy::Encoded;
    }
    if (name == "linear") {
        return BlendPolicy::Linear;
    }
    return std::nullopt;
}

} // namespace archmage::color
