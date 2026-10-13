#include "archmage/ui/MainWindow.h"

#include "archmage/commands/Command.h"
#include "archmage/document/Version.h"
#include "archmage/native/Recovery.h"
#include "archmage/render/CpuEvaluator.h"
#include "archmage/ui/ColorState.h"
#include "archmage/ui/DocumentView.h"
#include "archmage/ui/OptionsRow.h"
#include "archmage/ui/Theme.h"
#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/panels/ColorPanel.h"
#include "archmage/ui/panels/LayersPanel.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QLabel>
#include <QListView>
#include <QMenuBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QVBoxLayout>

#include <cmath>

namespace archmage::ui {
namespace {

QString plainText(QString text) {
    text.remove(QLatin1Char('&'));
    text.remove(QStringLiteral("…"));
    return text.trimmed();
}

QString message(const base::Error &error) {
    return error.detail.empty() ? QString::fromStdString(error.message)
                                : QStringLiteral("%1\n\n%2").arg(QString::fromStdString(error.message),
                                                                 QString::fromStdString(error.detail));
}

} // namespace

MainWindow::MainWindow(QWidget *parent) : MainWindow(Options{}, parent) {}

MainWindow::MainWindow(Options options, QWidget *parent)
    : QMainWindow(parent), prompts_(options.prompts ? options.prompts : makeDialogPrompts()),
      evaluator_(std::make_shared<render::CpuEvaluator>()), colors_(new ColorState(this)) {
    setObjectName(QStringLiteral("archmageMainWindow"));
    setWindowTitle(QStringLiteral("ArchMage"));
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);
    pool_.setMaxThreadCount(2);
    startRecovery(options);

    createActions();
    createMenus();
    createToolStrip();
    options_ = new OptionsRow(toolSettings_, this);
    addToolBar(Qt::TopToolBarArea, options_);
    connect(options_, &OptionsRow::fitRequested, this, [this] {
        if (auto *view = currentView()) {
            view->fitToView();
        }
    });
    connect(options_, &OptionsRow::actualPixelsRequested, this, [this] {
        if (auto *view = currentView()) {
            view->actualPixels();
        }
    });
    createDocumentArea();
    createDocks();
    createStatusBar();
    setActiveTool(QString::fromLatin1(tool_ids::kBrush));
    updateDocumentState();
    resize(1440, 900);
}

MainWindow::~MainWindow() {
    pool_.waitForDone();
    // Without a closeEvent (the window destroyed directly), documents keep their journals for recovery.
    layersPanel_->setSession(nullptr);
    while (tabs_->count() > 0) {
        auto *view = qobject_cast<DocumentView *>(tabs_->widget(0));
        DocumentSession *session = &view->session();
        tabs_->removeTab(0);
        delete view;
        if (recovery_ && session->journal()) {
            (void)recovery_->untrack(session->journal(), false);
        }
        delete session;
    }
    recovery_.reset();
}

void MainWindow::startRecovery(const Options &options) {
    if (!options.recovery) {
        recoveryProblem_ = QStringLiteral("Recovery is off");
        return;
    }
    std::filesystem::path root;
    if (options.recoveryRoot) {
        root = *options.recoveryRoot;
    } else {
        auto located = native::locateRecoveryRoot();
        if (!located) {
            recoveryProblem_ = QString::fromStdString(located.error().message);
            return;
        }
        root = located->path;
    }
    auto session = native::RecoverySession::start(root, native::RecoverySession::Options{});
    if (!session) {
        recoveryProblem_ = QString::fromStdString(session.error().message);
        return;
    }
    recovery_ = std::move(*session);
}

QAction *MainWindow::makeAction(const QString &id, const QString &text, const QKeySequence &shortcut,
                                const QString &icon, std::function<void()> slot) {
    auto *action = new QAction(text, this);
    action->setObjectName(id);
    if (!shortcut.isEmpty()) {
        action->setShortcut(shortcut);
    }
    if (!icon.isEmpty()) {
        action->setIcon(theme::icon(icon));
    }
    const QString plain = plainText(text);
    action->setToolTip(shortcut.isEmpty() ? plain
                                          : QStringLiteral("%1 (%2)").arg(plain, shortcut.toString(QKeySequence::NativeText)));
    if (slot) {
        connect(action, &QAction::triggered, this, [slot = std::move(slot)] { slot(); });
    }
    actions_[id] = action;
    return action;
}

QAction *MainWindow::action(const QString &id) const {
    const auto it = actions_.find(id);
    return it != actions_.end() ? it->second : nullptr;
}

QMenu *MainWindow::menu(const QString &title) const {
    const auto it = menus_.find(title);
    return it != menus_.end() ? it->second : nullptr;
}

void MainWindow::createActions() {
    makeAction(QStringLiteral("file.new"), QStringLiteral("&New…"), QKeySequence::New, {}, [this] {
        if (const auto request = prompts_->newDocument(this)) {
            newDocument(*request);
        }
    });
    makeAction(QStringLiteral("file.open"), QStringLiteral("&Open…"), QKeySequence::Open, {}, [this] {
        if (const auto path = prompts_->openPath(this)) {
            openFile(*path);
        }
    });
    makeAction(QStringLiteral("file.save"), QStringLiteral("&Save"), QKeySequence::Save, {}, [this] {
        if (currentIndex() >= 0) {
            saveDocument(currentIndex(), false);
        }
    });
    makeAction(QStringLiteral("file.saveAs"), QStringLiteral("Save &As…"), QKeySequence(QStringLiteral("Ctrl+Shift+S")), {},
               [this] {
                   if (currentIndex() >= 0) {
                       saveDocument(currentIndex(), true);
                   }
               });
    makeAction(QStringLiteral("file.close"), QStringLiteral("&Close"), QKeySequence(QStringLiteral("Ctrl+W")), {}, [this] {
        if (currentIndex() >= 0) {
            closeDocument(currentIndex());
        }
    });
    makeAction(QStringLiteral("file.quit"), QStringLiteral("&Quit"), QKeySequence(QStringLiteral("Ctrl+Q")), {},
               [this] { close(); });

    makeAction(QStringLiteral("edit.undo"), QStringLiteral("&Undo"), QKeySequence(QStringLiteral("Ctrl+Z")), {}, [this] {
        if (auto *view = currentView()) {
            view->suspendInteraction();
            (void)view->session().executor().undo();
        }
    });
    makeAction(QStringLiteral("edit.redo"), QStringLiteral("&Redo"), QKeySequence(QStringLiteral("Ctrl+Shift+Z")), {}, [this] {
        if (auto *view = currentView()) {
            view->suspendInteraction();
            (void)view->session().executor().redo();
        }
    });

    makeAction(QStringLiteral("view.zoomIn"), QStringLiteral("Zoom &In"), QKeySequence(QStringLiteral("Ctrl+=")), {}, [this] {
        if (auto *view = currentView()) {
            view->zoomBy(2.0);
        }
    });
    action(QStringLiteral("view.zoomIn"))->setShortcuts({QKeySequence(QStringLiteral("Ctrl+=")), QKeySequence(QStringLiteral("Ctrl++"))});
    makeAction(QStringLiteral("view.zoomOut"), QStringLiteral("Zoom &Out"), QKeySequence(QStringLiteral("Ctrl+-")), {}, [this] {
        if (auto *view = currentView()) {
            view->zoomBy(0.5);
        }
    });
    makeAction(QStringLiteral("view.fit"), QStringLiteral("&Fit on Screen"), QKeySequence(QStringLiteral("Ctrl+0")), {}, [this] {
        if (auto *view = currentView()) {
            view->fitToView();
        }
    });
    makeAction(QStringLiteral("view.actualPixels"), QStringLiteral("&Actual Pixels"), QKeySequence(QStringLiteral("Ctrl+1")), {},
               [this] {
                   if (auto *view = currentView()) {
                       view->actualPixels();
                   }
               });
    // Tab is not bound as a window shortcut: it moves focus between controls, and toggles the panels only
    // while the canvas has focus (ADR-0105). The menu shows it after the tab character.
    auto *panels = makeAction(QStringLiteral("view.panels"), QStringLiteral("Show &Panels\tTab"), {}, {}, [this] { togglePanels(); });
    panels->setCheckable(true);
    panels->setChecked(true);

    const auto tool = [this](const char *id, const QString &text, const QString &key) {
        auto *created = makeAction(QStringLiteral("tool.%1").arg(QLatin1String(id)), text, QKeySequence(key), QLatin1String(id),
                                   [this, id] { setActiveTool(QString::fromLatin1(id)); });
        created->setCheckable(true);
    };
    tool(tool_ids::kBrush, QStringLiteral("&Brush"), QStringLiteral("B"));
    tool(tool_ids::kHand, QStringLiteral("&Hand"), QStringLiteral("H"));
    tool(tool_ids::kZoom, QStringLiteral("&Zoom"), QStringLiteral("Z"));
    tool(tool_ids::kEyedropper, QStringLiteral("&Eyedropper"), QStringLiteral("I"));

    makeAction(QStringLiteral("color.swap"), QStringLiteral("S&wap Colors"), QKeySequence(QStringLiteral("X")),
               QStringLiteral("swap-colors"), [this] { colors_->swap(); });
    makeAction(QStringLiteral("color.reset"), QStringLiteral("&Default Colors"), QKeySequence(QStringLiteral("D")),
               QStringLiteral("reset-colors"), [this] { colors_->reset(); });
    makeAction(QStringLiteral("brush.smaller"), QStringLiteral("Decrease Brush Size"), QKeySequence(QStringLiteral("[")), {},
               [this] { stepBrushSize(false); });
    makeAction(QStringLiteral("brush.larger"), QStringLiteral("Increase Brush Size"), QKeySequence(QStringLiteral("]")), {},
               [this] { stepBrushSize(true); });
    makeAction(QStringLiteral("focus.next"), QStringLiteral("Next Region"), QKeySequence(QStringLiteral("F6")), {},
               [this] { focusRegion(1); });
    makeAction(QStringLiteral("focus.previous"), QStringLiteral("Previous Region"), QKeySequence(QStringLiteral("Shift+F6")), {},
               [this] { focusRegion(-1); });
    makeAction(QStringLiteral("help.about"), QStringLiteral("&About ArchMage"), {}, {}, [this] {
        prompts_->information(this, QStringLiteral("About ArchMage"),
                              QStringLiteral("%1\n\nStage 1 internal alpha: raster layers, the brush, native files and "
                                             "recovery. Not feature-complete.")
                                  .arg(QString::fromStdString(document::versionBanner())));
    });
    // Window-wide shortcuts that live in no menu.
    for (const char *id : {"color.swap", "color.reset", "brush.smaller", "brush.larger", "focus.next", "focus.previous"}) {
        addAction(action(QString::fromLatin1(id)));
    }
}

void MainWindow::createMenus() {
    const auto add = [this](const QString &title) {
        QMenu *created = menuBar()->addMenu(title);
        created->setObjectName(QStringLiteral("menu.%1").arg(plainText(title).toLower()));
        menus_[plainText(title)] = created;
        return created;
    };
    const auto placeholder = [](QMenu *menu, const QString &text) {
        QAction *none = menu->addAction(text);
        none->setEnabled(false); // spec §29.2: say what is missing instead of offering dead commands
    };
    QMenu *file = add(QStringLiteral("&File"));
    file->addActions({action(QStringLiteral("file.new")), action(QStringLiteral("file.open"))});
    file->addSeparator();
    file->addActions({action(QStringLiteral("file.save")), action(QStringLiteral("file.saveAs"))});
    file->addSeparator();
    file->addActions({action(QStringLiteral("file.close")), action(QStringLiteral("file.quit"))});

    QMenu *edit = add(QStringLiteral("&Edit"));
    edit->addActions({action(QStringLiteral("edit.undo")), action(QStringLiteral("edit.redo"))});

    placeholder(add(QStringLiteral("&Image")), QStringLiteral("No image commands in this version"));
    add(QStringLiteral("&Layer")); // filled once the Layers panel exists (createDocks)
    placeholder(add(QStringLiteral("&Select")), QStringLiteral("No selection commands in this version"));
    placeholder(add(QStringLiteral("Fil&ter")), QStringLiteral("No filters in this version"));

    QMenu *view = add(QStringLiteral("&View"));
    view->addActions({action(QStringLiteral("view.zoomIn")), action(QStringLiteral("view.zoomOut")),
                      action(QStringLiteral("view.fit")), action(QStringLiteral("view.actualPixels"))});
    view->addSeparator();
    view->addAction(action(QStringLiteral("view.panels")));

    add(QStringLiteral("&Window")); // dock and toolbar toggles, filled in createDocks
    QMenu *help = add(QStringLiteral("&Help"));
    help->addAction(action(QStringLiteral("help.about")));
}

void MainWindow::createToolStrip() {
    toolStrip_ = new QToolBar(QStringLiteral("Tools"), this);
    toolStrip_->setObjectName(QStringLiteral("archmageToolStrip"));
    toolStrip_->setOrientation(Qt::Vertical);
    toolStrip_->setMovable(false);
    toolStrip_->setFloatable(false);
    toolStrip_->setIconSize(QSize(24, 24));
    toolStrip_->setFixedWidth(44);
    toolStrip_->setToolButtonStyle(Qt::ToolButtonIconOnly);
    addToolBar(Qt::LeftToolBarArea, toolStrip_);
    toolGroup_ = new QActionGroup(this);
    toolGroup_->setExclusive(true);
    for (const char *id : {tool_ids::kBrush, tool_ids::kHand, tool_ids::kZoom, tool_ids::kEyedropper}) {
        QAction *tool = action(QStringLiteral("tool.%1").arg(QLatin1String(id)));
        toolGroup_->addAction(tool);
        toolStrip_->addAction(tool);
        if (QWidget *button = toolStrip_->widgetForAction(tool)) {
            button->setAccessibleName(plainText(tool->text()));
            button->setObjectName(QStringLiteral("archmageToolButton.%1").arg(QLatin1String(id)));
        }
    }
}

void MainWindow::createDocumentArea() {
    central_ = new QStackedWidget(this);
    emptyPage_ = new QWidget(central_);
    auto *emptyLayout = new QVBoxLayout(emptyPage_);
    emptyLayout->addStretch(1);
    auto *hint = new QLabel(QStringLiteral("No open documents"), emptyPage_);
    hint->setAlignment(Qt::AlignCenter);
    emptyLayout->addWidget(hint);
    for (const QString &id : {QStringLiteral("file.new"), QStringLiteral("file.open")}) {
        auto *button = new QPushButton(plainText(action(id)->text()) + QStringLiteral("…"), emptyPage_);
        button->setToolTip(action(id)->toolTip());
        connect(button, &QPushButton::clicked, action(id), &QAction::trigger);
        emptyLayout->addWidget(button, 0, Qt::AlignHCenter);
    }
    emptyLayout->addStretch(1);
    tabs_ = new QTabWidget(central_);
    tabs_->setObjectName(QStringLiteral("archmageDocumentTabs"));
    tabs_->setDocumentMode(true);
    tabs_->setTabsClosable(true);
    tabs_->setMovable(true);
    central_->addWidget(emptyPage_);
    central_->addWidget(tabs_);
    setCentralWidget(central_);
    connect(tabs_, &QTabWidget::tabCloseRequested, this, [this](int index) { closeDocument(index); });
    connect(tabs_, &QTabWidget::currentChanged, this, &MainWindow::onCurrentDocumentChanged);
}

void MainWindow::createDocks() {
    colorPanel_ = new ColorPanel(*colors_, action(QStringLiteral("color.swap")), action(QStringLiteral("color.reset")), this);
    colorDock_ = new QDockWidget(QStringLiteral("Color"), this);
    colorDock_->setObjectName(QStringLiteral("archmageColorDock"));
    colorDock_->setWidget(colorPanel_);
    layersPanel_ = new LayersPanel(this);
    layersDock_ = new QDockWidget(QStringLiteral("Layers"), this);
    layersDock_->setObjectName(QStringLiteral("archmageLayersDock"));
    layersDock_->setWidget(layersPanel_);
    for (QDockWidget *dock : {colorDock_, layersDock_}) {
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
        addDockWidget(Qt::RightDockWidgetArea, dock);
    }
    splitDockWidget(colorDock_, layersDock_, Qt::Vertical); // Color above Layers (spec §2.2)

    // The panel owns the layer actions; the window binds their shortcuts through the Layer menu.
    layersPanel_->newLayerAction()->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+N")));
    layersPanel_->duplicateLayerAction()->setShortcut(QKeySequence(QStringLiteral("Ctrl+J")));
    for (QAction *layer : {layersPanel_->newLayerAction(), layersPanel_->duplicateLayerAction(), layersPanel_->deleteLayerAction()}) {
        const QString plain = plainText(layer->text());
        layer->setToolTip(layer->shortcut().isEmpty()
                              ? plain
                              : QStringLiteral("%1 (%2)").arg(plain, layer->shortcut().toString(QKeySequence::NativeText)));
        actions_[layer->objectName()] = layer;
    }
    auto *rename = makeAction(QStringLiteral("layer.rename"), QStringLiteral("&Rename Layer"), QKeySequence(QStringLiteral("F2")), {},
                              [this] { layersPanel_->renameActiveLayer(); });
    QMenu *layer = menu(QStringLiteral("Layer"));
    layer->addActions({layersPanel_->newLayerAction(), layersPanel_->duplicateLayerAction(), layersPanel_->deleteLayerAction()});
    layer->addSeparator();
    layer->addAction(rename);

    QMenu *window = menu(QStringLiteral("Window"));
    for (QAction *toggle : {colorDock_->toggleViewAction(), layersDock_->toggleViewAction(), toolStrip_->toggleViewAction(),
                            options_->toggleViewAction()}) {
        window->addAction(toggle);
    }
}

void MainWindow::createStatusBar() {
    jobLabel_ = new QLabel(this);
    jobLabel_->setObjectName(QStringLiteral("archmageJobLabel"));
    coordinatesLabel_ = new QLabel(this);
    coordinatesLabel_->setObjectName(QStringLiteral("archmageCoordinatesLabel"));
    coordinatesLabel_->setMinimumWidth(150);
    zoomLabel_ = new QLabel(this);
    zoomLabel_->setObjectName(QStringLiteral("archmageZoomLabel"));
    zoomLabel_->setMinimumWidth(90);
    durabilityLabel_ = new QLabel(this);
    durabilityLabel_->setObjectName(QStringLiteral("archmageDurabilityLabel"));
    durabilityLabel_->setMinimumWidth(110);
    statusBar()->addWidget(jobLabel_, 1);
    statusBar()->addPermanentWidget(coordinatesLabel_);
    statusBar()->addPermanentWidget(zoomLabel_);
    statusBar()->addPermanentWidget(durabilityLabel_);
    durabilityTimer_ = new QTimer(this);
    durabilityTimer_->setInterval(500);
    connect(durabilityTimer_, &QTimer::timeout, this, &MainWindow::updateDurability);
    durabilityTimer_->start();
    updateJobLabel();
}

void MainWindow::showEvent(QShowEvent *event) {
    QMainWindow::showEvent(event);
    if (!docksSized_) {
        docksSized_ = true;
        resizeDocks({colorDock_, layersDock_}, {300, 300}, Qt::Horizontal); // spec §2.2: about 300 px wide
        resizeDocks({colorDock_}, {130}, Qt::Vertical);
    }
}

int MainWindow::documentCount() const { return tabs_->count(); }
int MainWindow::currentIndex() const { return tabs_->currentIndex(); }

DocumentView *MainWindow::viewAt(int index) const { return qobject_cast<DocumentView *>(tabs_->widget(index)); }
DocumentView *MainWindow::currentView() const { return viewAt(tabs_->currentIndex()); }

DocumentSession *MainWindow::currentSession() const {
    DocumentView *view = currentView();
    return view != nullptr ? &view->session() : nullptr;
}

DocumentView *MainWindow::newDocument(const NewDocumentRequest &request) {
    ++untitled_;
    auto created = commands::createDocument(commands::CommandEnvelope::make(
        "archmage.document.create", {{"width", request.width},
                                     {"height", request.height},
                                     {"sampleType", request.sampleType.toStdString()},
                                     {"background", request.transparent ? "transparent" : "white"},
                                     {"title", QStringLiteral("Untitled-%1").arg(untitled_).toStdString()}}));
    if (!created) {
        prompts_->error(this, QStringLiteral("New Document"), message(created.error()));
        return nullptr;
    }
    return addDocument(std::make_unique<DocumentSession>(created->state, QStringLiteral("New Document"), evaluator_), {});
}

DocumentView *MainWindow::addDocument(std::unique_ptr<DocumentSession> owned, const std::filesystem::path &source) {
    DocumentSession *session = owned.release();
    session->setParent(this);
    if (recovery_) {
        if (auto journal = recovery_->track(session->executor(), source)) {
            session->setJournal(*journal);
        } else {
            statusBar()->showMessage(QStringLiteral("This document is not journaled: %1")
                                         .arg(QString::fromStdString(journal.error().message)),
                                     8000);
        }
    }
    auto *view = new DocumentView(*session, tabs_);
    view->setColors(colors_);
    connect(view, &DocumentView::errorReported, this, [this](const QString &text) { statusBar()->showMessage(text, 8000); });
    connect(view, &DocumentView::viewChanged, this, [this, view] {
        if (view == currentView()) {
            updateZoomLabel();
        }
    });
    connect(view, &DocumentView::pointerMoved, this, [this, view](double x, double y) {
        const auto state = view->displayedState();
        const auto px = static_cast<std::int64_t>(std::floor(x));
        const auto py = static_cast<std::int64_t>(std::floor(y));
        coordinatesLabel_->setText(state && state->canvasRect().contains(base::IPoint{px, py})
                                       ? QStringLiteral("X %1, Y %2 px").arg(px).arg(py)
                                       : QString());
    });
    connect(view, &DocumentView::pointerLeft, this, [this] { coordinatesLabel_->clear(); });
    connect(view, &DocumentView::panelsToggleRequested, this, &MainWindow::togglePanels);
    connect(session, &DocumentSession::revisionChanged, this, [this, view] {
        updateTabTitle(view);
        if (view == currentView()) {
            updateDocumentState();
        }
    });
    connect(session, &DocumentSession::fileStateChanged, this, [this, view] {
        updateTabTitle(view);
        if (view == currentView()) {
            updateDocumentState();
        }
    });
    const int index = tabs_->addTab(view, session->displayName());
    central_->setCurrentWidget(tabs_);
    tabs_->setCurrentIndex(index);
    onCurrentDocumentChanged(index); // also when the index did not change (the first document)
    updateTabTitle(view);
    view->setFocus(Qt::OtherFocusReason);
    return view;
}

void MainWindow::openFile(const QString &path) {
    const std::filesystem::path file = path.toStdString();
    std::error_code ec;
    const auto canonical = std::filesystem::weakly_canonical(file, ec);
    for (int i = 0; i < tabs_->count(); ++i) {
        const auto &open = viewAt(i)->session().file().path;
        if (!open.empty() && std::filesystem::weakly_canonical(open, ec) == canonical) {
            tabs_->setCurrentIndex(i); // already open
            emit documentOpened(i);
            return;
        }
    }
    ++jobs_;
    jobText_ = QStringLiteral("Opening %1…").arg(QString::fromStdString(file.filename().string()));
    updateJobLabel();
    pool_.start([this, file] {
        auto opened = std::make_shared<base::Result<native::OpenedDocument>>(native::readDocument(file));
        QMetaObject::invokeMethod(this, [this, file, opened] { finishOpen(file, *opened); }, Qt::QueuedConnection);
    });
}

void MainWindow::finishOpen(const std::filesystem::path &path, const base::Result<native::OpenedDocument> &opened) {
    --jobs_;
    updateJobLabel();
    if (!opened) {
        prompts_->error(this, QStringLiteral("Open"), message(opened.error()));
        return;
    }
    if (!opened->state) {
        // A newer format or an unknown capability: there is nothing this version can edit (spec §10.5).
        prompts_->information(this, QStringLiteral("Open"), QString::fromStdString(opened->info.readOnlyExplanation));
        return;
    }
    auto session = std::make_unique<DocumentSession>(*opened->state, QStringLiteral("Open"), evaluator_);
    DocumentSession::FileState file;
    file.path = std::filesystem::absolute(path);
    file.identity = native::FileIdentity{opened->info.documentId, opened->info.derivedFrom};
    if (opened->info.readOnly) {
        file.readOnlyExplanation = QString::fromStdString(opened->info.readOnlyExplanation);
    }
    session->setFile(std::move(file));
    DocumentView *view = addDocument(std::move(session), path);
    emit documentOpened(tabs_->indexOf(view));
}

std::optional<native::SaveRequest> MainWindow::prepareSave(DocumentView &view, bool saveAs) {
    DocumentSession &session = view.session();
    view.suspendInteraction();
    session.executor().drain(); // a stroke submitted a moment ago belongs to this save
    std::filesystem::path target = session.file().path;
    const bool asCopy = saveAs || session.isReadOnly();
    if (asCopy || target.empty()) {
        const QString suggested = target.empty() ? session.displayName() + QStringLiteral(".archmage")
                                                 : QString::fromStdString(target.string());
        const auto chosen = prompts_->savePath(this, suggested);
        if (!chosen) {
            return std::nullopt;
        }
        target = chosen->toStdString();
    }
    native::SaveRequest request;
    request.target = target;
    request.revision = session.executor().current();
    request.identity = asCopy ? native::identityForSaveAs(*request.revision->state, session.file().identity)
                              : native::identityForSave(*request.revision->state, session.file().identity);
    return request;
}

void MainWindow::saveDocument(int index, bool saveAs, std::function<void(bool)> done) {
    DocumentView *view = viewAt(index);
    if (view == nullptr || view->session().isSaving()) {
        if (done) {
            done(false);
        }
        return;
    }
    auto request = prepareSave(*view, saveAs);
    if (!request) {
        if (done) {
            done(false);
        }
        return;
    }
    DocumentSession *session = &view->session();
    session->setSaving(true);
    ++jobs_;
    jobText_ = QStringLiteral("Saving %1…").arg(QString::fromStdString(request->target.filename().string()));
    updateJobLabel();
    pool_.start([this, session, view, request = *request, done = std::move(done)] {
        auto outcome = std::make_shared<base::Result<native::SaveOutcome>>(native::saveDocument(request));
        QMetaObject::invokeMethod(
            this,
            [this, session, view, request, outcome, done] {
                --jobs_;
                session->setSaving(false);
                const bool ok = outcome->has_value();
                if (ok) {
                    session->markSaved(*request.revision, (*outcome)->path, request.identity);
                    statusBar()->showMessage(QStringLiteral("Saved %1").arg(session->displayName()), 4000);
                } else {
                    prompts_->error(this, QStringLiteral("Save"), message(outcome->error()));
                }
                updateJobLabel();
                emit documentSaved(tabs_->indexOf(view), ok);
                if (done) {
                    done(ok);
                }
            },
            Qt::QueuedConnection);
    });
}

bool MainWindow::saveNow(DocumentView &view, bool saveAs) {
    auto request = prepareSave(view, saveAs);
    if (!request) {
        return false;
    }
    QApplication::setOverrideCursor(Qt::WaitCursor);
    auto outcome = native::saveDocument(*request);
    QApplication::restoreOverrideCursor();
    if (!outcome) {
        prompts_->error(this, QStringLiteral("Save"), message(outcome.error()));
        return false;
    }
    view.session().markSaved(*request->revision, outcome->path, request->identity);
    return true;
}

bool MainWindow::closeDocument(int index) {
    DocumentView *view = viewAt(index);
    if (view == nullptr) {
        return false;
    }
    DocumentSession &session = view->session();
    if (session.isSaving()) {
        statusBar()->showMessage(QStringLiteral("%1 is being saved; close it when the save finishes.").arg(session.displayName()),
                                 6000);
        return false;
    }
    view->suspendInteraction(); // ADR-0105: closing commits an interaction in progress
    session.executor().drain();
    if (session.isModified()) {
        tabs_->setCurrentIndex(index);
        switch (prompts_->confirmClose(this, session.displayName())) {
        case ShellPrompts::CloseChoice::Cancel:
            return false;
        case ShellPrompts::CloseChoice::Save:
            if (!saveNow(*view, false)) {
                return false;
            }
            break;
        case ShellPrompts::CloseChoice::Discard:
            break;
        }
    }
    finishClose(view);
    return true;
}

void MainWindow::finishClose(DocumentView *view) {
    DocumentSession *session = &view->session();
    if (view == currentView() && tabs_->count() == 1) {
        layersPanel_->setSession(nullptr);
    }
    tabs_->removeTab(tabs_->indexOf(view));
    if (previousView_ == view) {
        previousView_ = nullptr;
    }
    delete view;
    if (recovery_ && session->journal()) {
        // A deliberate close: whatever was in the journal was saved or discarded by the user (ADR-0104).
        if (auto status = recovery_->untrack(session->journal(), true); !status) {
            statusBar()->showMessage(QString::fromStdString(status.error().message), 8000);
        }
    }
    delete session;
    if (tabs_->count() == 0) {
        central_->setCurrentWidget(emptyPage_);
        layersPanel_->setSession(nullptr);
    }
    updateDocumentState();
}

void MainWindow::onCurrentDocumentChanged(int index) {
    DocumentView *view = viewAt(index);
    if (previousView_ && previousView_ != view) {
        previousView_->suspendInteraction(); // ADR-0105: switching documents commits what was drawn
    }
    previousView_ = view;
    if (view != nullptr) {
        if (view->tool() == nullptr || view->tool()->id() != activeTool_) {
            view->setTool(createTool(activeTool_, toolSettings_));
        }
        QWidget::setTabOrder(options_->brushOpacity(), view->surface().widget());
        QWidget::setTabOrder(view->surface().widget(), &layersPanel_->opacitySlider());
    }
    layersPanel_->setSession(view != nullptr ? &view->session() : nullptr);
    updateDocumentState();
}

void MainWindow::setActiveTool(const QString &id) {
    if (!createTool(id, toolSettings_)) {
        return;
    }
    activeTool_ = id;
    if (QAction *tool = action(QStringLiteral("tool.%1").arg(id))) {
        tool->setChecked(true);
    }
    options_->showOptionsFor(id);
    if (DocumentView *view = currentView()) {
        view->setTool(createTool(id, toolSettings_)); // the previous tool commits as it deactivates
    }
}

void MainWindow::updateTabTitle(DocumentView *view) {
    const int index = tabs_->indexOf(view);
    if (index < 0) {
        return;
    }
    const DocumentSession &session = view->session();
    QString title = session.displayName();
    if (session.isModified()) {
        title += QStringLiteral(" *");
    }
    if (session.isReadOnly()) {
        title += QStringLiteral(" (read-only)");
    }
    tabs_->setTabText(index, title);
    tabs_->setTabToolTip(index, session.file().path.empty() ? QStringLiteral("Not saved yet")
                                                            : QString::fromStdString(session.file().path.string()));
}

void MainWindow::updateDocumentState() {
    DocumentView *view = currentView();
    DocumentSession *session = view != nullptr ? &view->session() : nullptr;
    const bool hasDocument = session != nullptr;
    if (hasDocument) {
        setWindowTitle(QStringLiteral("%1%2 — ArchMage").arg(session->displayName(), session->isModified() ? QStringLiteral(" *") : QString()));
    } else {
        setWindowTitle(QStringLiteral("ArchMage"));
    }
    for (const char *id : {"file.save", "file.saveAs", "file.close", "view.zoomIn", "view.zoomOut", "view.fit", "view.actualPixels",
                           "layer.rename"}) {
        action(QString::fromLatin1(id))->setEnabled(hasDocument);
    }
    QAction *undo = action(QStringLiteral("edit.undo"));
    QAction *redo = action(QStringLiteral("edit.redo"));
    undo->setText(QStringLiteral("&Undo"));
    redo->setText(QStringLiteral("&Redo"));
    undo->setEnabled(false);
    redo->setEnabled(false);
    if (hasDocument) {
        const auto history = session->executor().history();
        std::size_t current = 0;
        for (std::size_t i = 0; i < history.size(); ++i) {
            current = history[i].current ? i : current;
        }
        if (current > 0) {
            undo->setEnabled(true);
            undo->setText(QStringLiteral("&Undo %1").arg(QString::fromStdString(history[current].label)));
        }
        if (current + 1 < history.size()) {
            redo->setEnabled(true);
            redo->setText(QStringLiteral("&Redo %1").arg(QString::fromStdString(history[current + 1].label)));
        }
    }
    updateZoomLabel();
    updateDurability();
    if (!hasDocument) {
        coordinatesLabel_->clear();
    }
}

void MainWindow::updateZoomLabel() {
    DocumentView *view = currentView();
    if (view == nullptr) {
        zoomLabel_->clear();
        return;
    }
    const double percent = view->surface().viewTransform().scale() * 100.0;
    zoomLabel_->setText(QStringLiteral("Zoom %1 %").arg(percent, 0, 'f', percent < 10.0 ? 2 : 1));
}

void MainWindow::updateDurability() {
    DocumentSession *session = currentSession();
    if (session == nullptr) {
        durabilityLabel_->clear();
        return;
    }
    if (session->isSaving()) {
        durabilityLabel_->setText(QStringLiteral("Saving"));
        return;
    }
    const auto &journal = session->journal();
    if (!journal) {
        durabilityLabel_->setText(QStringLiteral("Not journaled"));
        durabilityLabel_->setToolTip(recoveryProblem_);
        return;
    }
    if (!session->isModified() && !session->file().path.empty()) {
        durabilityLabel_->setText(QStringLiteral("Saved"));
    } else {
        switch (journal->currentDurability()) {
        case native::Durability::InMemory:
            durabilityLabel_->setText(QStringLiteral("In memory"));
            break;
        case native::Durability::Journaled:
            durabilityLabel_->setText(QStringLiteral("Journaled"));
            break;
        case native::Durability::Saved:
            durabilityLabel_->setText(QStringLiteral("Saved"));
            break;
        }
    }
    const auto problem = journal->lastError();
    durabilityLabel_->setToolTip(problem ? QStringLiteral("Recovery journal: %1").arg(QString::fromStdString(journal->path().string()))
                                         : QStringLiteral("Journaling failed: %1").arg(QString::fromStdString(problem.error().message)));
}

void MainWindow::updateJobLabel() {
    jobLabel_->setText(jobs_ > 0 ? jobText_ : QStringLiteral("Ready"));
}

void MainWindow::togglePanels() {
    if (!panelsHidden_) {
        hiddenPanels_.clear();
        for (QWidget *panel : std::initializer_list<QWidget *>{colorDock_, layersDock_, toolStrip_, options_}) {
            if (panel->isVisible()) {
                hiddenPanels_.push_back(panel);
                panel->hide();
            }
        }
        panelsHidden_ = true;
    } else {
        for (QWidget *panel : hiddenPanels_) {
            panel->show();
        }
        hiddenPanels_.clear();
        panelsHidden_ = false;
    }
    action(QStringLiteral("view.panels"))->setChecked(!panelsHidden_);
}

void MainWindow::focusRegion(int direction) {
    // F6 / Shift+F6 (ADR-0105): options row, canvas, Layers list.
    std::vector<QWidget *> regions;
    if (options_->isVisible()) {
        regions.push_back(options_->brushSize()->isVisible() ? static_cast<QWidget *>(options_->brushSize()) : options_);
    }
    if (DocumentView *view = currentView()) {
        regions.push_back(view->surface().widget());
    }
    if (layersDock_->isVisible()) {
        regions.push_back(&layersPanel_->list());
    }
    if (regions.empty()) {
        return;
    }
    const QWidget *focus = QApplication::focusWidget();
    int at = -1;
    for (std::size_t i = 0; i < regions.size(); ++i) {
        if (focus != nullptr && (focus == regions[i] || regions[i]->isAncestorOf(focus))) {
            at = static_cast<int>(i);
        }
    }
    const int count = static_cast<int>(regions.size());
    const int next = at < 0 ? (direction > 0 ? 0 : count - 1) : (at + direction + count) % count;
    regions[static_cast<std::size_t>(next)]->setFocus(Qt::ShortcutFocusReason);
}

void MainWindow::stepBrushSize(bool larger) {
    double &size = toolSettings_.brush.size;
    const double stepped = larger ? std::max(size + 1.0, std::round(size * 1.15)) : std::min(size - 1.0, std::round(size / 1.15));
    size = std::clamp(stepped, 1.0, 5000.0);
    options_->refresh();
}

void MainWindow::waitForJobs() { pool_.waitForDone(); }

void MainWindow::closeEvent(QCloseEvent *event) {
    while (tabs_->count() > 0) {
        if (!closeDocument(tabs_->count() - 1)) {
            event->ignore();
            return;
        }
    }
    event->accept();
}

} // namespace archmage::ui
