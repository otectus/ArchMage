#include "archmage/ui/MainWindow.h"

#include "archmage/commands/Command.h"
#include "archmage/render/CpuEvaluator.h"
#include "archmage/ui/DocumentView.h"
#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/tools/BrushTool.h"

#include <QAction>
#include <QMenuBar>
#include <QStatusBar>

namespace archmage::ui {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), evaluator_(std::make_shared<render::CpuEvaluator>()) {
    setObjectName(QStringLiteral("archmageMainWindow"));
    setWindowTitle(QStringLiteral("ArchMage"));

    auto created = commands::createDocument(commands::CommandEnvelope::make(
        "archmage.document.create", {{"width", 2048}, {"height", 2048}, {"title", "Untitled"}}));
    session_ = new DocumentSession(std::move(created).value().state, QStringLiteral("New Document"),
                                   evaluator_, this);
    view_ = new DocumentView(*session_, this);
    view_->setTool(std::make_unique<BrushTool>());
    setCentralWidget(view_);

    QMenu *edit = menuBar()->addMenu(QStringLiteral("&Edit"));
    undo_ = edit->addAction(QStringLiteral("&Undo"), this, [this] { (void)session_->executor().undo(); });
    undo_->setShortcut(QKeySequence::Undo);
    redo_ = edit->addAction(QStringLiteral("&Redo"), this, [this] { (void)session_->executor().redo(); });
    redo_->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")));
    connect(session_, &DocumentSession::revisionChanged, this, [this] { updateActions(); });
    connect(view_, &DocumentView::errorReported, this,
            [this](const QString &message) { statusBar()->showMessage(message, 6000); });
    updateActions();
    resize(1280, 800);
}

MainWindow::~MainWindow() = default;

void MainWindow::updateActions() {
    const auto history = session_->executor().history();
    std::size_t current = 0;
    for (std::size_t i = 0; i < history.size(); ++i) {
        current = history[i].current ? i : current;
    }
    undo_->setEnabled(current > 0);
    undo_->setText(current > 0
                       ? QStringLiteral("&Undo %1").arg(QString::fromStdString(history[current].label))
                       : QStringLiteral("&Undo"));
    redo_->setEnabled(current + 1 < history.size());
    redo_->setText(current + 1 < history.size()
                       ? QStringLiteral("&Redo %1").arg(QString::fromStdString(history[current + 1].label))
                       : QStringLiteral("&Redo"));
}

} // namespace archmage::ui
