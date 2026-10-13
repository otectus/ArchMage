// The one mapping between canvas surface pixels and document pixels (D-17, spec §6.3).
//
// Frames:
// - Logical: Qt logical pixels local to the canvas surface (what QMouseEvent::position() reports).
// - Device:  physical pixels local to the canvas surface; device = logical × devicePixelRatio.
//            Canvas frames are rendered in this frame, so it is also the spec's "view" frame.
// - Document: document pixels, double precision; device = document × scale + pan.
//
// `scale` counts device pixels per document pixel: 1.0 is "actual pixels" (100 %), so a document
// pixel maps to one physical pixel on every monitor. Changing the device pixel ratio keeps the scale
// and the document point under the viewport center (withDevicePixelRatio). Rotation and mirroring
// (NAV-02, later milestones) belong here too; callers never apply their own zoom or DPI correction.
//
// Ownership and thread safety: an immutable value type; safe to copy between threads.
#pragma once

#include "archmage/tiles/Geometry.h"

namespace archmage::render {

using tiles::DPoint;
using tiles::DRect;
using tiles::DSize;

class ViewTransform {
public:
    static constexpr double kMinScale = 1.0 / 1024.0; // about 0.1 %
    static constexpr double kMaxScale = 256.0;        // 25,600 %

    /// Identity: actual pixels, document origin at the surface origin, device pixel ratio 1.
    ViewTransform() = default;

    /// `scale` is clamped to [kMinScale, kMaxScale]; `devicePixelRatio` must be positive (1 otherwise).
    ViewTransform(double scale, DPoint pan, double devicePixelRatio);

    [[nodiscard]] double scale() const { return scale_; }
    [[nodiscard]] DPoint pan() const { return pan_; }
    [[nodiscard]] double devicePixelRatio() const { return dpr_; }

    [[nodiscard]] DPoint documentToDevice(DPoint p) const { return p * scale_ + pan_; }
    [[nodiscard]] DPoint deviceToDocument(DPoint p) const { return (p - pan_) / scale_; }
    [[nodiscard]] DPoint logicalToDevice(DPoint p) const { return p * dpr_; }
    [[nodiscard]] DPoint deviceToLogical(DPoint p) const { return p / dpr_; }
    [[nodiscard]] DPoint logicalToDocument(DPoint p) const { return deviceToDocument(logicalToDevice(p)); }
    [[nodiscard]] DPoint documentToLogical(DPoint p) const { return deviceToLogical(documentToDevice(p)); }

    [[nodiscard]] DRect documentToDevice(const DRect &r) const;
    [[nodiscard]] DRect deviceToDocument(const DRect &r) const;

    /// Document region visible in a viewport of `deviceSize` device pixels.
    [[nodiscard]] DRect visibleDocumentRect(DSize deviceSize) const;

    /// Same document point stays under `deviceAnchor`; the new scale is clamped.
    [[nodiscard]] ViewTransform zoomedAbout(double newScale, DPoint deviceAnchor) const;
    [[nodiscard]] ViewTransform panned(DPoint deviceDelta) const;

    /// New device pixel ratio for a viewport whose logical size is unchanged: keeps the scale and the
    /// document point at the viewport center. `logicalViewportSize` is the viewport in logical pixels.
    [[nodiscard]] ViewTransform withDevicePixelRatio(double newDpr, DSize logicalViewportSize) const;

    /// Largest scale (capped at 1, actual pixels) showing all of `document` inside `deviceViewport`
    /// with `marginDevice` pixels on every side, centered.
    [[nodiscard]] static ViewTransform fit(const DRect &document, DSize deviceViewport,
                                           double devicePixelRatio, double marginDevice = 0.0);

    /// Actual pixels (scale 1) with `documentPoint` at the viewport center.
    [[nodiscard]] static ViewTransform actualPixels(DPoint documentPoint, DSize deviceViewport,
                                                    double devicePixelRatio);

    friend bool operator==(const ViewTransform &, const ViewTransform &) = default;

private:
    double scale_ = 1.0;
    DPoint pan_{};
    double dpr_ = 1.0;
};

} // namespace archmage::render
