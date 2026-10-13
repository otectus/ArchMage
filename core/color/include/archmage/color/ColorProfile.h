// ICC profiles and document color policy (D-10, spec §8.2).
//
// A ColorProfile owns the profile's ICC bytes and the facts ArchMage reads from them; documents refer to
// profiles by SHA-256 (D-19). The built-in sRGB profile is generated with Little CMS and normalized so
// its bytes, and therefore its hash, are stable for a given Little CMS version.
//
// Thread safety: ColorProfile and ColorPolicy are immutable after construction and safe to share.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Sha256.h"

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace archmage::color {

enum class ColorModel { Rgb, Gray, Cmyk, Lab, Other };
enum class BlendPolicy { Encoded, Linear };

class ColorProfile {
public:
    /// Parses and validates ICC bytes; DamagedInput when Little CMS rejects them.
    [[nodiscard]] static base::Result<std::shared_ptr<const ColorProfile>>
    fromIcc(std::span<const std::byte> bytes);
    /// The built-in sRGB profile (Little CMS cmsCreate_sRGBProfile, creation date fixed at 2026-10-11).
    [[nodiscard]] static std::shared_ptr<const ColorProfile> srgb();

    /// CIE xy chromaticity.
    struct Chromaticity {
        double x = 0.0;
        double y = 0.0;
    };
    /// A matrix/TRC RGB profile from primaries, white point and a pure gamma transfer curve, built with
    /// Little CMS (creation date fixed, so the bytes are reproducible).
    [[nodiscard]] static base::Result<std::shared_ptr<const ColorProfile>>
    fromPrimaries(const std::string &description, Chromaticity white, Chromaticity red, Chromaticity green,
                  Chromaticity blue, double gamma);

    [[nodiscard]] const std::vector<std::byte> &iccBytes() const { return bytes_; }
    [[nodiscard]] const base::Sha256Digest &hash() const { return hash_; }
    [[nodiscard]] const std::string &description() const { return description_; }
    [[nodiscard]] ColorModel model() const { return model_; }
    [[nodiscard]] bool sameAs(const ColorProfile &other) const { return hash_ == other.hash_; }

private:
    ColorProfile() = default;

    std::vector<std::byte> bytes_;
    base::Sha256Digest hash_;
    std::string description_;
    ColorModel model_ = ColorModel::Other;
};

/// How a document's samples are interpreted and blended (D-09, D-10).
struct ColorPolicy {
    std::shared_ptr<const ColorProfile> profile;
    BlendPolicy blendPolicy = BlendPolicy::Encoded;

    /// sRGB, encoded blending: the default for new documents.
    [[nodiscard]] static ColorPolicy defaultPolicy();
    /// Validates a policy: an RGB profile is required, and Stage 1 rejects linear blending (ADR-0009).
    [[nodiscard]] static base::Result<ColorPolicy> make(std::shared_ptr<const ColorProfile> profile,
                                                        BlendPolicy blendPolicy);
};

[[nodiscard]] std::string_view blendPolicyName(BlendPolicy policy);
[[nodiscard]] std::optional<BlendPolicy> blendPolicyFromName(std::string_view name);

} // namespace archmage::color
