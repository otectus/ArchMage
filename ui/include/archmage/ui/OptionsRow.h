#pragma once

#include "archmage/ui/tools/Tool.h"

#include <QToolBar>

class QSpinBox;
class QStackedWidget;

namespace archmage::ui {

/// The options row (plan S1-7.1, S1-7.7; spec §2.2): about 36 logical pixels high, showing the active
/// tool's options. Brush: size, hardness and opacity, written to the window's ToolSettings and read when the
/// next stroke begins. Hand and zoom: fit and 100 %. Eyedropper: what it samples.
///
/// A spin box without keyboard focus ignores the mouse wheel, so scrolling across the row never changes a
/// value by accident (spec §11).
///
/// Thread safety: GUI thread only.
class OptionsRow final : public QToolBar {
    Q_OBJECT

public:
    explicit OptionsRow(ToolSettings &settings, QWidget *parent = nullptr);

    /// Shows the page of `toolId` (tool_ids).
    void showOptionsFor(const QString &toolId);
    [[nodiscard]] QString currentTool() const { return tool_; }
    /// Re-reads the settings, after a shortcut changed them ([ and ]).
    void refresh();

    [[nodiscard]] QSpinBox *brushSize() const { return size_; }
    [[nodiscard]] QSpinBox *brushHardness() const { return hardness_; }
    [[nodiscard]] QSpinBox *brushOpacity() const { return opacity_; }

signals:
    void fitRequested();
    void actualPixelsRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *brushPage();
    QWidget *navigationPage();
    QWidget *eyedropperPage();
    QSpinBox *spinBox(const QString &accessibleName, int minimum, int maximum, const QString &suffix);

    ToolSettings &settings_;
    QStackedWidget *pages_ = nullptr;
    QWidget *brush_ = nullptr;
    QWidget *navigation_ = nullptr;
    QWidget *eyedropper_ = nullptr;
    QSpinBox *size_ = nullptr;
    QSpinBox *hardness_ = nullptr;
    QSpinBox *opacity_ = nullptr;
    QString tool_;
    bool syncing_ = false;
};

} // namespace archmage::ui
