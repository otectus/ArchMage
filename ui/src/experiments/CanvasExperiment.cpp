#include "archmage/ui/experiments/CanvasExperiment.h"

#include "archmage/ui/canvas/SyntheticFrameSource.h"
#include "archmage/ui/canvas/VulkanCanvasSurface.h"
#include "archmage/ui/canvas/VulkanSupport.h"
#include "archmage/ui/input/InputMapper.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QMouseEvent>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QSpinBox>
#include <QStatusBar>
#include <QTimer>
#include <QToolBar>
#include <QToolTip>
#include <QVBoxLayout>
#include <QVulkanWindow>
#include <QWindow>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <numbers>

namespace archmage::ui::experiments {
namespace {

using canvas::CanvasSurface;

std::atomic<int> gQtValidationMessages{0};

void emitLine(const QString &line) {
    std::printf("%s\n", qPrintable(line));
    std::fflush(stdout);
}

void emitEvidence(const QString &kind, const QJsonObject &data) {
    emitLine(QStringLiteral("EVIDENCE %1 %2")
                 .arg(kind, QString::fromUtf8(QJsonDocument(data).toJson(QJsonDocument::Compact))));
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message) {
    if (message.contains(QLatin1String("Validation")) || message.contains(QLatin1String("VUID"))) {
        ++gQtValidationMessages;
    }
    std::printf("QTMSG %d %s: %s\n", static_cast<int>(type),
                context.category != nullptr ? context.category : "default", qPrintable(message));
    std::fflush(stdout);
}

QJsonArray rectJson(const QRect &r) { return {r.x(), r.y(), r.width(), r.height()}; }

QString describeObject(const QObject *object) {
    if (object == nullptr) {
        return QStringLiteral("none");
    }
    const QString name = object->objectName();
    return QStringLiteral("%1(%2)").arg(QString::fromLatin1(object->metaObject()->className()),
                                        name.isEmpty() ? QStringLiteral("-") : name);
}

class ExperimentWindow final : public QMainWindow, public canvas::CanvasInputSink {
public:
    ExperimentWindow(std::unique_ptr<CanvasSurface> surface, CanvasExperimentOptions options)
        : surface_(std::move(surface)), options_(std::move(options)), source_(QSize(4096, 4096)) {
        setObjectName(QStringLiteral("archmageExperimentWindow"));
        setWindowTitle(
            QStringLiteral("ArchMage canvas experiment: %1").arg(CanvasSurface::kindName(surface_->kind())));
        QDir().mkpath(options_.evidenceDirectory);

        surface_->setFrameSource(&source_);
        surface_->setInputSink(this);
        surface_->setOverlayPainter(
            [this](QPainter &painter, const render::ViewTransform &) { paintOverlay(painter); });
        setCentralWidget(surface_->widget());
        if (auto *vulkan = dynamic_cast<canvas::VulkanCanvasSurface *>(surface_.get())) {
            vulkan->vulkanWindow()->setObjectName(QStringLiteral("archmageCanvasWindow"));
        }

        buildMenus();
        buildToolBar();
        buildDocks();
        buildStatusBar();

        QPalette tips = QToolTip::palette();
        tips.setColor(QPalette::ToolTipBase, QColor(0x00, 0xff, 0xff));
        tips.setColor(QPalette::ToolTipText, Qt::black);
        QToolTip::setPalette(tips);

        connect(surface_.get(), &CanvasSurface::framePresented, this, [this] { onFramePresented(); });
        connect(surface_.get(), &CanvasSurface::geometryChanged, this, [this](QSize, double) {
            if (!viewInitialized_) {
                fitView();
            }
            updateStatus();
        });
        connect(surface_.get(), &CanvasSurface::deviceLost, this, [](const QString &reason) {
            emitEvidence(QStringLiteral("device-lost"), {{QStringLiteral("reason"), reason}});
        });
        resize(1024, 600);
    }

    ~ExperimentWindow() override {
        // The surface's widget is owned by this window; detach the surface before Qt deletes it.
        surface_->setInputSink(nullptr);
    }

    bool canvasEvent(CanvasSurface &surface, QEvent *event) override {
        const std::int64_t now = canvas::LatencyTrace::nowNs();
        if (const auto sample = input::InputMapper::pointerSample(*event, surface.viewTransform(), now)) {
            surface.latency().markEnqueued(canvas::LatencyTrace::nowNs());
            pointerDevice_ = QPointF(sample->posDevice.x, sample->posDevice.y);
            pointerDocument_ = QPointF(sample->posDocument.x, sample->posDocument.y);
            pointerInside_ = true;
            surface.requestFrame();
            return true;
        }
        if (const auto nav = input::InputMapper::navigation(*event, surface.viewTransform())) {
            const render::ViewTransform &view = surface.viewTransform();
            if (nav->kind == input::NavigationRequest::Kind::ZoomAbout) {
                surface.setViewTransform(view.zoomedAbout(view.scale() * nav->zoomFactor, nav->anchorDevice));
            } else {
                surface.setViewTransform(view.panned(nav->panDevice));
            }
            updateStatus();
            return true;
        }
        if (event->type() == QEvent::Leave) {
            pointerInside_ = false;
            surface.requestFrame();
        }
        return false;
    }

    void startScript() {
        emitEvidence(QStringLiteral("started"),
                     {{QStringLiteral("pid"), QCoreApplication::applicationPid()},
                      {QStringLiteral("platform"), QGuiApplication::platformName()},
                      {QStringLiteral("kind"), CanvasSurface::kindName(surface_->kind())},
                      {QStringLiteral("backend"), surface_->backendDescription()},
                      {QStringLiteral("validation_requested"), canvas::VulkanSupport::validationRequested()},
                      {QStringLiteral("validation_active"), canvas::VulkanSupport::validationActive()}});
        for (const QString &step : options_.script) {
            steps_.push_back(step);
        }
        next(0);
    }

private:
    void buildMenus() {
        QMenu *file = menuBar()->addMenu(QStringLiteral("&File"));
        QAction *quit = file->addAction(QStringLiteral("&Quit"), this, &QWidget::close);
        quit->setShortcut(QKeySequence::Quit);
        QMenu *view = menuBar()->addMenu(QStringLiteral("&View"));
        view->addAction(QStringLiteral("Zoom &In"), QKeySequence(QStringLiteral("Ctrl+=")), this,
                        [this] { zoomBy(2.0); });
        view->addAction(QStringLiteral("Zoom &Out"), QKeySequence(QStringLiteral("Ctrl+-")), this,
                        [this] { zoomBy(0.5); });
        view->addAction(QStringLiteral("&Fit on Screen"), QKeySequence(QStringLiteral("Ctrl+0")), this,
                        [this] { fitView(); });
        view->addAction(QStringLiteral("&Actual Pixels"), QKeySequence(QStringLiteral("Ctrl+1")), this,
                        [this] { actualPixels(); });
        QMenu *experiment = menuBar()->addMenu(QStringLiteral("E&xperiment"));
        experiment->addAction(QStringLiteral("Popup Menu over Canvas"), this, [this] { showProbeMenu(); });
        experiment->addAction(QStringLiteral("Tooltip over Canvas"), this, [this] { showProbeTooltip(); });
        experiment->addAction(QStringLiteral("Modal Dialog"), this, [this] { openProbeDialog(); });
        experiment->addAction(QStringLiteral("Float or Re-dock Layers"), this,
                              [this] { layersDock_->setFloating(!layersDock_->isFloating()); });
    }

    void buildToolBar() {
        toolBar_ = addToolBar(QStringLiteral("Tool Options"));
        toolBar_->setObjectName(QStringLiteral("toolOptions"));
        toolBar_->addWidget(new QLabel(QStringLiteral(" Size ")));
        auto *size = new QSpinBox;
        size->setRange(1, 5000);
        size->setValue(24);
        size->setSuffix(QStringLiteral(" px"));
        toolBar_->addWidget(size);
        auto *mode = new QComboBox;
        mode->addItems({QStringLiteral("Normal"), QStringLiteral("Multiply"), QStringLiteral("Screen")});
        toolBar_->addWidget(mode);
        auto *opacity = new QDoubleSpinBox;
        opacity->setRange(0.0, 100.0);
        opacity->setValue(100.0);
        opacity->setSuffix(QStringLiteral(" %"));
        toolBar_->addWidget(opacity);
        toolBar_->addWidget(new QCheckBox(QStringLiteral("Antialias")));
    }

    void buildDocks() {
        layersDock_ = new QDockWidget(QStringLiteral("Layers"), this);
        layersDock_->setObjectName(QStringLiteral("layersDock"));
        auto *layers = new QListWidget;
        for (int i = 20; i >= 1; --i) {
            layers->addItem(QStringLiteral("Layer %1").arg(i));
        }
        layersDock_->setWidget(layers);
        addDockWidget(Qt::RightDockWidgetArea, layersDock_);

        navigatorDock_ = new QDockWidget(QStringLiteral("Navigator"), this);
        navigatorDock_->setObjectName(QStringLiteral("navigatorDock"));
        navigatorDock_->setWidget(new QLabel(QStringLiteral("Floating dock over the canvas area")));
        addDockWidget(Qt::RightDockWidgetArea, navigatorDock_);
        navigatorDock_->setFloating(true);
        navigatorDock_->resize(260, 120);
    }

    void buildStatusBar() {
        backendLabel_ = new QLabel;
        geometryLabel_ = new QLabel;
        zoomLabel_ = new QLabel;
        pointerLabel_ = new QLabel;
        statusBar()->addWidget(backendLabel_, 1);
        statusBar()->addPermanentWidget(geometryLabel_);
        statusBar()->addPermanentWidget(zoomLabel_);
        statusBar()->addPermanentWidget(pointerLabel_);
        updateStatus();
    }

    void updateStatus() {
        backendLabel_->setText(surface_->backendDescription());
        const QSize device = surface_->deviceSize();
        geometryLabel_->setText(QStringLiteral("%1 × %2 device px, ratio %3")
                                    .arg(device.width())
                                    .arg(device.height())
                                    .arg(surface_->devicePixelRatio()));
        zoomLabel_->setText(QStringLiteral("%1 %").arg(surface_->viewTransform().scale() * 100.0, 0, 'f', 1));
        pointerLabel_->setText(pointerInside_ ? QStringLiteral("%1, %2")
                                                    .arg(pointerDocument_.x(), 0, 'f', 1)
                                                    .arg(pointerDocument_.y(), 0, 'f', 1)
                                              : QString());
    }

    void onFramePresented() {
        if (!firstFramePresented_) {
            firstFramePresented_ = true;
            QTimer::singleShot(300, this, [this] { startScript(); });
        }
        if (frameWaiter_) {
            auto waiter = std::move(frameWaiter_);
            frameWaiter_ = nullptr;
            waiter();
        }
        if (++statusCounter_ % 15 == 0) {
            updateStatus();
        }
    }

    void paintOverlay(QPainter &painter) {
        if (!pointerInside_) {
            return;
        }
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setPen(QPen(QColor(0xff, 0x20, 0x20), 1.0));
        const QPoint p(static_cast<int>(std::floor(pointerDevice_.x())),
                       static_cast<int>(std::floor(pointerDevice_.y())));
        painter.drawLine(p.x() - 12, p.y(), p.x() + 12, p.y());
        painter.drawLine(p.x(), p.y() - 12, p.x(), p.y() + 12);
    }

    void fitView() {
        const QSize device = surface_->deviceSize();
        if (device.isEmpty()) {
            return;
        }
        viewInitialized_ = true;
        const QSize doc = source_.documentSize();
        surface_->setViewTransform(render::ViewTransform::fit(
            render::DRect{0.0, 0.0, static_cast<double>(doc.width()), static_cast<double>(doc.height())},
            render::DSize{static_cast<double>(device.width()), static_cast<double>(device.height())},
            surface_->devicePixelRatio(), 24.0 * surface_->devicePixelRatio()));
        updateStatus();
    }

    void actualPixels() {
        const QSize device = surface_->deviceSize();
        viewInitialized_ = true;
        surface_->setViewTransform(render::ViewTransform::actualPixels(
            render::DPoint{0.0, 0.0},
            render::DSize{static_cast<double>(device.width()), static_cast<double>(device.height())},
            surface_->devicePixelRatio()));
        updateStatus();
    }

    void zoomBy(double factor) {
        const QSize device = surface_->deviceSize();
        const render::ViewTransform &view = surface_->viewTransform();
        surface_->setViewTransform(view.zoomedAbout(
            view.scale() * factor, render::DPoint{device.width() * 0.5, device.height() * 0.5}));
        updateStatus();
    }

    QMenu *showProbeMenu() {
        auto *menu = new QMenu(this);
        menu->setObjectName(QStringLiteral("probeMenu"));
        menu->setStyleSheet(QStringLiteral("QMenu { background: #ff00ff; color: #000000; border: 0px; }"
                                           "QMenu::item { padding: 6px 28px; background: #ff00ff; }"
                                           "QMenu::item:selected { background: #ff00ff; color: #000000; }"));
        for (int i = 1; i <= 6; ++i) {
            menu->addAction(QStringLiteral("Probe item %1").arg(i));
        }
        menu->setAttribute(Qt::WA_DeleteOnClose);
        QWidget *canvasWidget = surface_->widget();
        menu->popup(canvasWidget->mapToGlobal(
            QPoint(canvasWidget->width() / 2 - 80, canvasWidget->height() / 2 - 80)));
        return menu;
    }

    void showProbeTooltip() {
        QWidget *canvasWidget = surface_->widget();
        QToolTip::showText(canvasWidget->mapToGlobal(
                               QPoint(canvasWidget->width() / 2 + 120, canvasWidget->height() / 2 + 60)),
                           QStringLiteral("ArchMage tooltip probe\nover the canvas surface"), canvasWidget,
                           QRect(), 20000);
    }

    QDialog *openProbeDialog() {
        auto *dialog = new QDialog(this);
        dialog->setObjectName(QStringLiteral("probeDialog"));
        dialog->setWindowTitle(QStringLiteral("ArchMage focus probe"));
        auto *layout = new QVBoxLayout(dialog);
        auto *edit = new QLineEdit;
        edit->setObjectName(QStringLiteral("probeEdit"));
        layout->addWidget(edit);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        connect(buttons, &QDialogButtonBox::accepted, dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        layout->addWidget(buttons);
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setModal(true);
        dialog->open();
        return dialog;
    }

    QJsonObject focusState() const {
        return {{QStringLiteral("focus_window"), describeObject(QGuiApplication::focusWindow())},
                {QStringLiteral("focus_widget"), describeObject(QApplication::focusWidget())},
                {QStringLiteral("active_window"), describeObject(QApplication::activeWindow())}};
    }

    QRect relativeToWindow(const QRect &global) const {
        return global.translated(-mapToGlobal(QPoint(0, 0)));
    }

    QJsonObject geometryJson(const QString &tag, const QString &framePath) const {
        QWidget *canvasWidget = surface_->widget();
        const QPoint canvasOrigin = canvasWidget->mapTo(this, QPoint(0, 0));
        const render::ViewTransform &view = surface_->viewTransform();
        return {{QStringLiteral("tag"), tag},
                {QStringLiteral("platform"), QGuiApplication::platformName()},
                {QStringLiteral("kind"), CanvasSurface::kindName(surface_->kind())},
                {QStringLiteral("backend"), surface_->backendDescription()},
                {QStringLiteral("dpr"), surface_->devicePixelRatio()},
                {QStringLiteral("window_dpr"), devicePixelRatioF()},
                {QStringLiteral("device"),
                 QJsonArray{surface_->deviceSize().width(), surface_->deviceSize().height()}},
                {QStringLiteral("canvas_logical"), QJsonArray{canvasOrigin.x(), canvasOrigin.y(),
                                                              canvasWidget->width(), canvasWidget->height()}},
                {QStringLiteral("window_logical"), QJsonArray{width(), height()}},
                {QStringLiteral("screen"), screen() != nullptr ? screen()->name() : QString()},
                {QStringLiteral("frame"), framePath},
                {QStringLiteral("frames_presented"), static_cast<qint64>(surface_->framesPresented())},
                {QStringLiteral("view"), QJsonArray{view.scale(), view.pan().x, view.pan().y}}};
    }

    // Runs `then` after the next presented frame, or after a timeout when none arrives.
    void awaitFrame(std::function<void()> then) {
        auto shared = std::make_shared<std::function<void()>>(std::move(then));
        auto fired = std::make_shared<bool>(false);
        auto once = [shared, fired] {
            if (!*fired) {
                *fired = true;
                (*shared)();
            }
        };
        frameWaiter_ = once;
        QTimer::singleShot(2000, this, [this, once] {
            frameWaiter_ = nullptr;
            once();
        });
        surface_->requestFrame();
    }

    void snapThen(const QString &tag, std::function<void()> then) {
        emitLine(QStringLiteral("SNAP %1").arg(tag));
        QTimer::singleShot(options_.snapWaitMs, this, std::move(then));
    }

    void report(const QString &tag, std::function<void()> then) {
        awaitFrame([this, tag, then = std::move(then)]() mutable {
            const QString path =
                QDir(options_.evidenceDirectory).filePath(tag + QStringLiteral("-frame.png"));
            surface_->lastFrame().save(path);
            emitEvidence(QStringLiteral("geometry"), geometryJson(tag, path));
            snapThen(tag, std::move(then));
        });
    }

    void next(int delayMs) {
        QTimer::singleShot(delayMs, this, [this] { runStep(); });
    }

    void runStep() {
        if (steps_.empty()) {
            return;
        }
        const QString step = steps_.front();
        steps_.pop_front();
        const QString name = step.section(QLatin1Char(':'), 0, 0);
        const QString arg = step.section(QLatin1Char(':'), 1);
        emitLine(QStringLiteral("STEP %1").arg(step));

        if (name == QLatin1String("wait")) {
            next(arg.toInt());
        } else if (name == QLatin1String("fit")) {
            fitView();
            next(300);
        } else if (name == QLatin1String("actual")) {
            actualPixels();
            next(300);
        } else if (name == QLatin1String("report")) {
            report(arg, [this] { next(0); });
        } else if (name == QLatin1String("popup")) {
            // Probes wait so an external driver can activate the window first: Wayland grabbing popups need
            // a parent that has received input.
            QTimer::singleShot(kProbeDelayMs, this, [this, arg] { runPopup(arg); });
        } else if (name == QLatin1String("tooltip")) {
            QTimer::singleShot(kProbeDelayMs, this, [this, arg] { runTooltip(arg); });
        } else if (name == QLatin1String("dock-float") || name == QLatin1String("dock-redock")) {
            layersDock_->setFloating(name == QLatin1String("dock-float"));
            QTimer::singleShot(900, this, [this, arg] { report(arg, [this] { next(0); }); });
        } else if (name == QLatin1String("resize-storm")) {
            runResizeStorm(arg);
        } else if (name == QLatin1String("dialog")) {
            QTimer::singleShot(kProbeDelayMs, this, [this, arg] { runDialogFocus(arg, 3); });
        } else if (name == QLatin1String("drag")) {
            runDrag(arg.toInt() > 0 ? arg.toInt() : 3000);
        } else if (name == QLatin1String("move")) {
            runMove(arg);
        } else if (name == QLatin1String("quit")) {
            close();
        } else {
            emitEvidence(QStringLiteral("error"), {{QStringLiteral("unknown_step"), step}});
            next(0);
        }
    }

    static constexpr int kProbeDelayMs = 900;

    void runPopup(const QString &tag) {
        QPointer<QMenu> menu = showProbeMenu();
        QTimer::singleShot(700, this, [this, menu, tag] {
            QJsonObject probe{{QStringLiteral("tag"), tag},
                              {QStringLiteral("visible"), menu && menu->isVisible()}};
            if (menu) {
                probe[QStringLiteral("rect_logical")] = rectJson(relativeToWindow(menu->geometry()));
            }
            emitEvidence(QStringLiteral("popup"), probe);
            snapThen(tag, [this, menu] {
                if (menu) {
                    menu->close();
                }
                next(400);
            });
        });
    }

    void runTooltip(const QString &tag) {
        showProbeTooltip();
        QTimer::singleShot(900, this, [this, tag] {
            QJsonObject probe{{QStringLiteral("tag"), tag}, {QStringLiteral("visible"), false}};
            for (QWidget *w : QApplication::topLevelWidgets()) {
                if (w->inherits("QTipLabel") && w->isVisible()) {
                    probe[QStringLiteral("visible")] = true;
                    probe[QStringLiteral("rect_logical")] = rectJson(relativeToWindow(w->geometry()));
                }
            }
            emitEvidence(QStringLiteral("tooltip"), probe);
            snapThen(tag, [this] {
                QToolTip::hideText();
                next(400);
            });
        });
    }

    void runResizeStorm(const QString &tag) {
        const std::uint64_t framesBefore = surface_->framesPresented();
        auto *timer = new QTimer(this);
        timer->setInterval(16);
        auto iteration = std::make_shared<int>(0);
        connect(timer, &QTimer::timeout, this, [this, timer, iteration, tag, framesBefore] {
            static constexpr int kWidths[] = {180, 340, 260, 420, 220};
            resizeDocks({layersDock_}, {kWidths[*iteration % 5]}, Qt::Horizontal);
            if (*iteration % 7 == 0) {
                toolBar_->setVisible(!toolBar_->isVisible());
            }
            if (++*iteration == 60) {
                timer->stop();
                timer->deleteLater();
                toolBar_->setVisible(true);
                QTimer::singleShot(1000, this, [this, tag, framesBefore] {
                    emitEvidence(QStringLiteral("resize-storm"),
                                 {{QStringLiteral("tag"), tag},
                                  {QStringLiteral("iterations"), 60},
                                  {QStringLiteral("frames_during"),
                                   static_cast<qint64>(surface_->framesPresented() - framesBefore)}});
                    report(tag, [this] { next(0); });
                });
            }
        });
        timer->start();
    }

    // Opens and accepts a modal dialog `trials` times; each trial checks that focus returns to the canvas.
    void runDialogFocus(const QString &tag, int trials) {
        surface_->widget()->setFocus(Qt::OtherFocusReason);
        QTimer::singleShot(500, this, [this, tag, trials] {
            const QJsonObject before = focusState();
            QPointer<QDialog> dialog = openProbeDialog();
            QTimer::singleShot(1000, this, [this, tag, before, dialog, trials] {
                const QJsonObject during = focusState();
                if (dialog) {
                    dialog->accept();
                }
                QTimer::singleShot(1500, this, [this, tag, before, during, trials] {
                    const QJsonObject after = focusState();
                    const bool restored = before == after;
                    emitEvidence(QStringLiteral("focus"), {{QStringLiteral("tag"), tag},
                                                           {QStringLiteral("trials_left"), trials - 1},
                                                           {QStringLiteral("before"), before},
                                                           {QStringLiteral("during"), during},
                                                           {QStringLiteral("after"), after},
                                                           {QStringLiteral("restored"), restored}});
                    if (trials > 1) {
                        runDialogFocus(tag, trials - 1);
                    } else {
                        next(200);
                    }
                });
            });
        });
    }

    void runDrag(int durationMs) {
        QObject *target = surface_->widget();
        if (auto *vulkan = dynamic_cast<canvas::VulkanCanvasSurface *>(surface_.get())) {
            target = vulkan->vulkanWindow();
        }
        surface_->latency().reset();
        const QPointF center(surface_->widget()->width() * 0.5, surface_->widget()->height() * 0.5);
        const double radius = std::min(surface_->widget()->width(), surface_->widget()->height()) / 3.0;
        auto send = [target](QEvent::Type type, QPointF position, Qt::MouseButton button,
                             Qt::MouseButtons buttons) {
            QMouseEvent event(type, position, position, button, buttons, Qt::NoModifier);
            QCoreApplication::sendEvent(target, &event);
        };
        send(QEvent::MouseButtonPress, center + QPointF(radius, 0.0), Qt::LeftButton, Qt::LeftButton);
        auto *timer = new QTimer(this);
        timer->setTimerType(Qt::PreciseTimer);
        timer->setInterval(4); // 250 Hz, close to the 240 Hz synthetic input of plan S1-3.7
        auto started = std::make_shared<std::int64_t>(canvas::LatencyTrace::nowNs());
        auto events = std::make_shared<int>(0);
        connect(timer, &QTimer::timeout, this, [=, this] {
            const double elapsedMs = static_cast<double>(canvas::LatencyTrace::nowNs() - *started) / 1.0e6;
            const double angle = elapsedMs / 1000.0 * 2.0 * std::numbers::pi;
            const QPointF position = center + QPointF(std::cos(angle) * radius, std::sin(angle) * radius);
            send(QEvent::MouseMove, position, Qt::NoButton, Qt::LeftButton);
            ++*events;
            if (elapsedMs >= durationMs) {
                timer->stop();
                timer->deleteLater();
                send(QEvent::MouseButtonRelease, position, Qt::LeftButton, Qt::NoButton);
                QTimer::singleShot(300, this, [this, events, durationMs] {
                    const canvas::LatencyTrace::Stats stats = surface_->latency().total();
                    emitEvidence(QStringLiteral("latency"),
                                 {{QStringLiteral("events"), *events},
                                  {QStringLiteral("duration_ms"), durationMs},
                                  {QStringLiteral("frames"), stats.frames},
                                  {QStringLiteral("p50_ms"), stats.p50Ms},
                                  {QStringLiteral("p95_ms"), stats.p95Ms},
                                  {QStringLiteral("max_ms"), stats.maxMs},
                                  {QStringLiteral("render_p50_ms"), stats.renderP50Ms},
                                  {QStringLiteral("render_p95_ms"), stats.renderP95Ms}});
                    pointerInside_ = false;
                    surface_->requestFrame();
                    next(300);
                });
            }
        });
        timer->start();
    }

    void runMove(const QString &target) {
        const double ratioBefore = surface_->devicePixelRatio();
        const QString screenBefore = screen() != nullptr ? screen()->name() : QString();
        emitLine(QStringLiteral("MOVE %1").arg(target));
        auto *poll = new QTimer(this);
        poll->setInterval(200);
        auto started = std::make_shared<std::int64_t>(canvas::LatencyTrace::nowNs());
        connect(poll, &QTimer::timeout, this, [=, this] {
            surface_->requestFrame();
            const QString screenNow = screen() != nullptr ? screen()->name() : QString();
            const bool changed = surface_->devicePixelRatio() != ratioBefore || screenNow != screenBefore;
            const bool timedOut = canvas::LatencyTrace::nowNs() - *started > 6'000'000'000LL;
            if (!changed && !timedOut) {
                return;
            }
            poll->stop();
            poll->deleteLater();
            QTimer::singleShot(1200, this, [=, this] {
                if (!viewInitialized_ || changed) {
                    fitView();
                }
                emitEvidence(
                    QStringLiteral("move"),
                    {{QStringLiteral("target"), target},
                     {QStringLiteral("dpr_before"), ratioBefore},
                     {QStringLiteral("dpr_after"), surface_->devicePixelRatio()},
                     {QStringLiteral("screen_before"), screenBefore},
                     {QStringLiteral("screen_after"), screen() != nullptr ? screen()->name() : QString()},
                     {QStringLiteral("timed_out"), timedOut && !changed}});
                next(300);
            });
        });
        poll->start();
    }

    std::unique_ptr<CanvasSurface> surface_;
    CanvasExperimentOptions options_;
    canvas::SyntheticFrameSource source_;
    QToolBar *toolBar_ = nullptr;
    QDockWidget *layersDock_ = nullptr;
    QDockWidget *navigatorDock_ = nullptr;
    QLabel *backendLabel_ = nullptr;
    QLabel *geometryLabel_ = nullptr;
    QLabel *zoomLabel_ = nullptr;
    QLabel *pointerLabel_ = nullptr;
    std::deque<QString> steps_;
    std::function<void()> frameWaiter_;
    QPointF pointerDevice_;
    QPointF pointerDocument_;
    bool pointerInside_ = false;
    bool viewInitialized_ = false;
    bool firstFramePresented_ = false;
    int statusCounter_ = 0;
};

} // namespace

int runCanvasExperiment(QApplication &app, const CanvasExperimentOptions &options) {
    qInstallMessageHandler(messageHandler);
    int exitCode = 0;
    {
        QString error;
        std::unique_ptr<CanvasSurface> surface = CanvasSurface::create(options.kind, &error);
        if (!surface) {
            emitEvidence(QStringLiteral("error"),
                         {{QStringLiteral("kind"), CanvasSurface::kindName(options.kind)},
                          {QStringLiteral("message"), error}});
            canvas::VulkanSupport::shutdown();
            return 3;
        }
        auto *window = new ExperimentWindow(std::move(surface), options);
        window->setAttribute(Qt::WA_DeleteOnClose, false);
        QObject::connect(&app, &QApplication::lastWindowClosed, &app, &QApplication::quit);
        window->show();
        exitCode = QApplication::exec();
        delete window;
    }
    canvas::VulkanSupport::shutdown();
    emitEvidence(QStringLiteral("exit"),
                 {{QStringLiteral("exit_code"), exitCode},
                  {QStringLiteral("validation_active"), canvas::VulkanSupport::validationActive()},
                  {QStringLiteral("vk_validation_messages"), canvas::VulkanSupport::validationMessageCount()},
                  {QStringLiteral("qt_validation_messages"), gQtValidationMessages.load()}});
    return exitCode;
}

} // namespace archmage::ui::experiments
