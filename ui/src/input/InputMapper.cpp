#include "archmage/ui/input/InputMapper.h"

#include <QEvent>
#include <QInputDevice>
#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QWheelEvent>

#include <cmath>

namespace archmage::ui::input {
namespace {

tiles::DPoint toPoint(const QPointF &p) { return {p.x(), p.y()}; }

PointerPhase phaseOf(QEvent::Type type, Qt::MouseButtons buttons) {
    switch (type) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick:
    case QEvent::TabletPress:
        return PointerPhase::Press;
    case QEvent::MouseButtonRelease:
    case QEvent::TabletRelease:
        return PointerPhase::Release;
    default:
        return buttons == Qt::NoButton ? PointerPhase::Hover : PointerPhase::Move;
    }
}

void fillCommon(PointerSample &s, const QSinglePointEvent &e, const render::ViewTransform &view,
                std::int64_t receiptNs) {
    s.timestampNs = receiptNs;
    s.platformTimestampMs = e.timestamp();
    s.posDevice = view.logicalToDevice(toPoint(e.position()));
    s.posDocument = view.deviceToDocument(s.posDevice);
    s.buttons = e.buttons();
    s.button = e.button();
    s.modifiers = e.modifiers();
    s.phase = phaseOf(e.type(), e.buttons());
    if (const QPointingDevice *device = e.pointingDevice()) {
        s.deviceId = static_cast<std::uint64_t>(device->systemId());
    }
}

} // namespace

std::optional<PointerSample>
InputMapper::pointerSample(const QEvent &event, const render::ViewTransform &view, std::int64_t receiptNs) {
    switch (event.type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove: {
        const auto &e = static_cast<const QMouseEvent &>(event);
        PointerSample s;
        fillCommon(s, e, view, receiptNs);
        // Synthesized from touch or a tablet: report the originating kind so tools do not double count.
        const QPointingDevice *device = e.pointingDevice();
        if (device != nullptr && device->type() == QInputDevice::DeviceType::TouchScreen) {
            s.device = PointerDevice::Touch;
        } else {
            s.device = PointerDevice::Mouse;
        }
        return s;
    }
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease: {
        const auto &e = static_cast<const QTabletEvent &>(event);
        PointerSample s;
        fillCommon(s, e, view, receiptNs);
        const QPointingDevice *device = e.pointingDevice();
        const bool eraser =
            device != nullptr && device->pointerType() == QPointingDevice::PointerType::Eraser;
        s.device = eraser ? PointerDevice::Eraser : PointerDevice::Pen;
        s.hasPressure =
            device == nullptr || device->capabilities().testFlag(QInputDevice::Capability::Pressure);
        s.pressure = s.hasPressure ? e.pressure() : 1.0;
        s.tiltX = e.xTilt();
        s.tiltY = e.yTilt();
        s.rotation = e.rotation();
        return s;
    }
    default:
        return std::nullopt;
    }
}

std::optional<NavigationRequest> InputMapper::navigation(const QEvent &event,
                                                         const render::ViewTransform &view) {
    if (event.type() == QEvent::Wheel) {
        const auto &e = static_cast<const QWheelEvent &>(event);
        const tiles::DPoint anchor = view.logicalToDevice(toPoint(e.position()));
        const QPoint pixel = e.pixelDelta();
        const QPoint angle = e.angleDelta();
        NavigationRequest request;
        if (!pixel.isNull()) {
            if (e.modifiers().testFlag(Qt::ControlModifier)) {
                request.kind = NavigationRequest::Kind::ZoomAbout;
                request.zoomFactor = std::pow(2.0, pixel.y() / 240.0);
                request.anchorDevice = anchor;
            } else {
                request.kind = NavigationRequest::Kind::Pan;
                request.panDevice = view.logicalToDevice(
                    tiles::DPoint{static_cast<double>(pixel.x()), static_cast<double>(pixel.y())});
            }
            return request;
        }
        if (angle.isNull()) {
            return std::nullopt;
        }
        if (e.modifiers().testFlag(Qt::ShiftModifier)) {
            // One notch pans 64 logical pixels; Qt may already report Shift+wheel on the x axis.
            const int notches = angle.x() != 0 ? angle.x() : angle.y();
            request.kind = NavigationRequest::Kind::Pan;
            request.panDevice = view.logicalToDevice(tiles::DPoint{notches / 120.0 * 64.0, 0.0});
            return request;
        }
        request.kind = NavigationRequest::Kind::ZoomAbout;
        request.zoomFactor = std::pow(2.0, angle.y() / 120.0 * 0.25);
        request.anchorDevice = anchor;
        return request;
    }
    if (event.type() == QEvent::NativeGesture) {
        const auto &e = static_cast<const QNativeGestureEvent &>(event);
        if (e.gestureType() == Qt::ZoomNativeGesture) {
            NavigationRequest request;
            request.kind = NavigationRequest::Kind::ZoomAbout;
            request.zoomFactor = 1.0 + e.value();
            request.anchorDevice = view.logicalToDevice(toPoint(e.position()));
            return request;
        }
    }
    return std::nullopt;
}

} // namespace archmage::ui::input
