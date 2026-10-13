// S1-7.4: foreground and background colors. X swaps and D resets (shortcuts and panel buttons); the brush
// paints with the foreground; the eyedropper makes the composite under the pointer the foreground.

#include "ShellTestSupport.h"

#include "archmage/ui/ColorState.h"
#include "archmage/ui/panels/ColorPanel.h"

#include <QToolButton>

using namespace archmage;
using testing::ShellFixture;

class ColorPanelTest : public QObject {
    Q_OBJECT

private slots:
    void swapAndResetByShortcutAndButton();
    void brushPaintsWithTheForeground();
    void eyedropperSamplesTheComposite();
};

void ColorPanelTest::swapAndResetByShortcutAndButton() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    ui::ColorState &colors = shell.window->colors();
    QCOMPARE(colors.foreground(), QColor(Qt::black));
    QCOMPARE(colors.background(), QColor(Qt::white));
    colors.setForeground(QColor(255, 0, 0));
    view->surface().widget()->setFocus();
    QTest::keyClick(view->surface().widget(), Qt::Key_X);
    QCOMPARE(colors.foreground(), QColor(Qt::white));
    QCOMPARE(colors.background(), QColor(255, 0, 0));
    QTest::keyClick(view->surface().widget(), Qt::Key_D);
    QCOMPARE(colors.foreground(), QColor(Qt::black));
    QCOMPARE(colors.background(), QColor(Qt::white));

    // The panel's buttons trigger the same commands; the swatches show the colors.
    const QList<QToolButton *> buttons = shell.window->colorPanel()->findChildren<QToolButton *>();
    QToolButton *swapButton = nullptr;
    for (QToolButton *button : buttons) {
        if (button->defaultAction() == shell.window->action(QStringLiteral("color.swap"))) {
            swapButton = button;
        }
    }
    QVERIFY(swapButton != nullptr);
    QTest::mouseClick(swapButton, Qt::LeftButton);
    QCOMPARE(colors.foreground(), QColor(Qt::white));
    QVERIFY(shell.window->colorPanel()->foregroundSwatch().toolTip().contains(QStringLiteral("#ffffff")));
}

void ColorPanelTest::brushPaintsWithTheForeground() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    shell.window->colors().setForeground(QColor(0, 0, 255));
    testing::drag(view->surface().widget(), testing::widgetPoint(*view, 50, 50), testing::widgetPoint(*view, 200, 50));
    QVERIFY(testing::waitForRevision(view->session(), 2));
    const auto &tiles = view->session().revision()->state->layers.back()->content->tiles;
    const auto tile = tiles.at({0, 0});
    QVERIFY(tile->sample(2, 100, 50) > 0.99f); // blue
    QVERIFY(tile->sample(0, 100, 50) < 0.01f);
}

void ColorPanelTest::eyedropperSamplesTheComposite() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    ui::ColorState &colors = shell.window->colors();
    colors.setForeground(QColor(0, 160, 0));
    testing::drag(view->surface().widget(), testing::widgetPoint(*view, 40, 100), testing::widgetPoint(*view, 300, 100));
    QVERIFY(testing::waitForRevision(view->session(), 2));
    colors.reset();
    shell.window->setActiveTool(QStringLiteral("eyedropper"));
    QTest::mouseClick(view->surface().widget(), Qt::LeftButton, Qt::NoModifier,
                      testing::widgetPoint(*view, 150, 100).toPoint());
    QCOMPARE(colors.foreground(), QColor(0, 160, 0));
    // The white background is opaque: sampling it gives white.
    QTest::mouseClick(view->surface().widget(), Qt::LeftButton, Qt::NoModifier,
                      testing::widgetPoint(*view, 150, 250).toPoint());
    QCOMPARE(colors.foreground(), QColor(Qt::white));
    QCOMPARE(view->session().executor().history().size(), 2u); // sampling changes no document
}

QTEST_MAIN(ColorPanelTest)
#include "ColorPanelTest.moc"
