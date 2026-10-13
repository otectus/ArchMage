#pragma once

#include "archmage/native/NativeFile.h"
#include "archmage/native/SaveTransaction.h"
#include "archmage/ui/ShellPrompts.h"
#include "archmage/ui/tools/Tool.h"

#include <QMainWindow>
#include <QPointer>
#include <QThreadPool>

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>

class QAction;
class QActionGroup;
class QDockWidget;
class QLabel;
class QMenu;
class QStackedWidget;
class QTabWidget;
class QTimer;
class QToolBar;

namespace archmage::render {
class CpuEvaluator;
}
namespace archmage::native {
class RecoverySession;
}

namespace archmage::ui {

class ColorPanel;
class ColorState;
class DocumentSession;
class DocumentView;
class LayersPanel;
class OptionsRow;

/// The desktop shell (plan S1-7; spec §2.2): menu bar, options row, tool strip, tabbed documents, the right
/// dock with Color above Layers, and a status bar with zoom, pointer coordinates, durability and jobs.
/// Menus show only working commands; a menu with none says so instead of offering dead items (spec §29.2).
///
/// Documents: File > New creates one; Open and Save run on a worker thread, with the job shown in the status
/// bar; closing a modified document asks to Save, Discard or Cancel, and Save on close completes before the
/// document goes. Every open document is journaled by the window's recovery session (D-14, ADR-0104): a
/// deliberate close discards its journal, a crash leaves it for recovery (S1-8).
///
/// Ownership: owns its documents, their views and the recovery session. Thread safety: GUI thread only.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    struct Options {
        std::shared_ptr<ShellPrompts> prompts;             // null: Qt dialogs
        bool recovery = true;                              // journal open documents (D-14)
        std::optional<std::filesystem::path> recoveryRoot; // null: native::locateRecoveryRoot()
    };

    explicit MainWindow(QWidget *parent = nullptr);
    explicit MainWindow(Options options, QWidget *parent = nullptr);
    ~MainWindow() override;

    /// Creates and shows a new document; null (after reporting) when the parameters are invalid.
    DocumentView *newDocument(const NewDocumentRequest &request);
    /// Opens a file on a worker thread; documentOpened or an error prompt reports the outcome. A file
    /// already open is brought to the front instead.
    void openFile(const QString &path);
    /// Asks to save a modified document, then closes it. False when cancelled, still saving or not saved.
    bool closeDocument(int index);
    /// Saves on a worker thread; asks for a path for Save As, a never-saved document or a read-only one.
    /// `done` receives the outcome (false also when the user cancels the path prompt).
    void saveDocument(int index, bool saveAs, std::function<void(bool)> done = {});

    [[nodiscard]] int documentCount() const;
    [[nodiscard]] int currentIndex() const;
    [[nodiscard]] DocumentView *viewAt(int index) const;
    [[nodiscard]] DocumentView *currentView() const;
    [[nodiscard]] DocumentSession *currentSession() const;

    /// Activates a tool (tool_ids) for the current document and the ones shown later.
    void setActiveTool(const QString &id);
    [[nodiscard]] QString activeTool() const { return activeTool_; }

    /// An action by object name, e.g. "file.save", "tool.brush"; null when unknown.
    [[nodiscard]] QAction *action(const QString &id) const;
    [[nodiscard]] QMenu *menu(const QString &title) const;
    [[nodiscard]] QTabWidget *documentTabs() const { return tabs_; }
    [[nodiscard]] QToolBar *toolStrip() const { return toolStrip_; }
    [[nodiscard]] OptionsRow *optionsRow() const { return options_; }
    [[nodiscard]] QDockWidget *colorDock() const { return colorDock_; }
    [[nodiscard]] QDockWidget *layersDock() const { return layersDock_; }
    [[nodiscard]] LayersPanel *layersPanel() const { return layersPanel_; }
    [[nodiscard]] ColorPanel *colorPanel() const { return colorPanel_; }
    [[nodiscard]] ColorState &colors() { return *colors_; }
    [[nodiscard]] ToolSettings &toolSettings() { return toolSettings_; }
    [[nodiscard]] native::RecoverySession *recovery() const { return recovery_.get(); }
    [[nodiscard]] QString recoveryProblem() const { return recoveryProblem_; }
    [[nodiscard]] QLabel *zoomLabel() const { return zoomLabel_; }
    [[nodiscard]] QLabel *coordinatesLabel() const { return coordinatesLabel_; }
    [[nodiscard]] QLabel *durabilityLabel() const { return durabilityLabel_; }
    [[nodiscard]] QLabel *jobLabel() const { return jobLabel_; }
    [[nodiscard]] bool panelsVisible() const { return !panelsHidden_; }

    /// Blocks until background open and save jobs have finished; their results are delivered later through
    /// the event loop.
    void waitForJobs();

signals:
    void documentOpened(int index);
    void documentSaved(int index, bool ok);

protected:
    void closeEvent(QCloseEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void startRecovery(const Options &options);
    QAction *makeAction(const QString &id, const QString &text, const QKeySequence &shortcut,
                        const QString &icon, std::function<void()> slot);
    void createActions();
    void createMenus();
    void createToolStrip();
    void createDocumentArea();
    void createDocks();
    void createStatusBar();

    DocumentView *addDocument(std::unique_ptr<DocumentSession> session, const std::filesystem::path &source);
    void finishOpen(const std::filesystem::path &path, const base::Result<native::OpenedDocument> &opened);
    /// Prepares a save of the document in `view`: path prompt and identity; nullopt when cancelled.
    std::optional<native::SaveRequest> prepareSave(DocumentView &view, bool saveAs);
    bool saveNow(DocumentView &view, bool saveAs);
    void finishClose(DocumentView *view);
    void onCurrentDocumentChanged(int index);
    void updateDocumentState();
    void updateTabTitle(DocumentView *view);
    void updateZoomLabel();
    void updateDurability();
    void updateJobLabel();
    void togglePanels();
    void focusRegion(int direction);
    void stepBrushSize(bool larger);

    std::shared_ptr<ShellPrompts> prompts_;
    std::shared_ptr<render::CpuEvaluator> evaluator_;
    std::unique_ptr<native::RecoverySession> recovery_;
    QString recoveryProblem_;
    ColorState *colors_ = nullptr;
    ToolSettings toolSettings_;
    QString activeTool_;
    int untitled_ = 0;
    int jobs_ = 0;
    QString jobText_;
    bool panelsHidden_ = false;
    std::vector<QWidget *> hiddenPanels_;
    QPointer<DocumentView> previousView_;

    std::map<QString, QAction *> actions_;
    std::map<QString, QMenu *> menus_;
    QActionGroup *toolGroup_ = nullptr;
    QToolBar *toolStrip_ = nullptr;
    OptionsRow *options_ = nullptr;
    QStackedWidget *central_ = nullptr;
    QWidget *emptyPage_ = nullptr;
    QTabWidget *tabs_ = nullptr;
    QDockWidget *colorDock_ = nullptr;
    QDockWidget *layersDock_ = nullptr;
    ColorPanel *colorPanel_ = nullptr;
    LayersPanel *layersPanel_ = nullptr;
    QLabel *jobLabel_ = nullptr;
    QLabel *coordinatesLabel_ = nullptr;
    QLabel *zoomLabel_ = nullptr;
    QLabel *durabilityLabel_ = nullptr;
    QTimer *durabilityTimer_ = nullptr;
    bool docksSized_ = false;

    QThreadPool pool_; // last: destroyed first, after waiting for its jobs
};

} // namespace archmage::ui
