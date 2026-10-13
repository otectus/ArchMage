#include "archmage/ui/OptionsRow.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QStackedWidget>
#include <QWheelEvent>

#include <cmath>

namespace archmage::ui {

OptionsRow::OptionsRow(ToolSettings &settings, QWidget *parent)
    : QToolBar(QStringLiteral("Options"), parent), settings_(settings) {
    setObjectName(QStringLiteral("archmageOptionsRow"));
    setMovable(false);
    setFloatable(false);
    setFixedHeight(36);
    pages_ = new QStackedWidget(this);
    brush_ = brushPage();
    navigation_ = navigationPage();
    eyedropper_ = eyedropperPage();
    for (QWidget *page : {brush_, navigation_, eyedropper_}) {
        pages_->addWidget(page);
    }
    addWidget(pages_);
    showOptionsFor(QString::fromLatin1(tool_ids::kBrush));
}

QSpinBox *OptionsRow::spinBox(const QString &accessibleName, int minimum, int maximum, const QString &suffix) {
    auto *box = new QSpinBox;
    box->setRange(minimum, maximum);
    box->setSuffix(suffix);
    box->setAccessibleName(accessibleName);
    box->setToolTip(accessibleName);
    box->setFocusPolicy(Qt::StrongFocus); // never focused by the wheel alone
    box->installEventFilter(this);
    box->setKeyboardTracking(false);
    return box;
}

QWidget *OptionsRow::brushPage() {
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    size_ = spinBox(QStringLiteral("Brush size"), 1, 5000, QStringLiteral(" px"));
    size_->setObjectName(QStringLiteral("archmageBrushSize"));
    hardness_ = spinBox(QStringLiteral("Brush hardness"), 0, 100, QStringLiteral(" %"));
    hardness_->setObjectName(QStringLiteral("archmageBrushHardness"));
    opacity_ = spinBox(QStringLiteral("Brush opacity"), 1, 100, QStringLiteral(" %"));
    opacity_->setObjectName(QStringLiteral("archmageBrushOpacity"));
    const auto row = [&](const QString &text, QSpinBox *box) {
        auto *label = new QLabel(text);
        label->setBuddy(box);
        layout->addWidget(label);
        layout->addWidget(box);
    };
    // No mnemonics: Alt+letter belongs to the menu bar (S1-7.9).
    row(QStringLiteral("Size"), size_);
    row(QStringLiteral("Hardness"), hardness_);
    row(QStringLiteral("Opacity"), opacity_);
    layout->addStretch(1);
    connect(size_, &QSpinBox::valueChanged, this, [this](int value) {
        if (!syncing_) {
            settings_.brush.size = value;
        }
    });
    connect(hardness_, &QSpinBox::valueChanged, this, [this](int value) {
        if (!syncing_) {
            settings_.brush.hardness = value / 100.0;
        }
    });
    connect(opacity_, &QSpinBox::valueChanged, this, [this](int value) {
        if (!syncing_) {
            settings_.brush.opacity = value / 100.0;
        }
    });
    return page;
}

QWidget *OptionsRow::navigationPage() {
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *fit = new QPushButton(QStringLiteral("Fit on Screen"));
    fit->setToolTip(QStringLiteral("Fit on Screen (Ctrl+0)"));
    auto *actual = new QPushButton(QStringLiteral("100 %"));
    actual->setToolTip(QStringLiteral("Actual Pixels (Ctrl+1)"));
    actual->setAccessibleName(QStringLiteral("Actual pixels"));
    layout->addWidget(fit);
    layout->addWidget(actual);
    layout->addWidget(new QLabel(QStringLiteral("Click to zoom in, Alt-click to zoom out, drag sideways to scrub; "
                                                "hold Space to pan with any tool")));
    layout->addStretch(1);
    connect(fit, &QPushButton::clicked, this, &OptionsRow::fitRequested);
    connect(actual, &QPushButton::clicked, this, &OptionsRow::actualPixelsRequested);
    return page;
}

QWidget *OptionsRow::eyedropperPage() {
    auto *page = new QWidget;
    auto *layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(new QLabel(QStringLiteral("Samples one pixel of the visible composite as the foreground color")));
    layout->addStretch(1);
    return page;
}

void OptionsRow::showOptionsFor(const QString &toolId) {
    tool_ = toolId;
    if (toolId == QLatin1String(tool_ids::kBrush)) {
        pages_->setCurrentWidget(brush_);
    } else if (toolId == QLatin1String(tool_ids::kEyedropper)) {
        pages_->setCurrentWidget(eyedropper_);
    } else {
        pages_->setCurrentWidget(navigation_);
    }
    refresh();
}

void OptionsRow::refresh() {
    syncing_ = true;
    size_->setValue(static_cast<int>(std::lround(settings_.brush.size)));
    hardness_->setValue(static_cast<int>(std::lround(settings_.brush.hardness * 100.0)));
    opacity_->setValue(static_cast<int>(std::lround(settings_.brush.opacity * 100.0)));
    syncing_ = false;
}

bool OptionsRow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::Wheel) {
        if (auto *box = qobject_cast<QSpinBox *>(watched); box != nullptr && !box->hasFocus()) {
            event->ignore();
            return true; // spec §11: the wheel changes a value only in the control being edited
        }
    }
    return QToolBar::eventFilter(watched, event);
}

} // namespace archmage::ui
