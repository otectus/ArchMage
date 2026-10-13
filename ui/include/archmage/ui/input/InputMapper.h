#pragma once

#include "archmage/render/ViewTransform.h"
#include "archmage/ui/input/PointerSample.h"

#include <optional>

class QEvent;

namespace archmage::ui::input {

/// Maps Qt input events received by a canvas surface to pointer samples and navigation (S1-1.4).
///
/// Event positions are logical canvas-local coordinates; the view transform converts them to device and
/// document coordinates, so no tool applies its own zoom or DPI correction (spec §6.3).
///
/// Wheel and gesture input over the canvas always navigates and never reaches numeric controls
/// (spec §11): a mouse wheel (angle delta only) zooms about the pointer, 2^(1/4) per notch; a touchpad
/// scroll (pixel delta) pans; Shift turns a mouse wheel into a horizontal pan; Ctrl with a touchpad
/// scroll zooms about the pointer; a pinch gesture zooms about its center.
///
/// Thread safety: stateless; GUI thread only because it reads Qt events.
class InputMapper {
public:
    static std::optional<PointerSample> pointerSample(const QEvent &event, const render::ViewTransform &view,
                                                      std::int64_t receiptNs);
    static std::optional<NavigationRequest> navigation(const QEvent &event,
                                                       const render::ViewTransform &view);
};

} // namespace archmage::ui::input
