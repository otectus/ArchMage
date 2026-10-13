// Gaussian blur (plan S1-6.3; spec §7.2 halo, §15.3): a bounded-neighborhood node whose required input
// region is its output grown by the kernel radius. Each output pixel is a fixed-order sum over the same
// input pixels however the output is tiled, so tiles evaluated separately join without seams.
//
// The kernel has radius ceil(3 sigma), weights exp(-k² / 2 sigma²) normalized in double precision; the
// blur is separable (horizontal, then vertical) on premultiplied samples, and transparent outside its
// input's bounds. CPU only in Stage 1.
//
// Thread safety: immutable after construction.
#pragma once

#include "archmage/render/Graph.h"

#include <memory>
#include <vector>

namespace archmage::render {

class GaussianBlurNode final : public GraphNode {
public:
    /// `sigma` in document pixels, [0, 1000]; 0 passes the input through.
    GaussianBlurNode(std::shared_ptr<const GraphNode> input, double sigma);

    [[nodiscard]] std::string_view typeId() const override { return "archmage.render.gaussianBlur"; }
    [[nodiscard]] int algorithmVersion() const override { return 1; }
    [[nodiscard]] OperationClass operationClass() const override { return OperationClass::BoundedNeighborhood; }
    [[nodiscard]] base::IRect bounds() const override { return bounds_; }
    [[nodiscard]] std::span<const std::shared_ptr<const GraphNode>> inputs() const override { return inputs_; }
    [[nodiscard]] base::IRect requiredInputRegion(const base::IRect &output, std::size_t index) const override;
    [[nodiscard]] std::int64_t halo() const override { return radius_; }
    [[nodiscard]] std::uint64_t cacheKey(const base::IRect &region) const override;
    base::Status evaluate(const EvaluationContext &context, ImageBuffer &out) const override;

    [[nodiscard]] double sigma() const { return sigma_; }
    [[nodiscard]] std::int64_t radius() const { return radius_; }
    [[nodiscard]] const std::vector<float> &weights() const { return weights_; }

private:
    std::vector<std::shared_ptr<const GraphNode>> inputs_;
    double sigma_;
    std::int64_t radius_;
    std::vector<float> weights_; // 2 radius + 1, summing to 1
    base::IRect bounds_;
};

} // namespace archmage::render
