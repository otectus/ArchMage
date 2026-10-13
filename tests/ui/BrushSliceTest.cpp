// S1-3.5: a stroke through the real canvas reaches the preview, the history and the tile store.

#include "archmage/commands/Command.h"
#include "archmage/render/CpuEvaluator.h"
#include "archmage/ui/DocumentView.h"
#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/tools/BrushTool.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QTest>

using namespace archmage;

class BrushSliceTest : public QObject {
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void strokeCommitsOneHistoryItem();
    void escapeCancelsTheStroke();
    void lockedLayerReportsAnError();
    void focusLossCommitsTheStroke();

private:
    void press(QPointF at);
    void move(QPointF at);
    void release(QPointF at);
    [[nodiscard]] QPointF widgetPoint(double docX, double docY) const;
    [[nodiscard]] std::size_t paintTiles() const;

    std::shared_ptr<render::CpuEvaluator> evaluator_;
    std::unique_ptr<ui::DocumentSession> session_;
    std::unique_ptr<ui::DocumentView> view_;
};

void BrushSliceTest::init() {
    evaluator_ = std::make_shared<render::CpuEvaluator>();
    auto created = commands::createDocument(
        commands::CommandEnvelope::make("archmage.document.create", {{"width", 512}, {"height", 512}}));
    session_ =
        std::make_unique<ui::DocumentSession>(created->state, QStringLiteral("New Document"), evaluator_);
    QVERIFY(session_->executor().execute(
        commands::CommandEnvelope::make("archmage.layer.add", {{"name", "Paint"}})));
    QTRY_COMPARE(session_->revision()->sequence, 2u);
    session_->setActiveLayer(
        session_->revision()->state->layers.back()->id); // what the Layers panel does (S1-7)
    QCOMPARE(QString::fromStdString(session_->revision()->state->findLayer(session_->activeLayer())->name),
             QStringLiteral("Paint"));
    view_ = std::make_unique<ui::DocumentView>(*session_);
    view_->setTool(std::make_unique<ui::BrushTool>());
    view_->resize(640, 640);
    view_->show();
    QVERIFY(QTest::qWaitForWindowExposed(view_.get()));
    QTRY_VERIFY(view_->surface().framesPresented() > 0);
    // Actual pixels with the document origin at the widget origin, so document and widget points agree.
    view_->surface().setViewTransform(
        render::ViewTransform{1.0, {0.0, 0.0}, view_->surface().devicePixelRatio()});
}

void BrushSliceTest::cleanup() {
    view_.reset();
    session_.reset();
    evaluator_.reset();
}

QPointF BrushSliceTest::widgetPoint(double docX, double docY) const {
    const auto logical = view_->surface().viewTransform().documentToLogical(base::DPoint{docX, docY});
    return {logical.x, logical.y};
}

void BrushSliceTest::press(QPointF at) {
    QMouseEvent event(QEvent::MouseButtonPress, at, at, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view_->surface().widget(), &event);
}

void BrushSliceTest::move(QPointF at) {
    QMouseEvent event(QEvent::MouseMove, at, at, Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view_->surface().widget(), &event);
}

void BrushSliceTest::release(QPointF at) {
    QMouseEvent event(QEvent::MouseButtonRelease, at, at, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QCoreApplication::sendEvent(view_->surface().widget(), &event);
}

std::size_t BrushSliceTest::paintTiles() const {
    return session_->revision()->state->findLayer(session_->activeLayer())->content->tiles.tileCount();
}

void BrushSliceTest::strokeCommitsOneHistoryItem() {
    press(widgetPoint(40, 40));
    for (int i = 1; i <= 30; ++i) {
        move(widgetPoint(40 + i * 10, 40 + i * 8));
    }
    QVERIFY2(view_->tool()->isInteracting(), "the stroke is in progress");
    QCOMPARE(session_->revision()->sequence, 2u); // the preview is not a revision
    QVERIFY(view_->displayedState() != session_->revision()->state);
    release(widgetPoint(340, 280));
    QVERIFY(!view_->tool()->isInteracting());
    QTRY_COMPARE(session_->revision()->sequence, 3u);
    const auto history = session_->executor().history();
    QCOMPARE(history.size(), std::size_t{3});
    QCOMPARE(QString::fromStdString(history.back().label), QStringLiteral("Brush Stroke"));
    QVERIFY(paintTiles() > 0);
    QTRY_VERIFY(view_->displayedState() ==
                session_->revision()->state); // the preview gave way to the revision
    // The presented frame shows the black stroke over the white background.
    view_->surface().requestFrame();
    QTRY_VERIFY(!view_->surface().lastFrame().isNull());
    const QImage frame = view_->surface().lastFrame();
    const auto device = view_->surface().viewTransform().documentToDevice(base::DPoint{190.5, 160.5});
    QCOMPARE(QColor(frame.pixel(static_cast<int>(device.x), static_cast<int>(device.y))).value(), 0);
    QVERIFY(session_->executor().undo());
    QTRY_COMPARE(paintTiles(), std::size_t{0});
}

void BrushSliceTest::escapeCancelsTheStroke() {
    press(widgetPoint(50, 50));
    move(widgetPoint(150, 150));
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(view_->surface().widget(), &escape);
    QVERIFY(!view_->tool()->isInteracting());
    release(widgetPoint(150, 150));
    QTest::qWait(100);
    QCOMPARE(session_->revision()->sequence, 2u);
    QCOMPARE(paintTiles(), std::size_t{0});
    QVERIFY(view_->displayedState() == session_->revision()->state);
}

void BrushSliceTest::lockedLayerReportsAnError() {
    QVERIFY(session_->executor().execute(commands::CommandEnvelope::make(
        "archmage.layer.setLocked", {{"locked", true}}, {session_->activeLayer().toString()})));
    QTRY_COMPARE(session_->revision()->sequence, 3u);
    QSignalSpy errors(view_.get(), &ui::DocumentView::errorReported);
    press(widgetPoint(50, 50));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!view_->tool()->isInteracting());
    release(widgetPoint(50, 50));
    QCOMPARE(session_->revision()->sequence, 3u);
}

void BrushSliceTest::focusLossCommitsTheStroke() {
    press(widgetPoint(60, 60));
    move(widgetPoint(200, 60));
    QFocusEvent focusOut(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
    QCoreApplication::sendEvent(view_->surface().widget(), &focusOut);
    QVERIFY(!view_->tool()->isInteracting());
    QTRY_COMPARE(session_->revision()->sequence, 3u);
    QVERIFY(paintTiles() > 0);
}

QTEST_MAIN(BrushSliceTest)
#include "BrushSliceTest.moc"
