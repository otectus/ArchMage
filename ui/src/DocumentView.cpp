#include "archmage/ui/DocumentView.h"

#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/input/InputMapper.h"

#include <QKeyEvent>
#include <QVBoxLayout>

namespace archmage::ui {

DocumentView::DocumentView(DocumentSession &session, QWidget *parent)
    : QWidget(parent), session_(session), frameSource_(session.evaluator()) {
    setObjectName(QStringLiteral("archmageDocumentView"));
    QString error;
    surface_ = canvas::CanvasSurface::create(canvas::CanvasSurface::defaultKind(), &error);
    if (!surface_) { // the requested GPU adapter is unavailable: fall back to the safe raster surface
        surface_ = canvas::CanvasSurface::create(canvas::CanvasSurface::Kind::Raster, &error);
    }
    surface_->setFrameSource(&frameSource_);
    surface_->setInputSink(this);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(surface_->widget());
    setFocusProxy(surface_->widget());
    surface_->widget()->installEventFilter(this); // focus lives on the canvas widget

    connect(&session_, &DocumentSession::revisionChanged, this, [this] { onRevisionChanged(); });
    connect(&session_, &DocumentSession::commandFailed, this, [this](const QString &message) {
        clearPreview();
        reportError(message);
    });
    connect(surface_.get(), &canvas::CanvasSurface::geometryChanged, this, [this](QSize size, double) {
        if (!viewInitialized_ && !size.isEmpty()) {
            fitToView();
        }
    });
    refresh();
}

DocumentView::~DocumentView() {
    if (tool_) {
        tool_->deactivate();
    }
    surface_->setInputSink(nullptr);
}

std::shared_ptr<const document::DocumentState> DocumentView::displayedState() const {
    return preview_ ? preview_ : session_.revision()->state;
}

void DocumentView::showPreview(std::shared_ptr<const document::DocumentState> state) {
    if (!preview_) {
        previewBaseRevision_ = session_.revision()->sequence;
    }
    preview_ = std::move(state);
    refresh();
}

void DocumentView::clearPreview() {
    preview_.reset();
    refresh();
}

void DocumentView::reportError(const QString &message) { emit errorReported(message); }

void DocumentView::onRevisionChanged() {
    // A committed stroke replaces its preview; an interaction in progress keeps showing its own preview.
    if (preview_ && !(tool_ && tool_->isInteracting()) &&
        session_.revision()->sequence > previewBaseRevision_) {
        preview_.reset();
    }
    refresh();
}

void DocumentView::refresh() {
    frameSource_.setState(displayedState());
    surface_->requestFrame();
}

void DocumentView::setTool(std::unique_ptr<Tool> tool) {
    if (tool_) {
        tool_->deactivate();
    }
    tool_ = std::move(tool);
    if (tool_) {
        tool_->activate(*this);
    }
}

void DocumentView::fitToView() {
    const QSize device = surface_->deviceSize();
    const auto state = displayedState();
    if (device.isEmpty() || !state) {
        return;
    }
    viewInitialized_ = true;
    const double dpr = surface_->devicePixelRatio();
    surface_->setViewTransform(render::ViewTransform::fit(
        render::DRect{0.0, 0.0, static_cast<double>(state->width), static_cast<double>(state->height)},
        render::DSize{static_cast<double>(device.width()), static_cast<double>(device.height())}, dpr,
        24.0 * dpr));
}

bool DocumentView::canvasEvent(canvas::CanvasSurface &surface, QEvent *event) {
    const std::int64_t received = canvas::LatencyTrace::nowNs();
    if (const auto sample = input::InputMapper::pointerSample(*event, surface.viewTransform(), received)) {
        surface.latency().markEnqueued(canvas::LatencyTrace::nowNs());
        return tool_ != nullptr && tool_->pointer(*sample);
    }
    if (const auto navigation = input::InputMapper::navigation(*event, surface.viewTransform())) {
        const render::ViewTransform &view = surface.viewTransform();
        surface.setViewTransform(
            navigation->kind == input::NavigationRequest::Kind::ZoomAbout
                ? view.zoomedAbout(view.scale() * navigation->zoomFactor, navigation->anchorDevice)
                : view.panned(navigation->panDevice));
        viewInitialized_ = true;
        return true;
    }
    if (event->type() == QEvent::KeyPress && tool_ != nullptr && tool_->isInteracting()) {
        const auto *key = static_cast<QKeyEvent *>(event);
        if (key->key() == Qt::Key_Escape) {
            tool_->cancel();
            return true;
        }
        if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
            tool_->commit();
            return true;
        }
    }
    return false;
}

bool DocumentView::eventFilter(QObject *watched, QEvent *event) {
    if (watched == surface_->widget() && event->type() == QEvent::FocusOut && tool_ != nullptr &&
        tool_->isInteracting()) {
        tool_->commit(); // spec §16.3: an interrupted stroke keeps what was drawn
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace archmage::ui
