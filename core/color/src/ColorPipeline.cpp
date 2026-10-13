#include "archmage/color/ColorPipeline.h"

#include <lcms2.h>

#include <cassert>

namespace archmage::color {
namespace {

class Identity final : public ColorTransform {
public:
    [[nodiscard]] bool isIdentity() const override { return true; }
    void apply(std::span<float>) const override {}
    [[nodiscard]] std::string describe() const override { return "identity"; }
};

class LcmsTransform final : public ColorTransform {
public:
    LcmsTransform(cmsHTRANSFORM transform, std::string description)
        : transform_(transform), description_(std::move(description)) {}
    ~LcmsTransform() override { cmsDeleteTransform(transform_); }
    LcmsTransform(const LcmsTransform &) = delete;
    LcmsTransform &operator=(const LcmsTransform &) = delete;

    [[nodiscard]] bool isIdentity() const override { return false; }
    void apply(std::span<float> rgba) const override {
        assert(rgba.size() % 4 == 0);
        cmsDoTransform(transform_, rgba.data(), rgba.data(), static_cast<cmsUInt32Number>(rgba.size() / 4));
    }
    [[nodiscard]] std::string describe() const override { return description_; }

private:
    cmsHTRANSFORM transform_;
    std::string description_;
};

cmsUInt32Number lcmsIntent(RenderingIntent intent) {
    switch (intent) {
    case RenderingIntent::Perceptual:
        return INTENT_PERCEPTUAL;
    case RenderingIntent::RelativeColorimetric:
        return INTENT_RELATIVE_COLORIMETRIC;
    case RenderingIntent::Saturation:
        return INTENT_SATURATION;
    case RenderingIntent::AbsoluteColorimetric:
        return INTENT_ABSOLUTE_COLORIMETRIC;
    }
    return INTENT_PERCEPTUAL;
}

} // namespace

std::string_view stageName(PipelineStage stage) {
    switch (stage) {
    case PipelineStage::Decode:
        return "decode";
    case PipelineStage::Working:
        return "working";
    case PipelineStage::Operations:
        return "operations";
    case PipelineStage::View:
        return "view";
    case PipelineStage::Display:
        return "display";
    }
    return "unknown";
}

std::shared_ptr<const ColorTransform> identityTransform() {
    static const auto identity = std::make_shared<const Identity>();
    return identity;
}

base::Result<std::shared_ptr<const ColorTransform>> iccTransform(const ColorProfile &from,
                                                                 const ColorProfile &to,
                                                                 RenderingIntent intent,
                                                                 bool blackPointCompensation) {
    if (from.sameAs(to)) {
        return identityTransform();
    }
    if (from.model() != ColorModel::Rgb || to.model() != ColorModel::Rgb) {
        return base::fail(base::ErrorCategory::UnsupportedInput, "color.transform_unsupported",
                          "Only RGB to RGB conversions are available in this version");
    }
    cmsHPROFILE source =
        cmsOpenProfileFromMem(from.iccBytes().data(), static_cast<cmsUInt32Number>(from.iccBytes().size()));
    cmsHPROFILE destination =
        cmsOpenProfileFromMem(to.iccBytes().data(), static_cast<cmsUInt32Number>(to.iccBytes().size()));
    cmsUInt32Number flags = cmsFLAGS_NOCACHE | cmsFLAGS_COPY_ALPHA;
    if (blackPointCompensation) {
        flags |= cmsFLAGS_BLACKPOINTCOMPENSATION;
    }
    cmsHTRANSFORM transform =
        source != nullptr && destination != nullptr
            ? cmsCreateTransform(source, TYPE_RGBA_FLT, destination, TYPE_RGBA_FLT, lcmsIntent(intent), flags)
            : nullptr;
    if (source != nullptr) {
        cmsCloseProfile(source);
    }
    if (destination != nullptr) {
        cmsCloseProfile(destination);
    }
    if (transform == nullptr) {
        return base::fail(base::ErrorCategory::UnsupportedInput, "color.transform_failed",
                          "These color profiles cannot be converted",
                          from.description() + " -> " + to.description());
    }
    return std::shared_ptr<const ColorTransform>(
        std::make_shared<LcmsTransform>(transform, from.description() + " -> " + to.description()));
}

ColorPipeline::ColorPipeline() { stages_.fill(identityTransform()); }

base::Result<ColorPipeline> ColorPipeline::forDocument(const ColorPolicy &policy) {
    ColorPipeline pipeline;
    auto display =
        iccTransform(*policy.profile, *ColorProfile::srgb(), RenderingIntent::RelativeColorimetric, true);
    if (!display) {
        return base::unexpected(display.error());
    }
    pipeline.setStage(PipelineStage::Display, *display);
    return pipeline;
}

void ColorPipeline::setStage(PipelineStage stage, std::shared_ptr<const ColorTransform> transform) {
    stages_[static_cast<std::size_t>(stage)] = transform ? std::move(transform) : identityTransform();
}

const ColorTransform &ColorPipeline::stage(PipelineStage stage) const {
    return *stages_[static_cast<std::size_t>(stage)];
}

bool ColorPipeline::isIdentity() const {
    for (const auto &stage : stages_) {
        if (!stage->isIdentity()) {
            return false;
        }
    }
    return true;
}

void ColorPipeline::apply(std::span<float> rgba, PipelineStage first, PipelineStage last) const {
    for (auto i = static_cast<std::size_t>(first); i <= static_cast<std::size_t>(last); ++i) {
        if (!stages_[i]->isIdentity()) {
            stages_[i]->apply(rgba);
        }
    }
}

} // namespace archmage::color
