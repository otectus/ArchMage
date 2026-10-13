#include "archmage/ui/panels/ColorPanel.h"

#include "archmage/ui/ColorState.h"

#include <QAction>
#include <QColorDialog>
#include <QGridLayout>
#include <QLabel>
#include <QPainter>
#include <QToolButton>

namespace archmage::ui {
namespace {

QIcon swatch(const QColor &color) {
    QPixmap pixmap(36, 36);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.fillRect(QRect(2, 2, 32, 32), color);
    painter.setPen(QPen(QColor(0x8A, 0x89, 0x94), 1));
    painter.drawRect(QRect(2, 2, 31, 31));
    return QIcon(pixmap);
}

QToolButton *swatchButton(const QString &name, QWidget *parent) {
    auto *button = new QToolButton(parent);
    button->setAccessibleName(name);
    button->setIconSize(QSize(36, 36));
    button->setAutoRaise(true);
    return button;
}

} // namespace

ColorPanel::ColorPanel(ColorState &colors, QAction *swap, QAction *reset, QWidget *parent)
    : QWidget(parent), colors_(colors) {
    setObjectName(QStringLiteral("archmageColorPanel"));
    foreground_ = swatchButton(QStringLiteral("Foreground color"), this);
    foreground_->setObjectName(QStringLiteral("archmageForegroundSwatch"));
    background_ = swatchButton(QStringLiteral("Background color"), this);
    background_->setObjectName(QStringLiteral("archmageBackgroundSwatch"));
    auto *swapButton = new QToolButton(this);
    swapButton->setDefaultAction(swap);
    swapButton->setAutoRaise(true);
    swapButton->setIconSize(QSize(20, 20));
    swapButton->setAccessibleName(QStringLiteral("Swap colors"));
    auto *resetButton = new QToolButton(this);
    resetButton->setDefaultAction(reset);
    resetButton->setAutoRaise(true);
    resetButton->setIconSize(QSize(20, 20));
    resetButton->setAccessibleName(QStringLiteral("Default colors"));
    values_ = new QLabel(this);
    values_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *layout = new QGridLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->addWidget(foreground_, 0, 0);
    layout->addWidget(background_, 0, 1);
    layout->addWidget(swapButton, 0, 2);
    layout->addWidget(resetButton, 0, 3);
    layout->addWidget(values_, 1, 0, 1, 5);
    layout->setColumnStretch(4, 1);

    connect(foreground_, &QToolButton::clicked, this, [this] { pick(true); });
    connect(background_, &QToolButton::clicked, this, [this] { pick(false); });
    connect(&colors_, &ColorState::changed, this, &ColorPanel::refresh);
    refresh();
}

void ColorPanel::refresh() {
    const QColor fg = colors_.foreground();
    const QColor bg = colors_.background();
    foreground_->setIcon(swatch(fg));
    foreground_->setToolTip(QStringLiteral("Foreground color %1 (click to change)").arg(fg.name()));
    background_->setIcon(swatch(bg));
    background_->setToolTip(QStringLiteral("Background color %1 (click to change)").arg(bg.name()));
    values_->setText(QStringLiteral("Foreground %1   Background %2").arg(fg.name(), bg.name()));
}

void ColorPanel::pick(bool foreground) {
    const QColor current = foreground ? colors_.foreground() : colors_.background();
    const QColor chosen = QColorDialog::getColor(current, this,
                                                 foreground ? QStringLiteral("Foreground Color")
                                                            : QStringLiteral("Background Color"));
    if (!chosen.isValid()) {
        return;
    }
    if (foreground) {
        colors_.setForeground(chosen);
    } else {
        colors_.setBackground(chosen);
    }
}

} // namespace archmage::ui
