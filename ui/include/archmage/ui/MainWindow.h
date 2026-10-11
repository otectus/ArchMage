#pragma once

#include <QMainWindow>

namespace archmage::ui {

/// Top-level desktop window. Empty in S1-0; the spec §2.2 regions (menu bar, options row,
/// tool strip, document tabs, right dock, status bar) arrive in S1-7.
///
/// Ownership: an ordinary QWidget, owned by its creator when top-level or by its Qt parent.
/// Thread safety: GUI thread only, like every QWidget.
class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
};

} // namespace archmage::ui
