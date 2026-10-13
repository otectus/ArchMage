// S1-7.3: the Layers panel over a 2,000-layer document creates no per-layer widgets and computes only the
// thumbnails it draws; an opacity drag is one history item; rename, visibility, lock, reorder and the
// panel's commands go through the executor.

#include "ShellTestSupport.h"

#include "archmage/brush/Stroke.h"
#include "archmage/commands/Command.h"
#include "archmage/ui/panels/LayersModel.h"
#include "archmage/ui/panels/LayersPanel.h"

#include <QListView>
#include <QMimeData>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStyleOptionSlider>

using namespace archmage;

class LayersPanelTest : public QObject {
    Q_OBJECT

private slots:
    void twoThousandLayersWithoutTwoThousandWidgets();
    void opacityDragIsOneHistoryItem();
    void editsBecomeCommands();
    void dragAndDropReorders();
    void thumbnailsFollowThePixels();
};

namespace {

document::DocumentState manyLayers(int count) {
    auto created = commands::createDocument(commands::CommandEnvelope::make(
        "archmage.document.create", {{"width", 640}, {"height", 480}, {"background", "white"}}));
    document::DocumentState doc = created->state;
    for (int i = 1; i < count; ++i) {
        doc.layers.push_back(document::makeRasterLayer(doc, "Layer " + std::to_string(i + 1)));
    }
    return doc;
}

std::uint64_t historySize(ui::DocumentSession &session) { return session.executor().history().size(); }

} // namespace

void LayersPanelTest::twoThousandLayersWithoutTwoThousandWidgets() {
    auto evaluator = std::make_shared<render::CpuEvaluator>();
    ui::DocumentSession session(manyLayers(2000), QStringLiteral("New Document"), evaluator);
    ui::LayersPanel panel;
    panel.resize(300, 600);
    panel.setSession(&session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QCOMPARE(panel.model().rowCount(), 2000);
    const auto widgets = panel.findChildren<QWidget *>().size();
    QVERIFY2(widgets < 40, qPrintable(QString::number(widgets)));
    // Only the rows on screen were drawn, so only their thumbnails exist.
    QTRY_VERIFY(panel.model().thumbnailsRendered() > 0);
    const auto visibleRows = static_cast<std::uint64_t>(panel.list().viewport()->height() / 40 + 2);
    QVERIFY2(panel.model().thumbnailsRendered() <= visibleRows,
             qPrintable(QStringLiteral("%1 thumbnails for about %2 visible rows").arg(panel.model().thumbnailsRendered()).arg(visibleRows)));
    // Scrolling through the whole list keeps the cache bounded.
    QScrollBar *bar = panel.list().verticalScrollBar();
    for (int value = 0; value <= bar->maximum(); value += bar->pageStep()) {
        bar->setValue(value);
        QCoreApplication::processEvents();
    }
    QVERIFY(panel.model().thumbnailsCached() <= ui::LayersModel::kThumbnailCacheLimit);
    QCOMPARE(panel.list().currentIndex().row(), 0); // the active layer (the top one) is selected
}

void LayersPanelTest::opacityDragIsOneHistoryItem() {
    auto evaluator = std::make_shared<render::CpuEvaluator>();
    ui::DocumentSession session(manyLayers(3), QStringLiteral("New Document"), evaluator);
    ui::LayersPanel panel;
    panel.resize(320, 400);
    panel.setSession(&session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowExposed(&panel));
    QSlider &slider = panel.opacitySlider();
    QCOMPARE(slider.value(), 100);
    const std::uint64_t before = historySize(session);

    // A real drag of the handle, from 100 % towards the left end.
    QStyleOptionSlider option;
    option.initFrom(&slider);
    option.orientation = slider.orientation();
    option.minimum = slider.minimum();
    option.maximum = slider.maximum();
    option.sliderPosition = slider.sliderPosition();
    option.sliderValue = slider.value();
    const QRect handle = slider.style()->subControlRect(QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
    const QPoint start = handle.center();
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, start);
    QVERIFY(slider.isSliderDown());
    for (int i = 1; i <= 10; ++i) {
        QMouseEvent move(QEvent::MouseMove, QPointF(start.x() - i * slider.width() / 25, start.y()), slider.mapToGlobal(QPointF(start)),
                         Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&slider, &move);
    }
    const int dragged = slider.value();
    QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, QPoint(start.x() - 10 * slider.width() / 25, start.y()));
    QVERIFY2(dragged < 90, qPrintable(QString::number(dragged)));

    session.executor().drain();
    QTRY_COMPARE(session.revision()->sequence, session.executor().current()->sequence);
    QCOMPARE(historySize(session), before + 1); // one history item for the whole drag (spec §9.2)
    QCOMPARE(QString::fromStdString(session.executor().history().back().label), QStringLiteral("Layer Opacity"));
    QCOMPARE(std::lround(session.revision()->state->layers.back()->opacity * 100.0f), static_cast<long>(slider.value()));

    // A second drag is a second item; typing a value is one more.
    QTest::mousePress(&slider, Qt::LeftButton, Qt::NoModifier, slider.rect().center());
    QTest::mouseRelease(&slider, Qt::LeftButton, Qt::NoModifier, slider.rect().center());
    panel.opacityValue().setValue(42);
    session.executor().drain();
    QTRY_COMPARE(session.revision()->sequence, session.executor().current()->sequence);
    QVERIFY(historySize(session) <= before + 3);
    QCOMPARE(std::lround(session.revision()->state->layers.back()->opacity * 100.0f), 42L);
}

void LayersPanelTest::editsBecomeCommands() {
    auto evaluator = std::make_shared<render::CpuEvaluator>();
    ui::DocumentSession session(manyLayers(2), QStringLiteral("New Document"), evaluator);
    ui::LayersPanel panel;
    panel.setSession(&session);
    ui::LayersModel &model = panel.model();
    QCOMPARE(model.index(0).data().toString(), QStringLiteral("Layer 2"));
    QCOMPARE(model.index(1).data().toString(), QStringLiteral("Background"));

    QVERIFY(model.setData(model.index(0), QStringLiteral("Ink"), Qt::EditRole));
    QVERIFY(model.setData(model.index(1), false, ui::LayersModel::VisibleRole));
    QVERIFY(model.setData(model.index(0), true, ui::LayersModel::LockedRole));
    QVERIFY(testing::waitForRevision(session, 4));
    QCOMPARE(model.index(0).data().toString(), QStringLiteral("Ink"));
    QCOMPARE(model.index(1).data(ui::LayersModel::VisibleRole).toBool(), false);
    QCOMPARE(model.index(0).data(ui::LayersModel::LockedRole).toBool(), true);
    QVERIFY(!model.setData(model.index(0), QStringLiteral("   "), Qt::EditRole)) ; // empty names are refused

    panel.newLayerAction()->trigger(); // above the active (top) layer, and becomes active
    QVERIFY(testing::waitForRevision(session, 5));
    QTRY_COMPARE(model.rowCount(), 3);
    QTRY_COMPARE(session.activeLayer(), session.revision()->state->layers.back()->id);
    panel.duplicateLayerAction()->trigger();
    QVERIFY(testing::waitForRevision(session, 6));
    QTRY_COMPARE(model.rowCount(), 4);
    QCOMPARE(panel.list().currentIndex().row(), 0);
    panel.deleteLayerAction()->trigger();
    QVERIFY(testing::waitForRevision(session, 7));
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(session.executor().history().size(), 7u);
}

void LayersPanelTest::dragAndDropReorders() {
    auto evaluator = std::make_shared<render::CpuEvaluator>();
    ui::DocumentSession session(manyLayers(4), QStringLiteral("New Document"), evaluator);
    ui::LayersPanel panel;
    panel.setSession(&session);
    ui::LayersModel &model = panel.model();
    const auto names = [&] {
        QStringList out;
        for (int row = 0; row < model.rowCount(); ++row) {
            out << model.index(row).data().toString();
        }
        return out;
    };
    QCOMPARE(names(), (QStringList{QStringLiteral("Layer 4"), QStringLiteral("Layer 3"), QStringLiteral("Layer 2"),
                                   QStringLiteral("Background")}));
    // Drag the top row below "Layer 2" (an insertion point before row 3).
    std::unique_ptr<QMimeData> mime(model.mimeData({model.index(0)}));
    QVERIFY(!model.dropMimeData(mime.get(), Qt::MoveAction, 3, 0, {})); // false: the move arrives as a revision
    QVERIFY(testing::waitForRevision(session, 2));
    QCOMPARE(names(), (QStringList{QStringLiteral("Layer 3"), QStringLiteral("Layer 2"), QStringLiteral("Layer 4"),
                                   QStringLiteral("Background")}));
    // Drop on a row: take its place.
    mime.reset(model.mimeData({model.index(3)}));
    model.dropMimeData(mime.get(), Qt::MoveAction, -1, -1, model.index(0));
    QVERIFY(testing::waitForRevision(session, 3));
    QCOMPARE(names().front(), QStringLiteral("Background"));
    // Dropping a layer on its own place does nothing.
    QVERIFY(!model.moveLayerToRow(model.layerAt(1), 1));
    QVERIFY(!model.moveLayerToRow(model.layerAt(1), 2));
}

void LayersPanelTest::thumbnailsFollowThePixels() {
    auto evaluator = std::make_shared<render::CpuEvaluator>();
    ui::DocumentSession session(manyLayers(2), QStringLiteral("New Document"), evaluator);
    ui::LayersModel model;
    model.setSession(&session);
    const QImage before = model.index(0).data(Qt::DecorationRole).value<QImage>();
    QVERIFY(!before.isNull());
    QCOMPARE(model.thumbnailsRendered(), 1u);
    (void)model.index(0).data(Qt::DecorationRole); // from the cache
    QCOMPARE(model.thumbnailsRendered(), 1u);

    brush::BrushSettings settings;
    settings.color = {1.0f, 0.0f, 0.0f};
    settings.size = 120.0;
    settings.hardness = 1.0;
    std::vector<brush::StrokeSample> samples;
    for (int i = 0; i <= 20; ++i) {
        samples.push_back({{100.0 + i * 20.0, 240.0}, 1.0f, i * 4'166'667LL});
    }
    nlohmann::json parameters = settings.toJson();
    parameters["samples"] = brush::samplesToJson(samples);
    session.submit(commands::CommandEnvelope::make("archmage.raster.paintStroke", parameters,
                                                   {session.revision()->state->layers.back()->id.toString()}));
    QVERIFY(testing::waitForRevision(session, 2));
    const QImage after = model.index(0).data(Qt::DecorationRole).value<QImage>();
    QCOMPARE(model.thumbnailsRendered(), 2u);
    QVERIFY(after != before);
    // The stroke crosses the middle of the canvas: the thumbnail's middle is red.
    const QColor middle = after.pixelColor(after.width() / 2, after.height() / 2);
    QVERIFY2(middle.red() > 200 && middle.green() < 60, qPrintable(middle.name()));
}

QTEST_MAIN(LayersPanelTest)
#include "LayersPanelTest.moc"
