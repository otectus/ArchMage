// S1-7.1, S1-7.2, S1-7.6, S1-7.7, S1-7.8, S1-7.9: the shell's regions and geometry, document tabs and the
// clean save point, shortcuts and text input, the tool lifecycle across documents, accessible names, and
// keyboard-only operation.

#include "ShellTestSupport.h"

#include "archmage/native/NativeFile.h"
#include "archmage/ui/OptionsRow.h"
#include "archmage/ui/Theme.h"
#include "archmage/ui/panels/LayersModel.h"
#include "archmage/ui/panels/LayersPanel.h"

#include <QAccessible>
#include <QApplication>
#include <QDockWidget>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenuBar>
#include <QSignalSpy>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>

using namespace archmage;
using testing::ShellFixture;
using CloseChoice = ui::ShellPrompts::CloseChoice;

class ShellTest : public QObject {
    Q_OBJECT

private slots:
    void regionsMatchTheTargetLayout();
    void menusOfferOnlyWorkingCommands();
    void modifiedMarkerFollowsTheSavePoint();
    void closingAModifiedDocumentAsks();
    void openingAndSwitchingDocuments();
    void switchingDocumentsMidStrokeCommitsIt();
    void singleLetterShortcutsSwitchTools();
    void textInputKeepsSingleLetters();
    void tabTogglesPanelsOnlyFromTheCanvas();
    void iconControlsHaveTooltipsAndNames();
    void layersCanBeDrivenByKeyboardAlone();
    void altOpensTheMenus();
    void durabilityMovesToJournaled();
};

void ShellTest::regionsMatchTheTargetLayout() {
    // ARCHMAGE_SNAPSHOT_DIR=<dir> also saves the themed window there for visual review (no display needed).
    const QByteArray snapshots = qgetenv("ARCHMAGE_SNAPSHOT_DIR");
    if (!snapshots.isEmpty()) {
        ui::theme::apply(*qApp);
    }
    ShellFixture shell;
    QVERIFY(shell.ready());
    QVERIFY(shell.newDocument() != nullptr);
    ui::MainWindow &window = *shell.window;
    QTRY_VERIFY(window.layersDock()->width() > 0);
    QCoreApplication::processEvents();

    QCOMPARE(window.size(), QSize(1440, 900));
    const auto near = [](int value, int target) { return std::abs(value - target) <= target / 10; };
    QVERIFY2(near(window.optionsRow()->height(), 36), qPrintable(QString::number(window.optionsRow()->height())));
    QVERIFY2(near(window.toolStrip()->width(), 44), qPrintable(QString::number(window.toolStrip()->width())));
    QVERIFY2(near(window.layersDock()->width(), 300), qPrintable(QString::number(window.layersDock()->width())));
    QCOMPARE(window.colorDock()->width(), window.layersDock()->width());
    QVERIFY2(window.colorDock()->geometry().bottom() < window.layersDock()->geometry().top(), "Color is above Layers");
    QCOMPARE(window.dockWidgetArea(window.layersDock()), Qt::RightDockWidgetArea);
    QCOMPARE(window.toolBarArea(window.toolStrip()), Qt::LeftToolBarArea);
    QCOMPARE(window.toolBarArea(window.optionsRow()), Qt::TopToolBarArea);
    QVERIFY(window.toolStrip()->geometry().right() < window.documentTabs()->mapTo(&window, QPoint(0, 0)).x() + 1);
    QVERIFY(window.optionsRow()->geometry().bottom() <= window.documentTabs()->mapTo(&window, QPoint(0, 0)).y());
    for (QLabel *label : {window.zoomLabel(), window.coordinatesLabel(), window.durabilityLabel(), window.jobLabel()}) {
        QVERIFY(label->isVisible());
    }
    QVERIFY(window.zoomLabel()->text().startsWith(QStringLiteral("Zoom ")));
    QCOMPARE(window.jobLabel()->text(), QStringLiteral("Ready"));
    if (!snapshots.isEmpty()) {
        window.action(QStringLiteral("layer.new"))->trigger();
        QTest::qWait(300);
        QVERIFY(window.grab().save(QString::fromLocal8Bit(snapshots) + QStringLiteral("/shell.png")));
    }
}

void ShellTest::menusOfferOnlyWorkingCommands() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    const QStringList expected{QStringLiteral("File"),   QStringLiteral("Edit"), QStringLiteral("Image"),
                               QStringLiteral("Layer"),  QStringLiteral("Select"), QStringLiteral("Filter"),
                               QStringLiteral("View"),   QStringLiteral("Window"), QStringLiteral("Help")};
    QStringList titles;
    for (QAction *menu : shell.window->menuBar()->actions()) {
        titles << menu->text().remove(QLatin1Char('&'));
    }
    QCOMPARE(titles, expected);
    // Menus without working commands say so with one disabled entry instead of offering dead items.
    for (const QString &empty : {QStringLiteral("Image"), QStringLiteral("Select"), QStringLiteral("Filter")}) {
        const QList<QAction *> items = shell.window->menu(empty)->actions();
        QCOMPARE(items.size(), 1);
        QVERIFY(!items.front()->isEnabled());
    }
    // Without a document, document commands are disabled; New and Open are not.
    QVERIFY(shell.window->action(QStringLiteral("file.new"))->isEnabled());
    QVERIFY(shell.window->action(QStringLiteral("file.open"))->isEnabled());
    QVERIFY(!shell.window->action(QStringLiteral("file.save"))->isEnabled());
    QVERIFY(!shell.window->action(QStringLiteral("edit.undo"))->isEnabled());
    QVERIFY(!shell.window->action(QStringLiteral("layer.new"))->isEnabled());
}

void ShellTest::modifiedMarkerFollowsTheSavePoint() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    ui::DocumentSession &session = view->session();
    QTabWidget &tabs = *shell.window->documentTabs();
    QCOMPARE(tabs.tabText(0), QStringLiteral("Untitled-1"));

    shell.window->action(QStringLiteral("layer.new"))->trigger();
    QVERIFY(testing::waitForRevision(session, 2));
    QTRY_COMPARE(tabs.tabText(0), QStringLiteral("Untitled-1 *"));
    QVERIFY(shell.window->windowTitle().contains(QStringLiteral("*")));

    shell.window->action(QStringLiteral("edit.undo"))->trigger(); // back to the clean point (spec §9.2)
    QTRY_COMPARE(tabs.tabText(0), QStringLiteral("Untitled-1"));
    shell.window->action(QStringLiteral("edit.redo"))->trigger();
    QTRY_COMPARE(tabs.tabText(0), QStringLiteral("Untitled-1 *"));

    const auto file = shell.path(QStringLiteral("marker.archmage"));
    shell.prompts->savePaths.push_back(QString::fromStdString(file.string()));
    QSignalSpy saved(shell.window.get(), &ui::MainWindow::documentSaved);
    shell.window->action(QStringLiteral("file.save"))->trigger();
    QVERIFY(saved.wait(10000));
    QCOMPARE(saved.front().at(1).toBool(), true);
    QCOMPARE(tabs.tabText(0), QStringLiteral("marker.archmage"));
    QVERIFY(std::filesystem::exists(file));

    shell.window->action(QStringLiteral("layer.new"))->trigger();
    QTRY_COMPARE(tabs.tabText(0), QStringLiteral("marker.archmage *"));
    shell.window->action(QStringLiteral("edit.undo"))->trigger();
    QTRY_COMPARE(tabs.tabText(0), QStringLiteral("marker.archmage"));
    shell.window->action(QStringLiteral("edit.undo"))->trigger(); // before the save point: modified again
    QTRY_COMPARE(tabs.tabText(0), QStringLiteral("marker.archmage *"));
}

void ShellTest::closingAModifiedDocumentAsks() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    const std::filesystem::path journal = view->session().journal()->path();
    QVERIFY(std::filesystem::exists(journal));
    shell.window->action(QStringLiteral("layer.new"))->trigger();
    QVERIFY(testing::waitForRevision(view->session(), 2));

    shell.prompts->closeChoices.push_back(CloseChoice::Cancel);
    QVERIFY(!shell.window->closeDocument(0));
    QCOMPARE(shell.window->documentCount(), 1);

    shell.prompts->closeChoices.push_back(CloseChoice::Save);
    const auto file = shell.path(QStringLiteral("closed.archmage"));
    shell.prompts->savePaths.push_back(QString::fromStdString(file.string()));
    QVERIFY(shell.window->closeDocument(0));
    QCOMPARE(shell.window->documentCount(), 0);
    QCOMPARE(shell.prompts->closeQuestions, 2);
    auto reopened = native::readDocument(file);
    QVERIFY(reopened);
    QCOMPARE(reopened->state->layers.size(), 2u);
    QVERIFY2(!std::filesystem::exists(journal), "a deliberate close discards the journal");

    // Discard: closes without saving; an unmodified document closes without asking.
    ui::DocumentView *second = shell.newDocument();
    shell.window->action(QStringLiteral("layer.new"))->trigger();
    QVERIFY(testing::waitForRevision(second->session(), 2));
    shell.prompts->closeChoices.push_back(CloseChoice::Discard);
    QVERIFY(shell.window->closeDocument(0));
    QVERIFY(shell.newDocument() != nullptr);
    QVERIFY(shell.window->closeDocument(0));
    QCOMPARE(shell.prompts->closeQuestions, 3);
}

void ShellTest::openingAndSwitchingDocuments() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument(320, 200);
    shell.window->action(QStringLiteral("layer.new"))->trigger();
    QVERIFY(testing::waitForRevision(view->session(), 2));
    const auto file = shell.path(QStringLiteral("opened.archmage"));
    shell.prompts->savePaths.push_back(QString::fromStdString(file.string()));
    shell.prompts->closeChoices.push_back(CloseChoice::Save);
    QVERIFY(shell.window->closeDocument(0));

    QVERIFY(shell.newDocument(100, 100) != nullptr);
    QSignalSpy opened(shell.window.get(), &ui::MainWindow::documentOpened);
    shell.prompts->openPaths.push_back(QString::fromStdString(file.string()));
    shell.window->action(QStringLiteral("file.open"))->trigger();
    QVERIFY(opened.wait(10000));
    QCOMPARE(shell.window->documentCount(), 2);
    QCOMPARE(shell.window->currentIndex(), 1);
    QCOMPARE(shell.window->documentTabs()->tabText(1), QStringLiteral("opened.archmage"));
    QCOMPARE(shell.window->layersPanel()->model().rowCount(), 2);

    shell.window->documentTabs()->setCurrentIndex(0);
    QCOMPARE(shell.window->layersPanel()->model().rowCount(), 1); // the panel follows the current document
    shell.window->documentTabs()->setCurrentIndex(1);
    QCOMPARE(shell.window->layersPanel()->model().rowCount(), 2);

    // Opening it again brings the open tab to the front.
    shell.window->documentTabs()->setCurrentIndex(0);
    shell.window->openFile(QString::fromStdString(file.string()));
    QCOMPARE(shell.window->documentCount(), 2);
    QCOMPARE(shell.window->currentIndex(), 1);
}

void ShellTest::switchingDocumentsMidStrokeCommitsIt() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *first = shell.newDocument();
    ui::DocumentView *second = shell.newDocument();
    shell.window->documentTabs()->setCurrentWidget(first);
    QCOMPARE(shell.window->activeTool(), QStringLiteral("brush"));
    QWidget *canvas = first->surface().widget();
    testing::mouse(canvas, QEvent::MouseButtonPress, testing::widgetPoint(*first, 50, 50), Qt::LeftButton, Qt::LeftButton);
    testing::mouse(canvas, QEvent::MouseMove, testing::widgetPoint(*first, 120, 90), Qt::NoButton, Qt::LeftButton);
    QVERIFY(first->tool()->isInteracting());

    // ADR-0105: switching documents commits what was drawn so far.
    shell.window->documentTabs()->setCurrentWidget(second);
    QVERIFY(!first->tool()->isInteracting());
    QVERIFY(testing::waitForRevision(first->session(), 2));
    const auto history = first->session().executor().history();
    QCOMPARE(QString::fromStdString(history.back().label), QStringLiteral("Brush Stroke"));
    QCOMPARE(second->session().executor().history().size(), 1u);
    // The release lands on a document that no longer has a stroke in progress: nothing else happens.
    testing::mouse(canvas, QEvent::MouseButtonRelease, testing::widgetPoint(*first, 120, 90), Qt::LeftButton, Qt::NoButton);
    QTest::qWait(50);
    QCOMPARE(first->session().executor().history().size(), 2u);
}

void ShellTest::singleLetterShortcutsSwitchTools() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    QWidget *canvas = view->surface().widget();
    canvas->setFocus();
    const std::pair<Qt::Key, QString> keys[] = {{Qt::Key_H, QStringLiteral("hand")},
                                                {Qt::Key_Z, QStringLiteral("zoom")},
                                                {Qt::Key_I, QStringLiteral("eyedropper")},
                                                {Qt::Key_B, QStringLiteral("brush")}};
    for (const auto &[key, tool] : keys) {
        QTest::keyClick(canvas, key);
        QCOMPARE(shell.window->activeTool(), tool);
        QCOMPARE(view->tool()->id(), tool);
        QCOMPARE(shell.window->optionsRow()->currentTool(), tool);
    }
    const double size = shell.window->toolSettings().brush.size;
    QTest::keyClick(canvas, Qt::Key_BracketRight);
    QVERIFY(shell.window->toolSettings().brush.size > size);
    QCOMPARE(shell.window->optionsRow()->brushSize()->value(), static_cast<int>(shell.window->toolSettings().brush.size));
    QTest::keyClick(canvas, Qt::Key_BracketLeft);
    QCOMPARE(shell.window->toolSettings().brush.size, size);
}

void ShellTest::textInputKeepsSingleLetters() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    shell.newDocument();
    shell.window->setActiveTool(QStringLiteral("hand"));
    shell.window->action(QStringLiteral("layer.rename"))->trigger(); // F2: rename the active layer inline
    auto *editor = shell.window->layersPanel()->findChild<QLineEdit *>(QStringLiteral("archmageLayerNameEditor"));
    QTRY_VERIFY(editor != nullptr && editor->hasFocus());
    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("bzi"));
    QCOMPARE(editor->text(), QStringLiteral("bzi"));
    QCOMPARE(shell.window->activeTool(), QStringLiteral("hand")); // B, Z and I stayed text (S1-7.6)
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(shell.window->layersPanel()->model().index(0).data().toString(), QStringLiteral("bzi"));
}

void ShellTest::tabTogglesPanelsOnlyFromTheCanvas() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    QWidget *canvas = view->surface().widget();
    canvas->setFocus();
    QTest::keyClick(canvas, Qt::Key_Tab);
    QVERIFY(!shell.window->panelsVisible());
    QVERIFY(!shell.window->layersDock()->isVisible());
    QVERIFY(!shell.window->toolStrip()->isVisible());
    QTest::keyClick(canvas, Qt::Key_Tab);
    QVERIFY(shell.window->panelsVisible());
    QVERIFY(shell.window->layersDock()->isVisible());

    // In a panel, Tab moves focus as usual.
    QListView &list = shell.window->layersPanel()->list();
    list.setFocus();
    QTRY_VERIFY(list.hasFocus());
    QTest::keyClick(&list, Qt::Key_Tab);
    QVERIFY(shell.window->panelsVisible());
    QVERIFY(!list.hasFocus());
}

void ShellTest::iconControlsHaveTooltipsAndNames() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    shell.newDocument();
    int checked = 0;
    for (QAction *action : shell.window->findChildren<QAction *>()) {
        if (action->icon().isNull()) {
            continue;
        }
        ++checked;
        QVERIFY2(!action->toolTip().isEmpty(), qPrintable(action->objectName()));
        if (!action->shortcut().isEmpty()) {
            QVERIFY2(action->toolTip().contains(action->shortcut().toString(QKeySequence::NativeText)),
                     qPrintable(action->objectName() + QStringLiteral(": ") + action->toolTip()));
        }
    }
    QVERIFY(checked >= 9);
    // Every named action's tooltip is plain text: no mnemonic marker and no menu shortcut hint.
    for (QAction *action : shell.window->findChildren<QAction *>()) {
        if (action->objectName().contains(QLatin1Char('.'))) {
            QVERIFY2(!action->toolTip().contains(QLatin1Char('\t')) && !action->toolTip().contains(QLatin1Char('&')),
                     qPrintable(action->objectName() + QStringLiteral(": ") + action->toolTip()));
        }
    }
    int buttons = 0;
    for (QToolButton *button : shell.window->findChildren<QToolButton *>()) {
        if (button->icon().isNull() || !button->isVisible()) {
            continue;
        }
        ++buttons;
        QAccessibleInterface *accessible = QAccessible::queryAccessibleInterface(button);
        QVERIFY(accessible != nullptr);
        QVERIFY2(!accessible->text(QAccessible::Name).isEmpty(), qPrintable(button->objectName()));
        QVERIFY2(!button->toolTip().isEmpty(), qPrintable(accessible->text(QAccessible::Name)));
    }
    QVERIFY(buttons >= 9);
}

void ShellTest::layersCanBeDrivenByKeyboardAlone() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    ui::DocumentSession &session = view->session();
    for (int i = 0; i < 2; ++i) {
        shell.window->action(QStringLiteral("layer.new"))->trigger();
        QVERIFY(testing::waitForRevision(session, static_cast<std::uint64_t>(2 + i)));
    }
    QListView &list = shell.window->layersPanel()->list();
    QTRY_COMPARE(list.model()->rowCount(), 3);

    // F6 cycles regions until the Layers list has focus.
    view->surface().widget()->setFocus();
    for (int i = 0; i < 3 && !list.hasFocus(); ++i) {
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_F6);
    }
    QVERIFY(list.hasFocus());
    QTest::keyClick(&list, Qt::Key_Home);
    QTest::keyClick(&list, Qt::Key_Down);
    QTest::keyClick(&list, Qt::Key_Down);
    QCOMPARE(list.currentIndex().row(), 2);
    QCOMPARE(session.activeLayer(), session.revision()->state->layers.front()->id); // the bottom layer

    QTest::keyClick(&list, Qt::Key_F2);
    auto *editor = list.findChild<QLineEdit *>(QStringLiteral("archmageLayerNameEditor"));
    QTRY_VERIFY(editor != nullptr && editor->hasFocus());
    editor->selectAll();
    QTest::keyClicks(editor, QStringLiteral("Paper"));
    QTest::keyClick(editor, Qt::Key_Return);
    QTRY_COMPARE(QString::fromStdString(session.revision()->state->layers.front()->name), QStringLiteral("Paper"));

    QTRY_VERIFY(list.hasFocus());
    QTest::keyClick(&list, Qt::Key_Space); // hide it
    QTRY_VERIFY(!session.revision()->state->layers.front()->visible);
    QTest::keyClick(&list, Qt::Key_Space);
    QTRY_VERIFY(session.revision()->state->layers.front()->visible);
}

void ShellTest::altOpensTheMenus() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    QMenu *file = shell.window->menu(QStringLiteral("File"));
    QTest::keyClick(shell.window.get(), Qt::Key_F, Qt::AltModifier);
    QTRY_VERIFY(file->isVisible());
    QTest::keyClick(file, Qt::Key_Escape);
    QTRY_VERIFY(!file->isVisible());
}

void ShellTest::durabilityMovesToJournaled() {
    ShellFixture shell;
    QVERIFY(shell.ready());
    ui::DocumentView *view = shell.newDocument();
    shell.window->action(QStringLiteral("layer.new"))->trigger();
    QVERIFY(testing::waitForRevision(view->session(), 2));
    QTRY_COMPARE_WITH_TIMEOUT(shell.window->durabilityLabel()->text(), QStringLiteral("Journaled"), 6000);
    const auto file = shell.path(QStringLiteral("durable.archmage"));
    shell.prompts->savePaths.push_back(QString::fromStdString(file.string()));
    QSignalSpy saved(shell.window.get(), &ui::MainWindow::documentSaved);
    shell.window->action(QStringLiteral("file.save"))->trigger();
    QVERIFY(saved.wait(10000));
    QTRY_COMPARE(shell.window->durabilityLabel()->text(), QStringLiteral("Saved"));
}

QTEST_MAIN(ShellTest)
#include "ShellTest.moc"
