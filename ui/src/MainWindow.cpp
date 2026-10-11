#include "archmage/ui/MainWindow.h"

namespace archmage::ui {

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setObjectName(QStringLiteral("archmageMainWindow"));
    setWindowTitle(QStringLiteral("ArchMage"));
}

} // namespace archmage::ui
