#include "archmage/ui/ColorState.h"

#include <utility>

namespace archmage::ui {

ColorState::ColorState(QObject *parent) : QObject(parent) {}

void ColorState::setForeground(const QColor &color) {
    if (color.isValid() && color != foreground_) {
        foreground_ = color.toRgb();
        emit changed();
    }
}

void ColorState::setBackground(const QColor &color) {
    if (color.isValid() && color != background_) {
        background_ = color.toRgb();
        emit changed();
    }
}

void ColorState::swap() {
    std::swap(foreground_, background_);
    emit changed();
}

void ColorState::reset() {
    foreground_ = Qt::black;
    background_ = Qt::white;
    emit changed();
}

} // namespace archmage::ui
