// S1-1: the default canvas adapter (ADR-0011) and the raster surface presenting CPU frames.

#include "archmage/ui/canvas/CanvasSurface.h"
#include "archmage/commands/Command.h"
#include "archmage/render/CpuEvaluator.h"
#include "archmage/ui/canvas/DocumentFrameSource.h"
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
    void documentFramesSurviveCacheEvictionWithinAFrame();
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

void CanvasSurfaceTest::documentFramesSurviveCacheEvictionWithinAFrame() {
    using namespace archmage;
    // 256 tiles of distinct solid colors, more than the smallest display cache (64 tiles) holds.
    auto created = commands::createDocument(commands::CommandEnvelope::make(
        "archmage.document.create", {{"width", 2048}, {"height", 2048}, {"background", "transparent"}}));
    QVERIFY(created);
    document::DocumentState doc = created->state;
    auto layer = std::make_shared<document::Layer>(*doc.layers[0]);
    auto content = std::make_shared<document::RasterContent>(*layer->content);
    // The top half has distinct colors; the bottom half shares one tile object, so many visible tiles have
    // the same content key in one frame.
    const auto colorOf = [](std::int64_t tx, std::int64_t ty) {
        if (ty >= 8) {
            return std::array<std::uint8_t, 4>{10, 200, 30, 255};
        }
        return std::array<std::uint8_t, 4>{static_cast<std::uint8_t>(tx * 16),
                                           static_cast<std::uint8_t>(ty * 16),
                                           static_cast<std::uint8_t>(255 - tx * 8), 255};
    };
    const auto tileOf = [](const std::array<std::uint8_t, 4> &c) {
        const std::array<std::byte, 4> value{std::byte{c[0]}, std::byte{c[1]}, std::byte{c[2]},
                                             std::byte{c[3]}};
        return tiles::Tile::makeUniform(tiles::SampleType::UInt8, 4, value);
    };
    const auto shared = tileOf(colorOf(0, 8));
    for (std::int64_t ty = 0; ty < 16; ++ty) {
        for (std::int64_t tx = 0; tx < 16; ++tx) {
            content->tiles.set({tx, ty}, ty >= 8 ? shared : tileOf(colorOf(tx, ty)));
        }
    }
    layer->content = content;
    doc.layers[0] = layer;
    render::CpuEvaluator evaluator;
    ui::canvas::DocumentFrameSource source(evaluator, 1); // smallest cache: 64 display tiles
    source.setState(std::make_shared<const document::DocumentState>(doc));
    QImage frame(1024, 1024, QImage::Format_RGBA8888_Premultiplied);
    const render::ViewTransform view{0.5, {0.0, 0.0}, 1.0};
    for (int pass = 0; pass < 2; ++pass) {
        source.renderFrame(view, frame);
        for (std::int64_t ty = 0; ty < 16; ty += 5) {
            for (std::int64_t tx = 0; tx < 16; tx += 3) {
                const auto c = colorOf(tx, ty);
                const QColor seen =
                    frame.pixelColor(static_cast<int>(tx * 64 + 32), static_cast<int>(ty * 64 + 32));
                QCOMPARE(seen.red(), int(c[0]));
                QCOMPARE(seen.green(), int(c[1]));
                QCOMPARE(seen.blue(), int(c[2]));
            }
        }
    }
    QVERIFY(source.stats().displayMisses >= 128); // 128 distinct tiles plus one shared key
}

QTEST_MAIN(CanvasSurfaceTest)
#include "CanvasSurfaceTest.moc"
