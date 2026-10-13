#include "archmage/ui/canvas/RasterCanvasSurface.h"

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QWidget>

#include <cmath>

namespace archmage::ui::canvas {

class RasterCanvasSurface::Widget final : public QWidget {
public:
    explicit Widget(RasterCanvasSurface *surface) : surface_(surface) {
        setObjectName(QStringLiteral("archmageCanvas"));
        setAttribute(Qt::WA_OpaquePaintEvent);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TabletTracking);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setMinimumSize(64, 64);
    }

    void detach() { surface_ = nullptr; }

protected:
    bool event(QEvent *event) override {
        if (surface_ != nullptr && CanvasSurface::isCanvasInputEvent(event) &&
            surface_->deliverInput(event)) {
            return true;
        }
        if (surface_ != nullptr && event->type() == QEvent::DevicePixelRatioChange) {
            surface_->updateGeometry(deviceSize(), devicePixelRatioF());
            update();
        }
        return QWidget::event(event);
    }

    void resizeEvent(QResizeEvent *event) override {
        QWidget::resizeEvent(event);
        if (surface_ != nullptr) {
            surface_->updateGeometry(deviceSize(), devicePixelRatioF());
        }
    }

    void paintEvent(QPaintEvent * /*event*/) override {
        QPainter painter(this);
        if (surface_ == nullptr) {
            painter.fillRect(rect(), Qt::black);
            return;
        }
        QImage frame = surface_->produceFrame(deviceSize(), devicePixelRatioF());
        frame.setDevicePixelRatio(devicePixelRatioF());
        painter.drawImage(QPointF(0.0, 0.0), frame);
        painter.end();
        surface_->frameSubmitted();
    }

private:
    [[nodiscard]] QSize deviceSize() const {
        const double ratio = devicePixelRatioF();
        return {static_cast<int>(std::ceil(width() * ratio)), static_cast<int>(std::ceil(height() * ratio))};
    }

    RasterCanvasSurface *surface_;
};

RasterCanvasSurface::RasterCanvasSurface(QObject *parent)
    : CanvasSurface(parent), widget_(new Widget(this)) {}

RasterCanvasSurface::~RasterCanvasSurface() {
    if (auto *w = static_cast<Widget *>(widget_.data())) {
        w->detach();
        if (w->parent() == nullptr) {
            delete w;
        }
    }
}

QWidget *RasterCanvasSurface::widget() { return widget_; }

QString RasterCanvasSurface::backendDescription() const {
    return QStringLiteral("QWidget raster painting (QPainter into the window backing store)");
}

void RasterCanvasSurface::requestFrame() {
    if (widget_ != nullptr) {
        widget_->update();
    }
}

} // namespace archmage::ui::canvas
