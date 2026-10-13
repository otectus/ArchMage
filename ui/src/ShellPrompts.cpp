#include "archmage/ui/ShellPrompts.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QMessageBox>
#include <QSpinBox>

namespace archmage::ui {
namespace {

constexpr int kMaxDimension = 300000; // spec §6.3, document::kMaxDimension

class NewDocumentDialog final : public QDialog {
public:
    explicit NewDocumentDialog(QWidget *parent) : QDialog(parent) {
        setWindowTitle(QStringLiteral("New Document"));
        width_ = dimension(1920);
        height_ = dimension(1080);
        background_ = new QComboBox(this);
        background_->addItems({QStringLiteral("White"), QStringLiteral("Transparent")});
        precision_ = new QComboBox(this);
        precision_->addItems({QStringLiteral("8-bit integer"), QStringLiteral("32-bit float")});
        auto *form = new QFormLayout;
        form->addRow(QStringLiteral("&Width"), width_);
        form->addRow(QStringLiteral("&Height"), height_);
        form->addRow(QStringLiteral("&Background"), background_);
        form->addRow(QStringLiteral("&Precision"), precision_);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        auto *layout = new QFormLayout(this);
        layout->addRow(form);
        layout->addRow(buttons);
    }

    [[nodiscard]] NewDocumentRequest request() const {
        NewDocumentRequest request;
        request.width = width_->value();
        request.height = height_->value();
        request.transparent = background_->currentIndex() == 1;
        request.sampleType = precision_->currentIndex() == 1 ? QStringLiteral("float32") : QStringLiteral("uint8");
        return request;
    }

private:
    QSpinBox *dimension(int value) {
        auto *box = new QSpinBox(this);
        box->setRange(1, kMaxDimension);
        box->setValue(value);
        box->setSuffix(QStringLiteral(" px"));
        return box;
    }

    QSpinBox *width_ = nullptr;
    QSpinBox *height_ = nullptr;
    QComboBox *background_ = nullptr;
    QComboBox *precision_ = nullptr;
};

const QString kFilter = QStringLiteral("ArchMage documents (*.archmage)");

class DialogPrompts final : public ShellPrompts {
public:
    std::optional<NewDocumentRequest> newDocument(QWidget *parent) override {
        NewDocumentDialog dialog(parent);
        if (dialog.exec() != QDialog::Accepted) {
            return std::nullopt;
        }
        return dialog.request();
    }

    std::optional<QString> openPath(QWidget *parent) override {
        const QString path = QFileDialog::getOpenFileName(parent, QStringLiteral("Open"), QString(), kFilter);
        return path.isEmpty() ? std::nullopt : std::optional<QString>(path);
    }

    std::optional<QString> savePath(QWidget *parent, const QString &suggested) override {
        QString path = QFileDialog::getSaveFileName(parent, QStringLiteral("Save As"), suggested, kFilter);
        if (path.isEmpty()) {
            return std::nullopt;
        }
        if (!path.endsWith(QStringLiteral(".archmage"), Qt::CaseInsensitive)) {
            path += QStringLiteral(".archmage");
        }
        return path;
    }

    CloseChoice confirmClose(QWidget *parent, const QString &documentName) override {
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Save Changes"),
                        QStringLiteral("Save the changes to \"%1\" before closing?").arg(documentName),
                        QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, parent);
        box.setDefaultButton(QMessageBox::Save);
        box.setInformativeText(QStringLiteral("If you discard them, the changes are lost."));
        switch (box.exec()) {
        case QMessageBox::Save:
            return CloseChoice::Save;
        case QMessageBox::Discard:
            return CloseChoice::Discard;
        default:
            return CloseChoice::Cancel;
        }
    }

    void information(QWidget *parent, const QString &title, const QString &text) override {
        QMessageBox::information(parent, title, text);
    }

    void error(QWidget *parent, const QString &title, const QString &text) override {
        QMessageBox::warning(parent, title, text);
    }
};

} // namespace

std::shared_ptr<ShellPrompts> makeDialogPrompts() { return std::make_shared<DialogPrompts>(); }

} // namespace archmage::ui
