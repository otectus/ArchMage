// ArchMage desktop executable. S1-0: an empty main window that opens natively under Wayland
// (Qt's wayland platform plugin) and under X11/XWayland (xcb).

#include "archmage/document/Version.h"
#include "archmage/ui/MainWindow.h"

#include <QApplication>
#include <QString>

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ArchMage"));
    QApplication::setApplicationVersion(QString::fromUtf8(
        archmage::document::kVersion.data(), static_cast<qsizetype>(archmage::document::kVersion.size())));
    // Becomes the Wayland app_id, which Hyprland reports as the client class.
    QApplication::setDesktopFileName(QStringLiteral("archmage"));

    archmage::ui::MainWindow window;
    window.show();
    return QApplication::exec();
}
