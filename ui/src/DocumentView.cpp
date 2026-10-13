#include "archmage/ui/DocumentView.h"

#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/input/InputMapper.h"
#include "archmage/ui/tools/NavigationTools.h"

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
    surface_->widget()->setAccessibleName(QStringLiteral("Canvas"));
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
    connect(surface_.get(), &canvas::CanvasSurface::viewChanged, this, &DocumentView::viewChanged);
    refresh();
}

DocumentView::~DocumentView() {
    endTemporaryHand();
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

void DocumentView::setCanvasCursor(const QCursor &cursor) { surface_->widget()->setCursor(cursor); }

void DocumentView::onRevisionChanged() {
    // A committed stroke replaces its preview; an interaction in progress keeps showing its own preview.
    const Tool *active = effectiveTool();
    if (preview_ && !(active != nullptr && active->isInteracting()) &&
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
    endTemporaryHand();
    if (tool_) {
        tool_->deactivate();
    }
    tool_ = std::move(tool);
    if (tool_) {
        tool_->activate(*this);
    } else {
        setCanvasCursor(Qt::ArrowCursor);
    }
}

void DocumentView::suspendInteraction() {
    endTemporaryHand();
    if (tool_ && tool_->isInteracting()) {
        tool_->suspend();
    }
}

void DocumentView::beginTemporaryHand() {
    if (temporary_) {
        return;
    }
    if (tool_) {
        tool_->suspend();
    }
    temporary_ = std::make_unique<HandTool>();
    temporary_->activate(*this);
}

void DocumentView::endTemporaryHand() {
    if (!temporary_) {
        return;
    }
    temporary_->deactivate();
    temporary_.reset();
    if (tool_) {
        tool_->resume();
    } else {
        setCanvasCursor(Qt::ArrowCursor);
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

void DocumentView::actualPixels() {
    const QSize device = surface_->deviceSize();
    if (device.isEmpty()) {
        return;
    }
    viewInitialized_ = true;
    const render::ViewTransform &view = surface_->viewTransform();
    const base::DPoint center{device.width() / 2.0, device.height() / 2.0};
    surface_->setViewTransform(render::ViewTransform::actualPixels(
        view.deviceToDocument(center),
        render::DSize{static_cast<double>(device.width()), static_cast<double>(device.height())},
        surface_->devicePixelRatio()));
}

void DocumentView::zoomBy(double factor) {
    const QSize device = surface_->deviceSize();
    if (device.isEmpty()) {
        return;
    }
    viewInitialized_ = true;
    const render::ViewTransform &view = surface_->viewTransform();
    surface_->setViewTransform(
        view.zoomedAbout(view.scale() * factor, base::DPoint{device.width() / 2.0, device.height() / 2.0}));
}

bool DocumentView::canvasEvent(canvas::CanvasSurface &surface, QEvent *event) {
    const std::int64_t received = canvas::LatencyTrace::nowNs();
    if (const auto sample = input::InputMapper::pointerSample(*event, surface.viewTransform(), received)) {
        surface.latency().markEnqueued(canvas::LatencyTrace::nowNs());
        emit pointerMoved(sample->posDocument.x, sample->posDocument.y);
        Tool *active = effectiveTool();
        return active != nullptr && active->pointer(*sample);
    }
    if (event->type() == QEvent::Leave) {
        emit pointerLeft();
        return false;
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
    if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
        const auto *key = static_cast<QKeyEvent *>(event);
        const bool press = event->type() == QEvent::KeyPress;
        if (key->key() == Qt::Key_Space && key->modifiers() == Qt::NoModifier) {
            if (!key->isAutoRepeat()) {
                if (press && !(tool_ && tool_->isInteracting())) {
                    beginTemporaryHand();
                } else if (!press) {
                    endTemporaryHand();
                }
            }
            return true; // Space over the canvas is the temporary hand, repeats included
        }
        if (!press) {
            return false;
        }
        if (key->key() == Qt::Key_Tab && key->modifiers() == Qt::NoModifier) {
            emit panelsToggleRequested();
            return true;
        }
        Tool *active = effectiveTool();
        if (active != nullptr && active->isInteracting()) {
            if (key->key() == Qt::Key_Escape) {
                active->cancel();
                return true;
            }
            if (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) {
                active->commit();
                return true;
            }
        }
    }
    return false;
}

bool DocumentView::eventFilter(QObject *watched, QEvent *event) {
    if (watched == surface_->widget() && event->type() == QEvent::FocusOut) {
        // Spec §16.3: an interrupted interaction keeps what was drawn; the Space release goes elsewhere.
        endTemporaryHand();
        if (tool_ != nullptr && tool_->isInteracting()) {
            tool_->commit();
        }
    }
    return QWidget::eventFilter(watched, event);
}

} // namespace archmage::ui
