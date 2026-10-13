#pragma once

#include <QWidget>

class QAction;
class QLabel;
class QToolButton;

namespace archmage::ui {

class ColorState;

/// The Color panel (plan S1-7.4): foreground and background swatches, each opening a color dialog, and the
/// swap (X) and reset (D) commands. The swap and reset actions belong to the window, which binds their
/// shortcuts; the panel shows them as buttons.
///
/// Thread safety: GUI thread only.
class ColorPanel final : public QWidget {
    Q_OBJECT

public:
    ColorPanel(ColorState &colors, QAction *swap, QAction *reset, QWidget *parent = nullptr);

    [[nodiscard]] QToolButton &foregroundSwatch() { return *foreground_; }
    [[nodiscard]] QToolButton &backgroundSwatch() { return *background_; }

private:
    void refresh();
    void pick(bool foreground);

    ColorState &colors_;
    QToolButton *foreground_ = nullptr;
    QToolButton *background_ = nullptr;
    QLabel *values_ = nullptr;
};

} // namespace archmage::ui
