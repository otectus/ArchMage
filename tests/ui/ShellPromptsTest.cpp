// S1-7.2: the Qt dialogs behind ShellPrompts, which the other shell tests replace with scripted answers. File >
// New's dialog (defaults, changed values, Cancel), the close question's three answers and Escape, and the save
// path's extension.

#include "archmage/ui/ShellPrompts.h"

#include <QApplication>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <functional>

using namespace archmage;
using CloseChoice = ui::ShellPrompts::CloseChoice;

class ShellPromptsTest : public QObject {
    Q_OBJECT

private slots:
    void newDocumentDialog();
    void closeQuestionMapsEveryAnswer();
    void savePathGainsTheExtension();
};

namespace {

/// Answers the next modal dialog from inside its own event loop. A dialog still open after ten seconds is
/// rejected, so a broken answer fails the test instead of hanging it.
class ModalAnswer {
public:
    explicit ModalAnswer(std::function<void(QWidget *)> answer) : answer_(std::move(answer)) {
        QObject::connect(&poll_, &QTimer::timeout, [this] {
            QWidget *modal = QApplication::activeModalWidget();
            if (modal == nullptr || !modal->isVisible()) {
                return;
            }
            poll_.stop();
            answered_ = true;
            answer_(modal);
            QTimer::singleShot(10000, modal, [modal] {
                if (auto *dialog = qobject_cast<QDialog *>(modal)) {
                    dialog->reject();
                }
            });
        });
        poll_.start(10);
    }

    [[nodiscard]] bool answered() const { return answered_; }

private:
    std::function<void(QWidget *)> answer_;
    QTimer poll_;
    bool answered_ = false;
};

QWidget *field(QWidget *dialog, const QString &label) {
    for (QLabel *candidate : dialog->findChildren<QLabel *>()) {
        if (candidate->text() == label) {
            return candidate->buddy();
        }
    }
    return nullptr;
}

void click(QWidget *dialog, QDialogButtonBox::StandardButton which) {
    dialog->findChild<QDialogButtonBox *>()->button(which)->click();
}

} // namespace

void ShellPromptsTest::newDocumentDialog() {
    auto prompts = ui::makeDialogPrompts();
    {
        QString title;
        ModalAnswer answer([&](QWidget *dialog) {
            title = dialog->windowTitle();
            click(dialog, QDialogButtonBox::Ok);
        });
        const auto request = prompts->newDocument(nullptr);
        QVERIFY(answer.answered());
        QCOMPARE(title, QStringLiteral("New Document"));
        QVERIFY(request.has_value());
        QCOMPARE(request->width, 1920);
        QCOMPARE(request->height, 1080);
        QVERIFY(!request->transparent);
        QCOMPARE(request->sampleType, QStringLiteral("uint8"));
    }
    {
        bool found = false;
        ModalAnswer answer([&](QWidget *dialog) {
            auto *width = qobject_cast<QSpinBox *>(field(dialog, QStringLiteral("&Width")));
            auto *height = qobject_cast<QSpinBox *>(field(dialog, QStringLiteral("&Height")));
            auto *background = qobject_cast<QComboBox *>(field(dialog, QStringLiteral("&Background")));
            auto *precision = qobject_cast<QComboBox *>(field(dialog, QStringLiteral("&Precision")));
            found = width != nullptr && height != nullptr && background != nullptr && precision != nullptr;
            if (found) {
                width->setValue(640);
                height->setValue(480);
                background->setCurrentText(QStringLiteral("Transparent"));
                precision->setCurrentText(QStringLiteral("32-bit float"));
            }
            click(dialog, QDialogButtonBox::Ok);
        });
        const auto request = prompts->newDocument(nullptr);
        QVERIFY(answer.answered());
        QVERIFY(found);
        QVERIFY(request.has_value());
        QCOMPARE(request->width, 640);
        QCOMPARE(request->height, 480);
        QVERIFY(request->transparent);
        QCOMPARE(request->sampleType, QStringLiteral("float32"));
    }
    {
        ModalAnswer answer([](QWidget *dialog) { click(dialog, QDialogButtonBox::Cancel); });
        QVERIFY(!prompts->newDocument(nullptr).has_value());
        QVERIFY(answer.answered());
    }
}

void ShellPromptsTest::closeQuestionMapsEveryAnswer() {
    auto prompts = ui::makeDialogPrompts();
    const std::pair<QMessageBox::StandardButton, CloseChoice> answers[] = {
        {QMessageBox::Save, CloseChoice::Save}, {QMessageBox::Discard, CloseChoice::Discard}, {QMessageBox::Cancel, CloseChoice::Cancel}};
    for (const auto &[button, expected] : answers) {
        QString text;
        bool saveIsDefault = false;
        ModalAnswer answer([&, which = button](QWidget *dialog) {
            auto *box = qobject_cast<QMessageBox *>(dialog);
            text = box->text();
            saveIsDefault = box->defaultButton() == box->button(QMessageBox::Save);
            box->button(which)->click();
        });
        QCOMPARE(prompts->confirmClose(nullptr, QStringLiteral("poster.archmage")), expected);
        QVERIFY(answer.answered());
        QVERIFY2(text.contains(QStringLiteral("\"poster.archmage\"")), qPrintable(text));
        QVERIFY(saveIsDefault);
    }
    ModalAnswer escape([](QWidget *dialog) { QTest::keyClick(dialog, Qt::Key_Escape); });
    QCOMPARE(prompts->confirmClose(nullptr, QStringLiteral("poster.archmage")), CloseChoice::Cancel);
    QVERIFY(escape.answered());
}

void ShellPromptsTest::savePathGainsTheExtension() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    auto prompts = ui::makeDialogPrompts();
    const std::pair<QString, QString> cases[] = {
        {QStringLiteral("picture"), QStringLiteral("picture.archmage")},
        {QStringLiteral("Poster.ARCHMAGE"), QStringLiteral("Poster.ARCHMAGE")},
    };
    for (const auto &[typed, expected] : cases) {
        ModalAnswer answer([&, name = typed](QWidget *dialog) {
            auto *files = qobject_cast<QFileDialog *>(dialog);
            QVERIFY(files != nullptr);
            // Type into the file name field, as a person would; selectFile() leaves a focused field alone.
            auto *edit = files->findChild<QLineEdit *>(QStringLiteral("fileNameEdit"));
            QVERIFY(edit != nullptr);
            edit->setText(directory.filePath(name));
            static_cast<QDialog *>(files)->accept(); // QFileDialog::accept is protected; QDialog::accept is a public slot
        });
        const auto path = prompts->savePath(nullptr, QStringLiteral("Untitled-1.archmage"));
        QVERIFY(answer.answered());
        QVERIFY(path.has_value());
        QCOMPARE(QDir::cleanPath(*path), QDir::cleanPath(directory.filePath(expected)));
    }
    ModalAnswer cancel([](QWidget *dialog) { qobject_cast<QDialog *>(dialog)->reject(); });
    QVERIFY(!prompts->savePath(nullptr, QStringLiteral("Untitled-1.archmage")).has_value());
    QVERIFY(cancel.answered());
}

QTEST_MAIN(ShellPromptsTest)
#include "ShellPromptsTest.moc"
