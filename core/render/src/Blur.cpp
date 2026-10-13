#include "archmage/render/Blur.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace archmage::render {
namespace {

base::IRect grown(const base::IRect &rect, std::int64_t by) {
    if (rect.empty()) {
        return rect;
    }
    return {rect.x0 - by, rect.y0 - by, rect.x1 + by, rect.y1 + by};
}

base::Status cancelled() {
    return base::fail(base::ErrorCategory::Cancelled, "render.cancelled", "Rendering was cancelled");
}

} // namespace

GaussianBlurNode::GaussianBlurNode(std::shared_ptr<const GraphNode> input, double sigma)
    : inputs_{std::move(input)}, sigma_(std::clamp(sigma, 0.0, 1000.0)),
      radius_(static_cast<std::int64_t>(std::ceil(3.0 * sigma_))) {
    std::vector<double> weights(static_cast<std::size_t>(2 * radius_ + 1), 1.0);
    if (radius_ > 0) {
        double sum = 0.0;
        for (std::int64_t k = -radius_; k <= radius_; ++k) {
            const double w = std::exp(-static_cast<double>(k * k) / (2.0 * sigma_ * sigma_));
            weights[static_cast<std::size_t>(k + radius_)] = w;
            sum += w;
        }
        for (double &w : weights) {
            w /= sum;
        }
    }
    weights_.assign(weights.begin(), weights.end());
    bounds_ = grown(inputs_.front()->bounds(), radius_);
}

base::IRect GaussianBlurNode::requiredInputRegion(const base::IRect &output, std::size_t) const {
    return grown(output, radius_);
}

std::uint64_t GaussianBlurNode::cacheKey(const base::IRect &region) const {
    KeyBuilder key;
    key.add(typeId()).add(static_cast<std::uint64_t>(algorithmVersion())).add(sigma_);
    key.add(inputs_.front()->cacheKey(requiredInputRegion(region, 0)));
    return key.finish();
}

base::Status GaussianBlurNode::evaluate(const EvaluationContext &context, ImageBuffer &out) const {
    const base::IRect &region = out.region();
    if (region.empty() || !bounds_.intersects(region)) {
        return {};
    }
    const GraphNode &input = *inputs_.front();
    if (radius_ == 0) {
        return input.evaluate(context, out);
    }
    ImageBuffer source(requiredInputRegion(region, 0));
    if (auto status = input.evaluate(context, source); !status) {
        return status;
    }
    // Horizontal pass over every source row the vertical pass needs.
    ImageBuffer horizontal(base::IRect{region.x0, source.region().y0, region.x1, source.region().y1});
    const std::size_t taps = weights_.size();
    for (std::int64_t y = source.region().y0; y < source.region().y1; ++y) {
        if (context.stop.stop_requested()) {
            return cancelled();
        }
        const std::span<const float> in = source.row(y);
        const std::span<float> row = horizontal.row(y);
        for (std::int64_t x = region.x0; x < region.x1; ++x) {
            // Source column of tap 0 (x - radius) relative to the source row's first pixel.
            const auto first = static_cast<std::size_t>(x - region.x0); // source.x0 = region.x0 - radius
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t k = 0; k < taps; ++k) {
                const float w = weights_[k];
                const float *p = in.data() + (first + k) * 4;
                acc[0] += w * p[0];
                acc[1] += w * p[1];
                acc[2] += w * p[2];
                acc[3] += w * p[3];
            }
            float *o = row.data() + static_cast<std::size_t>(x - region.x0) * 4;
            o[0] = acc[0];
            o[1] = acc[1];
            o[2] = acc[2];
            o[3] = acc[3];
        }
    }
    // Vertical pass into the output.
    for (std::int64_t y = region.y0; y < region.y1; ++y) {
        if (context.stop.stop_requested()) {
            return cancelled();
        }
        const std::span<float> row = out.row(y);
        for (std::int64_t x = region.x0; x < region.x1; ++x) {
            const auto column = static_cast<std::size_t>(x - region.x0) * 4;
            float acc[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            for (std::size_t k = 0; k < taps; ++k) {
                const float w = weights_[k];
                const float *p = horizontal.row(y - radius_ + static_cast<std::int64_t>(k)).data() + column;
                acc[0] += w * p[0];
                acc[1] += w * p[1];
                acc[2] += w * p[2];
                acc[3] += w * p[3];
            }
            float *o = row.data() + column;
            o[0] = acc[0];
            o[1] = acc[1];
            o[2] = acc[2];
            o[3] = acc[3];
        }
    }
    return {};
}

} // namespace archmage::render
