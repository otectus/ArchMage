#pragma once

#include "archmage/render/ViewTransform.h"
#include "archmage/ui/canvas/LatencyTrace.h"

#include <QImage>
#include <QObject>
#include <QSize>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

class QEvent;
class QPainter;
class QTimer;
class QWidget;

namespace archmage::ui::canvas {

class CanvasSurface;
class FrameSource;

/// Receives pointer, tablet, wheel and gesture events delivered to a canvas surface.
/// Not owned by the surface; must outlive it or be detached with setInputSink(nullptr).
class CanvasInputSink {
public:
    virtual ~CanvasInputSink() = default;
    /// Return true when the event was consumed.
    virtual bool canvasEvent(CanvasSurface &surface, QEvent *event) = 0;
};

/// Draws overlays (cursors, guides, marching ants) on top of the frame, in device pixels.
using OverlayPainter = std::function<void(QPainter &painter, const render::ViewTransform &view)>;

/// Presentation adapter for the document canvas (D-11, spec §5.2). Every implementation presents a
/// CPU-rendered frame (FrameSource plus overlay) covering the surface in device pixels; they differ in
/// how the frame reaches the screen: QWidget painting, QRhiWidget, or a QVulkanWindow embedded with
/// QWidget::createWindowContainer.
///
/// Ownership: the surface owns its widget until the widget is given a Qt parent (for example by a
/// layout); after that the parent owns it. Deleting the surface first is safe.
/// Thread safety: GUI thread only.
class CanvasSurface : public QObject {
    Q_OBJECT

public:
    enum class Kind { Raster, Rhi, Vulkan };

    static QString kindName(Kind kind);
    static std::optional<Kind> kindFromName(const QString &name);
    /// The adapter chosen by ADR-0011: raster, unless ARCHMAGE_CANVAS names another kind ("vulkan" opts in
    /// to the Vulkan window adapter, "rhi" to the experimental QRhiWidget one). Unknown names fall back to
    /// raster.
    static Kind defaultKind();
    /// Creates a surface; nullptr with `error` set when this platform cannot provide the kind.
    static std::unique_ptr<CanvasSurface> create(Kind kind, QString *error);

    ~CanvasSurface() override;

    virtual QWidget *widget() = 0;
    [[nodiscard]] virtual Kind kind() const = 0;
    /// Human-readable description of the presentation path, e.g. the GPU and API in use.
    [[nodiscard]] virtual QString backendDescription() const = 0;
    /// Schedules one new frame; repeated requests before it is produced coalesce.
    virtual void requestFrame() = 0;

    void setFrameSource(FrameSource *source);
    void setOverlayPainter(OverlayPainter painter);
    void setInputSink(CanvasInputSink *sink);
    /// Replaces the view (its device pixel ratio is overridden by the surface's own) and requests a frame.
    void setViewTransform(const render::ViewTransform &view);

    [[nodiscard]] const render::ViewTransform &viewTransform() const { return view_; }
    [[nodiscard]] double devicePixelRatio() const { return dpr_; }
    [[nodiscard]] QSize deviceSize() const { return deviceSize_; }
    /// The frame most recently handed to the platform; null before the first frame.
    [[nodiscard]] QImage lastFrame() const { return frame_; }
    [[nodiscard]] std::uint64_t framesPresented() const { return frames_; }
    LatencyTrace &latency() { return latency_; }

    /// Pointer, tablet, wheel, gesture, enter, leave and key events: the events surfaces route to the sink.
    static bool isCanvasInputEvent(const QEvent *event);

signals:
    void geometryChanged(QSize deviceSize, double devicePixelRatio);
    void deviceLost(const QString &reason);
    void framePresented();

protected:
    explicit CanvasSurface(QObject *parent = nullptr);

    /// Renders the frame for the current view at `deviceSize`; called by implementations when the
    /// platform asks for pixels. Updates the geometry (and the view's device pixel ratio) first.
    const QImage &produceFrame(QSize deviceSize, double devicePixelRatio);
    /// Implementations call this when the frame was handed to the platform.
    void frameSubmitted();
    /// Routes an input event to the sink; returns true when consumed.
    bool deliverInput(QEvent *event);
    void reportDeviceLost(const QString &reason);
    /// Geometry known before a frame is rendered (resize, screen change).
    void updateGeometry(QSize deviceSize, double devicePixelRatio);

private:
    FrameSource *source_ = nullptr;
    OverlayPainter overlay_;
    CanvasInputSink *sink_ = nullptr;
    render::ViewTransform view_;
    QSize deviceSize_;
    double dpr_ = 1.0;
    QImage frame_;
    std::uint64_t frames_ = 0;
    LatencyTrace latency_;
    QTimer *traceTimer_ = nullptr;
};

} // namespace archmage::ui::canvas
