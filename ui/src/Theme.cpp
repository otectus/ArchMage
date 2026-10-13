#include "archmage/ui/Theme.h"

#include <QApplication>
#include <QFile>
#include <QPalette>
#include <QStyleFactory>

namespace archmage::ui::theme {

QColor accent() { return {0x8B, 0x6F, 0xE8}; }

void apply(QApplication &app) {
    if (QStyle *fusion = QStyleFactory::create(QStringLiteral("Fusion"))) {
        QApplication::setStyle(fusion);
    }
    const QColor window(0x2A, 0x2A, 0x2F);
    const QColor base(0x1E, 0x1E, 0x22);
    const QColor alternate(0x25, 0x25, 0x2A);
    const QColor text(0xE2, 0xE1, 0xEA);
    const QColor button(0x33, 0x33, 0x39);
    const QColor disabled(0x7A, 0x79, 0x84);

    QPalette palette;
    palette.setColor(QPalette::Window, window);
    palette.setColor(QPalette::WindowText, text);
    palette.setColor(QPalette::Base, base);
    palette.setColor(QPalette::AlternateBase, alternate);
    palette.setColor(QPalette::ToolTipBase, QColor(0x3A, 0x3A, 0x42));
    palette.setColor(QPalette::ToolTipText, text);
    palette.setColor(QPalette::PlaceholderText, disabled);
    palette.setColor(QPalette::Text, text);
    palette.setColor(QPalette::Button, button);
    palette.setColor(QPalette::ButtonText, text);
    palette.setColor(QPalette::BrightText, Qt::white);
    palette.setColor(QPalette::Highlight, accent());
    palette.setColor(QPalette::HighlightedText, Qt::white);
    palette.setColor(QPalette::Link, accent().lighter(125));
    palette.setColor(QPalette::Light, QColor(0x45, 0x45, 0x4C));
    palette.setColor(QPalette::Midlight, QColor(0x3A, 0x3A, 0x41));
    palette.setColor(QPalette::Mid, QColor(0x26, 0x26, 0x2B));
    palette.setColor(QPalette::Dark, QColor(0x1A, 0x1A, 0x1E));
    palette.setColor(QPalette::Shadow, QColor(0x0E, 0x0E, 0x10));
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        palette.setColor(QPalette::Disabled, role, disabled);
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor(0x4A, 0x4A, 0x52));
    QApplication::setPalette(palette);

    app.setStyleSheet(QStringLiteral(R"(
QToolBar { border: 0px; spacing: 2px; }
QToolBar#archmageToolStrip QToolButton { margin: 1px 2px; padding: 3px; border-radius: 4px; }
QToolBar#archmageToolStrip QToolButton:checked { background: %1; }
QToolBar#archmageOptionsRow QLabel { padding: 0px 2px 0px 8px; }
QDockWidget::title { padding: 4px 8px; }
QTabBar::tab:selected { border-bottom: 2px solid %1; }
QStatusBar QLabel { padding: 0px 6px; }
)").arg(accent().name()));
}

QIcon icon(const QString &name) {
    const QString path = QStringLiteral(":/archmage/icons/%1.svg").arg(name);
    return QFile::exists(path) ? QIcon(path) : QIcon();
}

} // namespace archmage::ui::theme
