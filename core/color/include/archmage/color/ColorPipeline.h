// The named color pipeline (D-10, spec §8.3): decode → working → operations → view → display.
//
// Each stage transforms straight-alpha RGBA float pixels in place; alpha passes through unchanged. In
// Stage 1 the working space is the document profile, operations happen in it, the view stage is
// identity, and the display is assumed to be sRGB, so display is a conversion only for non-sRGB
// documents. ICC conversions use Little CMS float transforms.
//
// Thread safety: a built pipeline is immutable; apply() is safe from several threads at once (Little CMS
// transforms are created without a cache for that reason).
#pragma once

#include "archmage/base/Error.h"
#include "archmage/color/ColorProfile.h"

#include <array>
#include <memory>
#include <span>
#include <string_view>

namespace archmage::color {

enum class PipelineStage { Decode, Working, Operations, View, Display };
[[nodiscard]] std::string_view stageName(PipelineStage stage);

enum class RenderingIntent { Perceptual, RelativeColorimetric, Saturation, AbsoluteColorimetric };

class ColorTransform {
public:
    virtual ~ColorTransform() = default;
    [[nodiscard]] virtual bool isIdentity() const = 0;
    /// Transforms `rgba` (straight RGBA floats, 4 per pixel) in place.
    virtual void apply(std::span<float> rgba) const = 0;
    [[nodiscard]] virtual std::string describe() const = 0;
};

[[nodiscard]] std::shared_ptr<const ColorTransform> identityTransform();

/// ICC conversion between two RGB profiles; alpha is copied unchanged.
[[nodiscard]] base::Result<std::shared_ptr<const ColorTransform>> iccTransform(const ColorProfile &from,
                                                                               const ColorProfile &to,
                                                                               RenderingIntent intent,
                                                                               bool blackPointCompensation);

class ColorPipeline {
public:
    /// Every stage identity.
    ColorPipeline();

    /// Stage 1 pipeline for a document: identity up to the view, then working profile → sRGB display.
    [[nodiscard]] static base::Result<ColorPipeline> forDocument(const ColorPolicy &policy);

    void setStage(PipelineStage stage, std::shared_ptr<const ColorTransform> transform);
    [[nodiscard]] const ColorTransform &stage(PipelineStage stage) const;
    [[nodiscard]] bool isIdentity() const;
    /// Applies the stages from `first` to `last` inclusive, in order.
    void apply(std::span<float> rgba, PipelineStage first, PipelineStage last) const;

private:
    std::array<std::shared_ptr<const ColorTransform>, 5> stages_;
};

} // namespace archmage::color
