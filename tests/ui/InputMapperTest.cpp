// S1-1.4: InputMapper turns Qt events into pointer samples and navigation (spec §11, §16.1).

#include "archmage/ui/input/InputMapper.h"

#include <QMouseEvent>
#include <QNativeGestureEvent>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTest>
#include <QWheelEvent>

#include <cmath>

using archmage::base::DPoint;
using archmage::render::ViewTransform;
using archmage::ui::input::InputMapper;
using archmage::ui::input::NavigationRequest;
using archmage::ui::input::PointerDevice;
using archmage::ui::input::PointerPhase;

class InputMapperTest : public QObject {
    Q_OBJECT

private slots:
    void mouseSamplesUseTheViewTransform();
    void mousePhasesFollowEventTypeAndButtons();
    void tabletSamplesKeepPressureTiltAndRotation();
    void eraserEndIsReported();
    void mouseWheelZoomsAboutThePointer();
    void touchpadScrollPans();
    void shiftWheelPansHorizontally();
    void ctrlTouchpadScrollZooms();
    void pinchZooms();
    void otherEventsAreIgnored();

private:
    // Scale 2, document origin at device (100, 50), device pixel ratio 1.5.
    const ViewTransform view_{2.0, DPoint{100.0, 50.0}, 1.5};
};

void InputMapperTest::mouseSamplesUseTheViewTransform() {
    QMouseEvent event(QEvent::MouseButtonPress, QPointF(40.0, 20.0), QPointF(40.0, 20.0), Qt::LeftButton,
                      Qt::LeftButton, Qt::ShiftModifier);
    const auto sample = InputMapper::pointerSample(event, view_, 123456789);
    QVERIFY(sample.has_value());
    QCOMPARE(sample->device, PointerDevice::Mouse);
    QCOMPARE(sample->phase, PointerPhase::Press);
    QCOMPARE(sample->timestampNs, 123456789);
    QCOMPARE(sample->posDevice, (DPoint{60.0, 30.0}));     // logical × 1.5
    QCOMPARE(sample->posDocument, (DPoint{-20.0, -10.0})); // (device − pan) / 2
    QCOMPARE(sample->button, Qt::LeftButton);
    QCOMPARE(sample->modifiers, Qt::KeyboardModifiers(Qt::ShiftModifier));
    QVERIFY(!sample->hasPressure);
    QCOMPARE(sample->pressure, 1.0);
}

void InputMapperTest::mousePhasesFollowEventTypeAndButtons() {
    QMouseEvent hover(QEvent::MouseMove, QPointF(1, 1), QPointF(1, 1), Qt::NoButton, Qt::NoButton,
                      Qt::NoModifier);
    QCOMPARE(InputMapper::pointerSample(hover, view_, 0)->phase, PointerPhase::Hover);
    QMouseEvent drag(QEvent::MouseMove, QPointF(1, 1), QPointF(1, 1), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QCOMPARE(InputMapper::pointerSample(drag, view_, 0)->phase, PointerPhase::Move);
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(1, 1), QPointF(1, 1), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QCOMPARE(InputMapper::pointerSample(release, view_, 0)->phase, PointerPhase::Release);
}

void InputMapperTest::tabletSamplesKeepPressureTiltAndRotation() {
    const QPointingDevice pen(QStringLiteral("test pen"), 42, QInputDevice::DeviceType::Stylus,
                              QPointingDevice::PointerType::Pen,
                              QInputDevice::Capability::Position | QInputDevice::Capability::Pressure |
                                  QInputDevice::Capability::XTilt | QInputDevice::Capability::YTilt |
                                  QInputDevice::Capability::Rotation,
                              1, 3);
    QTabletEvent event(QEvent::TabletMove, &pen, QPointF(10.0, 10.0), QPointF(10.0, 10.0), 0.4, 15.0, -20.0,
                       0.0, 33.0, 0.0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    const auto sample = InputMapper::pointerSample(event, view_, 5);
    QVERIFY(sample.has_value());
    QCOMPARE(sample->device, PointerDevice::Pen);
    QCOMPARE(sample->phase, PointerPhase::Move);
    QVERIFY(sample->hasPressure);
    QCOMPARE(sample->pressure, 0.4);
    QCOMPARE(sample->tiltX, 15.0);
    QCOMPARE(sample->tiltY, -20.0);
    QCOMPARE(sample->rotation, 33.0);
    QCOMPARE(sample->deviceId, std::uint64_t{42});
    QCOMPARE(sample->posDocument, (DPoint{-42.5, -17.5}));
}

void InputMapperTest::eraserEndIsReported() {
    const QPointingDevice eraser(QStringLiteral("test eraser"), 43, QInputDevice::DeviceType::Stylus,
                                 QPointingDevice::PointerType::Eraser,
                                 QInputDevice::Capability::Position | QInputDevice::Capability::Pressure, 1,
                                 3);
    QTabletEvent event(QEvent::TabletPress, &eraser, QPointF(0, 0), QPointF(0, 0), 1.0, 0.0, 0.0, 0.0, 0.0,
                       0.0, Qt::NoModifier, Qt::LeftButton, Qt::LeftButton);
    QCOMPARE(InputMapper::pointerSample(event, view_, 0)->device, PointerDevice::Eraser);
}

void InputMapperTest::mouseWheelZoomsAboutThePointer() {
    QWheelEvent event(QPointF(80.0, 40.0), QPointF(80.0, 40.0), QPoint(), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    const auto nav = InputMapper::navigation(event, view_);
    QVERIFY(nav.has_value());
    QCOMPARE(nav->kind, NavigationRequest::Kind::ZoomAbout);
    QVERIFY(std::abs(nav->zoomFactor - std::pow(2.0, 0.25)) < 1e-12);
    QCOMPARE(nav->anchorDevice, (DPoint{120.0, 60.0}));
    // The wheel never reaches the pointer path, so numeric controls under the canvas are untouched.
    QVERIFY(!InputMapper::pointerSample(event, view_, 0).has_value());
}

void InputMapperTest::touchpadScrollPans() {
    QWheelEvent event(QPointF(0, 0), QPointF(0, 0), QPoint(6, -10), QPoint(0, -120), Qt::NoButton,
                      Qt::NoModifier, Qt::ScrollUpdate, false);
    const auto nav = InputMapper::navigation(event, view_);
    QVERIFY(nav.has_value());
    QCOMPARE(nav->kind, NavigationRequest::Kind::Pan);
    QCOMPARE(nav->panDevice, (DPoint{9.0, -15.0}));
}

void InputMapperTest::shiftWheelPansHorizontally() {
    QWheelEvent event(QPointF(0, 0), QPointF(0, 0), QPoint(), QPoint(0, -240), Qt::NoButton,
                      Qt::ShiftModifier, Qt::NoScrollPhase, false);
    const auto nav = InputMapper::navigation(event, view_);
    QVERIFY(nav.has_value());
    QCOMPARE(nav->kind, NavigationRequest::Kind::Pan);
    QCOMPARE(nav->panDevice, (DPoint{-192.0, 0.0})); // two notches × 64 logical px × 1.5
}

void InputMapperTest::ctrlTouchpadScrollZooms() {
    QWheelEvent event(QPointF(10, 10), QPointF(10, 10), QPoint(0, 240), QPoint(0, 120), Qt::NoButton,
                      Qt::ControlModifier, Qt::ScrollUpdate, false);
    const auto nav = InputMapper::navigation(event, view_);
    QVERIFY(nav.has_value());
    QCOMPARE(nav->kind, NavigationRequest::Kind::ZoomAbout);
    QCOMPARE(nav->zoomFactor, 2.0);
}

void InputMapperTest::pinchZooms() {
    const QPointingDevice touchpad(QStringLiteral("touchpad"), 7, QInputDevice::DeviceType::TouchPad,
                                   QPointingDevice::PointerType::Finger, QInputDevice::Capability::Position,
                                   2, 0);
    QNativeGestureEvent event(Qt::ZoomNativeGesture, &touchpad, 2, QPointF(20, 20), QPointF(20, 20),
                              QPointF(20, 20), 0.25, QPointF());
    const auto nav = InputMapper::navigation(event, view_);
    QVERIFY(nav.has_value());
    QCOMPARE(nav->kind, NavigationRequest::Kind::ZoomAbout);
    QCOMPARE(nav->zoomFactor, 1.25);
    QCOMPARE(nav->anchorDevice, (DPoint{30.0, 30.0}));
}

void InputMapperTest::otherEventsAreIgnored() {
    QEvent event(QEvent::FocusIn);
    QVERIFY(!InputMapper::pointerSample(event, view_, 0).has_value());
    QVERIFY(!InputMapper::navigation(event, view_).has_value());
}

QTEST_MAIN(InputMapperTest)
#include "InputMapperTest.moc"
