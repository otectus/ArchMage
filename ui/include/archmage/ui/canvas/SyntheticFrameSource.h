#pragma once

#include "archmage/ui/canvas/FrameSource.h"

#include <QSize>

#include <cstdint>

namespace archmage::ui::canvas {

/// Deterministic test content for canvas experiments (S1-1.1): a document of `documentSize` pixels
/// divided into 128-pixel tiles, each with its own color, every third tile fully transparent and every
/// fifth half transparent, one-pixel grid lines on tile borders and a white marker at the document
/// origin, over a transparency checkerboard of 8 × 8 logical-pixel cells fixed to the view (NAV-04).
/// The pasteboard outside the document is a flat neutral gray.
///
/// Output depends only on the view transform and the frame size, so a presented frame can be compared
/// with a screenshot pixel for pixel.
class SyntheticFrameSource final : public FrameSource {
public:
    explicit SyntheticFrameSource(QSize documentSize = {4096, 4096}, int tileSize = 128);

    void renderFrame(const render::ViewTransform &view, QImage &frame) override;

    [[nodiscard]] QSize documentSize() const { return documentSize_; }

    static constexpr std::uint32_t kPasteboard = 0xff3c3c3cu; // premultiplied RGBA8888 as 0xAABBGGRR

private:
    QSize documentSize_;
    int tileSize_;
};

} // namespace archmage::ui::canvas
