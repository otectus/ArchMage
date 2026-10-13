#pragma once

#include "archmage/render/CpuEvaluator.h"
#include "archmage/ui/canvas/FrameSource.h"

#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace archmage::ui::canvas {

/// Presents a document state on a canvas surface (S1-3.5).
///
/// Visible document tiles come from the CPU evaluator and are converted once into 8-bit premultiplied
/// display tiles, cached by the graph's per-region key: after a stroke only the tiles whose sources
/// changed are evaluated again. Keys are content addresses: equal keys mean equal pixels, so every fully
/// white background tile shares one display tile. Each frame samples display tiles at device pixel
/// centers (nearest),
/// composites them over the transparency checkerboard (8 × 8 logical pixels, NAV-04) and fills the
/// pasteboard outside the canvas. The display is assumed to be sRGB (ADR-0010); zoomed-out views sample
/// full-resolution tiles (mip levels are later work).
///
/// Thread safety: GUI thread only.
class DocumentFrameSource final : public FrameSource {
public:
    struct Stats {
        std::uint64_t displayHits = 0;
        std::uint64_t displayMisses = 0;
        double lastFrameMs = 0.0;
    };

    explicit DocumentFrameSource(render::CpuEvaluator &evaluator,
                                 std::uint64_t displayCacheBytes = 128ull << 20);

    void setState(std::shared_ptr<const document::DocumentState> state);
    [[nodiscard]] const std::shared_ptr<const document::DocumentState> &state() const { return state_; }
    void renderFrame(const render::ViewTransform &view, QImage &frame) override;
    [[nodiscard]] const Stats &stats() const { return stats_; }

    static constexpr std::uint32_t kPasteboard = 0xff3c3c3cu;

private:
    using Pixels = std::shared_ptr<const std::vector<std::uint32_t>>; // kTileSize² premultiplied RGBA8
    struct DisplayTile {
        Pixels pixels;
        std::list<std::uint64_t>::iterator lru;
    };

    /// Stores a tile under its content key (tiles with equal keys have equal pixels, wherever they are).
    void insert(std::uint64_t key, Pixels pixels);

    render::CpuEvaluator &evaluator_;
    std::uint64_t capacityTiles_;
    std::shared_ptr<const document::DocumentState> state_;
    std::shared_ptr<const render::GraphNode> root_;
    std::unordered_map<std::uint64_t, DisplayTile> cache_;
    std::list<std::uint64_t> lru_;
    Stats stats_;
};

} // namespace archmage::ui::canvas
