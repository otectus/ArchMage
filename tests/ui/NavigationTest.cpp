// S1-7.5: navigation through the shell. Wheel zoom keeps the document point under the cursor; the hand pans;
// the zoom tool zooms in, out and by scrubbing; Space held is a temporary hand that paints nothing; Ctrl+0
// and Ctrl+1 fit and show actual pixels; a wheel over a spin box neither zooms the canvas nor changes the
// value of a control the user is not editing (spec §11).

#include "ShellTestSupport.h"

#include "archmage/ui/OptionsRow.h"

#include <QLabel>
#include <QSpinBox>
#include <QWheelEvent>

using namespace archmage;
using testing::ShellFixture;

class NavigationTest : public QObject {
    Q_OBJECT

private slots:
    void wheelZoomsAboutTheCursor();
    void handDragPans();
    void zoomToolClicksAndScrubs();
    void spaceIsATemporaryHand();
    void fitAndActualPixelsShortcuts();
    void wheelOverASpinBoxDoesNotNavigate();
    void statusBarFollowsThePointer();
};

namespace {

void wheel(QWidget *widget, QPointF at, int angle, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QWheelEvent event(at, widget->mapToGlobal(at), QPoint(), QPoint(0, angle), Qt::NoButton, modifiers, Qt::NoScrollPhase, false);
    QCoreApplication::sendEvent(widget, &event);
}

} // namespace

void NavigationTest::wheelZoomsAboutTheCursor() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument(800, 600);
    QWidget *canvas = view->surface().widget();
    const render::ViewTransform before = view->surface().viewTransform();
    const QPointF at(200.0, 150.0);
    const auto anchor = before.logicalToDocument(base::DPoint{at.x(), at.y()});
    wheel(canvas, at, 120);
    const render::ViewTransform after = view->surface().viewTransform();
    QVERIFY(std::abs(after.scale() / before.scale() - std::exp2(0.25)) < 1e-9);
    const auto still = after.logicalToDocument(base::DPoint{at.x(), at.y()});
    QVERIFY(std::abs(still.x - anchor.x) < 1e-6 && std::abs(still.y - anchor.y) < 1e-6);
    QVERIFY(shell.window->zoomLabel()->text().contains(QStringLiteral("118.9")));
    wheel(canvas, at, -120);
    QVERIFY(std::abs(view->surface().viewTransform().scale() - before.scale()) < 1e-9);
}

void NavigationTest::handDragPans() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    shell.window->setActiveTool(QStringLiteral("hand"));
    const render::ViewTransform before = view->surface().viewTransform();
    testing::drag(view->surface().widget(), QPointF(100, 100), QPointF(160, 130));
    const render::ViewTransform after = view->surface().viewTransform();
    const double dpr = view->surface().devicePixelRatio();
    QCOMPARE(after.pan().x - before.pan().x, 60.0 * dpr);
    QCOMPARE(after.pan().y - before.pan().y, 30.0 * dpr);
    QCOMPARE(after.scale(), before.scale());
    QCOMPARE(view->session().executor().history().size(), 1u); // navigation is not a document change
}

void NavigationTest::zoomToolClicksAndScrubs() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    shell.window->setActiveTool(QStringLiteral("zoom"));
    QWidget *canvas = view->surface().widget();
    const double start = view->surface().viewTransform().scale();
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier, QPoint(80, 60));
    QCOMPARE(view->surface().viewTransform().scale(), start * 2.0);
    QTest::mouseClick(canvas, Qt::LeftButton, Qt::AltModifier, QPoint(80, 60));
    QCOMPARE(view->surface().viewTransform().scale(), start);
    // Scrub: 120 logical pixels to the right doubles the scale.
    testing::drag(canvas, QPointF(100, 100), QPointF(220, 100), 12);
    QVERIFY(std::abs(view->surface().viewTransform().scale() / start - 2.0) < 1e-6);
    testing::drag(canvas, QPointF(220, 100), QPointF(100, 100), 12);
    QVERIFY(std::abs(view->surface().viewTransform().scale() / start - 1.0) < 1e-6);
}

void NavigationTest::spaceIsATemporaryHand() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    QWidget *canvas = view->surface().widget();
    canvas->setFocus();
    QTRY_VERIFY(canvas->hasFocus());
    QCOMPARE(view->tool()->id(), QStringLiteral("brush"));
    QTest::keyPress(canvas, Qt::Key_Space);
    QVERIFY(view->temporaryHandActive());
    QCOMPARE(view->effectiveTool()->id(), QStringLiteral("hand"));
    QCOMPARE(canvas->cursor().shape(), Qt::OpenHandCursor);
    const render::ViewTransform before = view->surface().viewTransform();
    testing::drag(canvas, QPointF(100, 100), QPointF(140, 100));
    QVERIFY(view->surface().viewTransform().pan().x > before.pan().x);
    QTest::keyRelease(canvas, Qt::Key_Space);
    QVERIFY(!view->temporaryHandActive());
    QCOMPARE(view->effectiveTool()->id(), QStringLiteral("brush"));
    QCOMPARE(canvas->cursor().shape(), Qt::CrossCursor);
    QTest::qWait(50);
    QCOMPARE(view->session().executor().history().size(), 1u); // the drag panned and painted nothing
    QCOMPARE(shell.window->activeTool(), QStringLiteral("brush"));
}

void NavigationTest::fitAndActualPixelsShortcuts() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument(4000, 3000);
    QWidget *canvas = view->surface().widget();
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_1, Qt::ControlModifier);
    QCOMPARE(view->surface().viewTransform().scale(), 1.0);
    QVERIFY(shell.window->zoomLabel()->text().contains(QStringLiteral("100.0")));
    QTest::keyClick(canvas, Qt::Key_0, Qt::ControlModifier);
    const double fitted = view->surface().viewTransform().scale();
    QVERIFY2(fitted < 0.5, qPrintable(QString::number(fitted)));
    // The whole document is inside the viewport.
    const auto visible = view->surface().viewTransform().visibleDocumentRect(
        render::DSize{static_cast<double>(view->surface().deviceSize().width()),
                      static_cast<double>(view->surface().deviceSize().height())});
    QVERIFY(visible.x <= 0.0 && visible.y <= 0.0 && visible.right() >= 4000.0 && visible.bottom() >= 3000.0);
    QTest::keyClick(canvas, Qt::Key_Equal, Qt::ControlModifier);
    QCOMPARE(view->surface().viewTransform().scale(), fitted * 2.0);
}

void NavigationTest::wheelOverASpinBoxDoesNotNavigate() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    view->surface().widget()->setFocus();
    QSpinBox *size = shell.window->optionsRow()->brushSize();
    const int value = size->value();
    const render::ViewTransform before = view->surface().viewTransform();
    wheel(size, QPointF(size->rect().center()), 120);
    wheel(size, QPointF(size->rect().center()), 120, Qt::ControlModifier);
    QCOMPARE(view->surface().viewTransform().scale(), before.scale());
    QCOMPARE(view->surface().viewTransform().pan().x, before.pan().x);
    QCOMPARE(size->value(), value); // not focused: the wheel is not for this control
    size->setFocus();
    QTRY_VERIFY(size->hasFocus());
    wheel(size, QPointF(size->rect().center()), 120);
    QCOMPARE(size->value(), value + 1); // focused: the wheel edits the value
    QCOMPARE(view->surface().viewTransform().scale(), before.scale());
}

void NavigationTest::statusBarFollowsThePointer() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    QWidget *canvas = view->surface().widget();
    testing::mouse(canvas, QEvent::MouseMove, testing::widgetPoint(*view, 123.5, 45.2), Qt::NoButton, Qt::NoButton);
    QCOMPARE(shell.window->coordinatesLabel()->text(), QStringLiteral("X 123, Y 45 px"));
    testing::mouse(canvas, QEvent::MouseMove, testing::widgetPoint(*view, 900, 900), Qt::NoButton, Qt::NoButton);
    QVERIFY(shell.window->coordinatesLabel()->text().isEmpty()); // outside the canvas
}

QTEST_MAIN(NavigationTest)
#include "NavigationTest.moc"
