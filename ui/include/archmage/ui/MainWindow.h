#pragma once

#include <QMainWindow>

#include <memory>

class QAction;

namespace archmage::render {
class CpuEvaluator;
}

namespace archmage::ui {

class DocumentSession;
class DocumentView;

/// Top-level desktop window. Stage 1 interim (S1-3): one new 2048 × 2048 document with the brush tool and
/// Edit > Undo and Redo. S1-7 replaces this with the spec §2.2 regions, document tabs and panels; only
/// working actions are shown (spec §29.2).
///
/// Ownership: an ordinary QWidget, owned by its creator when top-level or by its Qt parent.
/// Thread safety: GUI thread only, like every QWidget.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] DocumentSession *session() const { return session_; }
    [[nodiscard]] DocumentView *view() const { return view_; }

private:
    void updateActions();

    std::shared_ptr<render::CpuEvaluator> evaluator_;
    DocumentSession *session_ = nullptr;
    DocumentView *view_ = nullptr;
    QAction *undo_ = nullptr;
    QAction *redo_ = nullptr;
};

} // namespace archmage::ui
