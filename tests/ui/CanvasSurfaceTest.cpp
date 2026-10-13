// S1-1: the default canvas adapter (ADR-0011) and the raster surface presenting CPU frames.

#include "archmage/ui/canvas/CanvasSurface.h"
#include "archmage/ui/canvas/FrameSource.h"
#include "archmage/ui/canvas/SyntheticFrameSource.h"

#include <QImage>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

using archmage::base::DPoint;
using archmage::render::ViewTransform;
using archmage::ui::canvas::CanvasSurface;
using archmage::ui::canvas::SyntheticFrameSource;

class CanvasSurfaceTest : public QObject {
    Q_OBJECT

private slots:
    void defaultKindIsRasterUnlessRequested();
    void rasterSurfacePresentsTheSourceFrame();
};

void CanvasSurfaceTest::defaultKindIsRasterUnlessRequested() {
    qunsetenv("ARCHMAGE_CANVAS");
    QCOMPARE(CanvasSurface::defaultKind(), CanvasSurface::Kind::Raster);
    qputenv("ARCHMAGE_CANVAS", "vulkan");
    QCOMPARE(CanvasSurface::defaultKind(), CanvasSurface::Kind::Vulkan);
    qputenv("ARCHMAGE_CANVAS", " RHI ");
    QCOMPARE(CanvasSurface::defaultKind(), CanvasSurface::Kind::Rhi);
    qputenv("ARCHMAGE_CANVAS", "metal");
    QCOMPARE(CanvasSurface::defaultKind(), CanvasSurface::Kind::Raster);
    qunsetenv("ARCHMAGE_CANVAS");
}

void CanvasSurfaceTest::rasterSurfacePresentsTheSourceFrame() {
    QString error;
    auto surface = CanvasSurface::create(CanvasSurface::Kind::Raster, &error);
    QVERIFY2(surface != nullptr, qPrintable(error));
    SyntheticFrameSource source(QSize(512, 512));
    surface->setFrameSource(&source);
    surface->setViewTransform(ViewTransform{1.0, DPoint{0.0, 0.0}, 1.0});

    QWidget *widget = surface->widget();
    widget->resize(300, 200);
    QSignalSpy presented(surface.get(), &CanvasSurface::framePresented);
    widget->show();
    QVERIFY(QTest::qWaitForWindowExposed(widget));
    QTRY_VERIFY_WITH_TIMEOUT(presented.count() > 0, 2000);

    const QImage frame = surface->lastFrame();
    QCOMPARE(frame.size(), QSize(300, 200));
    QCOMPARE(frame.format(), QImage::Format_RGBA8888_Premultiplied);
    // The presented frame is exactly what the source renders for the same view and size.
    QImage expected(frame.size(), QImage::Format_RGBA8888_Premultiplied);
    source.renderFrame(surface->viewTransform(), expected);
    QCOMPARE(frame, expected);
    // Document origin marker (white) at the top-left with the identity view.
    QCOMPARE(frame.pixelColor(1, 1), QColor(Qt::white));
    QVERIFY(surface->framesPresented() >= 1);
}

QTEST_MAIN(CanvasSurfaceTest)
#include "CanvasSurfaceTest.moc"
