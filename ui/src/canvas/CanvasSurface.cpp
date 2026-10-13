#include "archmage/ui/canvas/CanvasSurface.h"

#include "archmage/ui/canvas/FrameSource.h"
#include "archmage/ui/canvas/RasterCanvasSurface.h"
#include "archmage/ui/canvas/RhiCanvasSurface.h"
#include "archmage/ui/canvas/VulkanCanvasSurface.h"

#include <QEvent>
#include <QPainter>
#include <QTimer>

#include <cstdio>

namespace archmage::ui::canvas {

QString CanvasSurface::kindName(Kind kind) {
    switch (kind) {
    case Kind::Raster:
        return QStringLiteral("raster");
    case Kind::Rhi:
        return QStringLiteral("rhi");
    case Kind::Vulkan:
        return QStringLiteral("vulkan");
    }
    return {};
}

std::optional<CanvasSurface::Kind> CanvasSurface::kindFromName(const QString &name) {
    for (const Kind kind : {Kind::Raster, Kind::Rhi, Kind::Vulkan}) {
        if (name == kindName(kind)) {
            return kind;
        }
    }
    return std::nullopt;
}

CanvasSurface::Kind CanvasSurface::defaultKind() {
    const QString requested = qEnvironmentVariable("ARCHMAGE_CANVAS").trimmed().toLower();
    return kindFromName(requested).value_or(Kind::Raster);
}

std::unique_ptr<CanvasSurface> CanvasSurface::create(Kind kind, QString *error) {
    switch (kind) {
    case Kind::Raster:
        return std::make_unique<RasterCanvasSurface>();
    case Kind::Rhi:
        return std::make_unique<RhiCanvasSurface>();
    case Kind::Vulkan:
        return VulkanCanvasSurface::create(error);
    }
    if (error != nullptr) {
        *error = QStringLiteral("unknown canvas surface kind");
    }
    return nullptr;
}

CanvasSurface::CanvasSurface(QObject *parent) : QObject(parent) {
    if (LatencyTrace::enabledByEnvironment()) {
        traceTimer_ = new QTimer(this);
        traceTimer_->setInterval(1000);
        connect(traceTimer_, &QTimer::timeout, this, [this] {
            if (const auto stats = latency_.takeWindow()) {
                std::printf("LATENCY backend=%s frames=%d p50_ms=%.2f p95_ms=%.2f max_ms=%.2f "
                            "render_p50_ms=%.2f render_p95_ms=%.2f\n",
                            qPrintable(kindName(kind())), stats->frames, stats->p50Ms, stats->p95Ms,
                            stats->maxMs, stats->renderP50Ms, stats->renderP95Ms);
                std::fflush(stdout);
            }
        });
        traceTimer_->start();
    }
}

CanvasSurface::~CanvasSurface() = default;

void CanvasSurface::setFrameSource(FrameSource *source) {
    source_ = source;
    requestFrame();
}

void CanvasSurface::setOverlayPainter(OverlayPainter painter) {
    overlay_ = std::move(painter);
    requestFrame();
}

void CanvasSurface::setInputSink(CanvasInputSink *sink) { sink_ = sink; }

void CanvasSurface::setViewTransform(const render::ViewTransform &view) {
    view_ = render::ViewTransform{view.scale(), view.pan(), dpr_};
    requestFrame();
}

void CanvasSurface::updateGeometry(QSize deviceSize, double devicePixelRatio) {
    if (deviceSize == deviceSize_ && devicePixelRatio == dpr_) {
        return;
    }
    if (devicePixelRatio != dpr_ && !deviceSize.isEmpty()) {
        const render::DSize logical{deviceSize.width() / devicePixelRatio,
                                    deviceSize.height() / devicePixelRatio};
        view_ = view_.withDevicePixelRatio(devicePixelRatio, logical);
    }
    deviceSize_ = deviceSize;
    dpr_ = devicePixelRatio;
    emit geometryChanged(deviceSize_, dpr_);
}

const QImage &CanvasSurface::produceFrame(QSize deviceSize, double devicePixelRatio) {
    updateGeometry(deviceSize, devicePixelRatio);
    latency_.markRenderStart(LatencyTrace::nowNs());
    if (frame_.size() != deviceSize || frame_.format() != QImage::Format_RGBA8888_Premultiplied) {
        frame_ = QImage(deviceSize, QImage::Format_RGBA8888_Premultiplied);
    }
    if (source_ != nullptr) {
        source_->renderFrame(view_, frame_);
    } else {
        frame_.fill(QColor(0x30, 0x30, 0x30));
    }
    if (overlay_) {
        QPainter painter(&frame_);
        overlay_(painter, view_);
    }
    return frame_;
}

void CanvasSurface::frameSubmitted() {
    ++frames_;
    latency_.markSubmitted(LatencyTrace::nowNs());
    emit framePresented();
}

bool CanvasSurface::isCanvasInputEvent(const QEvent *event) {
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseButtonDblClick:
    case QEvent::MouseMove:
    case QEvent::TabletPress:
    case QEvent::TabletMove:
    case QEvent::TabletRelease:
    case QEvent::Wheel:
    case QEvent::NativeGesture:
    case QEvent::Enter:
    case QEvent::Leave:
    case QEvent::KeyPress:
    case QEvent::KeyRelease:
        return true;
    default:
        return false;
    }
}

bool CanvasSurface::deliverInput(QEvent *event) {
    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::Wheel:
    case QEvent::NativeGesture:
        latency_.markInput(LatencyTrace::nowNs());
        break;
    default:
        break;
    }
    return sink_ != nullptr && sink_->canvasEvent(*this, event);
}

void CanvasSurface::reportDeviceLost(const QString &reason) { emit deviceLost(reason); }

} // namespace archmage::ui::canvas
