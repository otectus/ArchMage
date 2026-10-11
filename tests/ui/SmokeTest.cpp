// UI tier smoke test: QTEST_MAIN constructs and destroys a QApplication on the offscreen
// platform, and the main window shows and closes without a display server.

#include "archmage/ui/MainWindow.h"

#include <QGuiApplication>
#include <QTest>

class SmokeTest : public QObject {
    Q_OBJECT

private slots:
    void runsOnTheOffscreenPlatform();
    void mainWindowShowsAndCloses();
};

void SmokeTest::runsOnTheOffscreenPlatform() {
    // CTest sets QT_QPA_PLATFORM=offscreen; failing here means the tier would need a display.
    QCOMPARE(QGuiApplication::platformName(), QStringLiteral("offscreen"));
}

void SmokeTest::mainWindowShowsAndCloses() {
    archmage::ui::MainWindow window;
    QCOMPARE(window.windowTitle(), QStringLiteral("ArchMage"));

    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QVERIFY(window.close());
    QVERIFY(!window.isVisible());
}

QTEST_MAIN(SmokeTest)
#include "SmokeTest.moc"
