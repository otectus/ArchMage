#pragma once

#include "archmage/render/ViewTransform.h"

class QImage;

namespace archmage::ui::canvas {

/// Produces the pixels a canvas surface presents.
///
/// Ownership: not owned by the surface; it must outlive every surface it is attached to.
/// Thread safety: called on the GUI thread only.
class FrameSource {
public:
    virtual ~FrameSource() = default;

    /// Renders the region visible through `view` into `frame`, which covers the whole surface in device
    /// pixels and uses QImage::Format_RGBA8888_Premultiplied. Every pixel must be written.
    virtual void renderFrame(const render::ViewTransform &view, QImage &frame) = 0;
};

} // namespace archmage::ui::canvas
