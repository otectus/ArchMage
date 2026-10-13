#pragma once

#include "archmage/base/Geometry.h"

#include <Qt>

#include <cstdint>

namespace archmage::ui::input {

enum class PointerDevice { Mouse, Pen, Eraser, Touch, Unknown };
enum class PointerPhase { Hover, Press, Move, Release };

/// One pointer sample in device and document coordinates (S1-1.4). Samples keep their receipt
/// timestamp and pressure, tilt and rotation history so strokes never depend on the display refresh
/// rate (spec §16.1). Devices without pressure report pressure 1.0 with hasPressure false.
///
/// Thread safety: a plain value; safe to copy between threads.
struct PointerSample {
    PointerDevice device = PointerDevice::Unknown;
    PointerPhase phase = PointerPhase::Hover;
    std::uint64_t deviceId = 0;            // QInputDevice::systemId(), 0 when unknown
    std::int64_t timestampNs = 0;          // steady-clock time the event was received
    std::uint64_t platformTimestampMs = 0; // QInputEvent::timestamp()
    base::DPoint posDevice;
    base::DPoint posDocument;
    double pressure = 1.0;
    bool hasPressure = false;
    double tiltX = 0.0;    // degrees
    double tiltY = 0.0;    // degrees
    double rotation = 0.0; // barrel rotation, degrees
    Qt::MouseButtons buttons;
    Qt::MouseButton button = Qt::NoButton; // the button that changed, for Press and Release
    Qt::KeyboardModifiers modifiers;
};

/// Navigation derived from wheel and gesture input over the canvas.
struct NavigationRequest {
    enum class Kind { ZoomAbout, Pan };
    Kind kind = Kind::Pan;
    double zoomFactor = 1.0;   // multiplies the current scale (ZoomAbout)
    base::DPoint anchorDevice; // stays fixed while zooming (ZoomAbout)
    base::DPoint panDevice;    // added to the view's pan (Pan)
};

} // namespace archmage::ui::input
