// GPU evaluation of the narrow slice (D-12, ADR-0012; spec §7.5; plan S1-6.2): the source-over composite
// of layer sources, computed by a SPIR-V compute shader compiled at build time, with the CPU reference's
// arithmetic so that results match it (uint8 exactly, float32 within 1e-6).
//
// Layer samples are gathered on the CPU, as the layer source node reads them (straight, converted to
// float), and the shader premultiplies and composites them per pixel. Any other graph runs on the CPU
// evaluator; so does everything once the device is lost (spec §7.5: cancel unsafe work, keep the document,
// continue on the CPU). Results come back as premultiplied tiles like CpuEvaluator::renderTiles; Stage 1
// uses that readback for comparison only.
//
// Thread safety: renderTiles() serializes on an internal mutex.
#pragma once

#include "archmage/gpu/GpuContext.h"
#include "archmage/render/CpuEvaluator.h"

#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace archmage::gpu {

class GpuEvaluator {
public:
    struct Stats {
        std::uint64_t gpuTiles = 0;
        std::uint64_t cpuTiles = 0;
        std::uint64_t batches = 0;
        bool deviceLost = false;
        double gatherMs = 0.0;   // CPU: reading layer samples into the upload buffers
        double dispatchMs = 0.0; // submit to fence: transfer and shader execution
        double readbackMs = 0.0; // CPU: copying results into tiles
    };

    GpuEvaluator(std::shared_ptr<GpuContext> context, render::CpuEvaluator &fallback);
    ~GpuEvaluator();
    GpuEvaluator(const GpuEvaluator &) = delete;
    GpuEvaluator &operator=(const GpuEvaluator &) = delete;

    /// Premultiplied output of `root` for each tile, in the order of `coords`.
    std::vector<base::Result<std::shared_ptr<const render::ImageBuffer>>>
    renderTiles(const render::GraphNode &root, std::span<const tiles::TileCoord> coords,
                const render::EvaluationContext &context);

    /// True when `root` is a composite of layer sources, or one layer source: what the GPU slice covers.
    [[nodiscard]] static bool supports(const render::GraphNode &root);
    [[nodiscard]] Stats stats() const;

    struct Pipeline;

private:
    std::shared_ptr<GpuContext> context_;
    render::CpuEvaluator &fallback_;
    std::unique_ptr<Pipeline> pipeline_;
    mutable std::mutex mutex_;
    Stats stats_;
};

} // namespace archmage::gpu
