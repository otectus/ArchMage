#pragma once

#include <QString>

#include <memory>
#include <optional>

class QWidget;

namespace archmage::ui {

/// Parameters of File > New.
struct NewDocumentRequest {
    int width = 1920;
    int height = 1080;
    bool transparent = false;    // otherwise a white background layer
    QString sampleType = QStringLiteral("uint8"); // "uint8" or "float32"
};

/// Every question the shell asks the user (plan S1-7.2). The application uses Qt dialogs; tests supply
/// scripted answers, so the offscreen UI tier never waits on a modal dialog (spec §23.3: nothing headless
/// waits for an invisible dialog).
class ShellPrompts {
public:
    enum class CloseChoice { Save, Discard, Cancel };

    virtual ~ShellPrompts() = default;
    virtual std::optional<NewDocumentRequest> newDocument(QWidget *parent) = 0;
    virtual std::optional<QString> openPath(QWidget *parent) = 0;
    /// Where to save; `suggested` is a file name or path.
    virtual std::optional<QString> savePath(QWidget *parent, const QString &suggested) = 0;
    /// Asked when closing a modified document.
    virtual CloseChoice confirmClose(QWidget *parent, const QString &documentName) = 0;
    virtual void information(QWidget *parent, const QString &title, const QString &text) = 0;
    virtual void error(QWidget *parent, const QString &title, const QString &text) = 0;
};

/// Qt dialogs: QFileDialog, QMessageBox and a small New Document dialog.
[[nodiscard]] std::shared_ptr<ShellPrompts> makeDialogPrompts();

} // namespace archmage::ui
