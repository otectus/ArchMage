// Helpers for the shell UI tests (S1-7): scripted prompts instead of modal dialogs, a window with its own
// recovery folder, and waits for asynchronous command results.
#pragma once

#include "archmage/native/Recovery.h"
#include "archmage/ui/DocumentView.h"
#include "archmage/ui/MainWindow.h"
#include "archmage/ui/document/DocumentSession.h"

#include <QTemporaryDir>
#include <QTest>

#include <deque>
#include <memory>

namespace archmage::testing {

/// Answers the shell's questions from queues; an empty queue means "cancel".
class ScriptedPrompts final : public ui::ShellPrompts {
public:
    std::deque<ui::NewDocumentRequest> newDocuments;
    std::deque<QString> openPaths;
    std::deque<QString> savePaths;
    std::deque<CloseChoice> closeChoices;
    QStringList informations;
    QStringList errors;
    int closeQuestions = 0;

    std::optional<ui::NewDocumentRequest> newDocument(QWidget *) override { return take(newDocuments); }
    std::optional<QString> openPath(QWidget *) override { return take(openPaths); }
    std::optional<QString> savePath(QWidget *, const QString &) override { return take(savePaths); }
    CloseChoice confirmClose(QWidget *, const QString &) override {
        ++closeQuestions;
        return closeChoices.empty() ? CloseChoice::Cancel : pop(closeChoices);
    }
    void information(QWidget *, const QString &, const QString &text) override { informations << text; }
    void error(QWidget *, const QString &, const QString &text) override { errors << text; }

private:
    template <class T> static std::optional<T> take(std::deque<T> &queue) {
        if (queue.empty()) {
            return std::nullopt;
        }
        return pop(queue);
    }
    template <class T> static T pop(std::deque<T> &queue) {
        T value = queue.front();
        queue.pop_front();
        return value;
    }
};

/// A shown, active 1440 × 900 window whose recovery journals live in a temporary folder.
struct ShellFixture {
    QTemporaryDir state;
    std::shared_ptr<ScriptedPrompts> prompts = std::make_shared<ScriptedPrompts>();
    std::unique_ptr<ui::MainWindow> window;

    ShellFixture() {
        ui::MainWindow::Options options;
        options.prompts = prompts;
        options.recoveryRoot = std::filesystem::path(state.path().toStdString());
        window = std::make_unique<ui::MainWindow>(options);
        window->resize(1440, 900);
        window->show();
        window->activateWindow();
    }

    [[nodiscard]] bool ready() { return QTest::qWaitForWindowExposed(window.get()) && QTest::qWaitForWindowActive(window.get()); }

    /// A new white document, with its canvas showing actual pixels at the origin (document = widget points).
    ui::DocumentView *newDocument(int width = 400, int height = 300) {
        ui::NewDocumentRequest request;
        request.width = width;
        request.height = height;
        ui::DocumentView *view = window->newDocument(request);
        if (view != nullptr) {
            (void)QTest::qWaitFor([view] { return view->surface().framesPresented() > 0; }, 5000);
            view->surface().setViewTransform(render::ViewTransform{1.0, {0.0, 0.0}, view->surface().devicePixelRatio()});
        }
        return view;
    }

    [[nodiscard]] std::filesystem::path path(const QString &name) const {
        return std::filesystem::path(state.path().toStdString()) / name.toStdString();
    }
};

/// Waits until the session has delivered at least `sequence` to the GUI thread.
[[nodiscard]] inline bool waitForRevision(ui::DocumentSession &session, std::uint64_t sequence, int ms = 5000) {
    return QTest::qWaitFor([&] { return session.revision()->sequence >= sequence; }, ms);
}

inline QPointF widgetPoint(ui::DocumentView &view, double x, double y) {
    const auto logical = view.surface().viewTransform().documentToLogical(base::DPoint{x, y});
    return {logical.x, logical.y};
}

inline void mouse(QWidget *widget, QEvent::Type type, QPointF at, Qt::MouseButton button, Qt::MouseButtons buttons,
                  Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    QMouseEvent event(type, at, widget->mapToGlobal(at), button, buttons, modifiers);
    QCoreApplication::sendEvent(widget, &event);
}

/// Press, a few moves and release with the left button, in widget coordinates.
inline void drag(QWidget *widget, QPointF from, QPointF to, int steps = 8, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
    mouse(widget, QEvent::MouseButtonPress, from, Qt::LeftButton, Qt::LeftButton, modifiers);
    for (int i = 1; i <= steps; ++i) {
        const double t = static_cast<double>(i) / steps;
        mouse(widget, QEvent::MouseMove, from + (to - from) * t, Qt::NoButton, Qt::LeftButton, modifiers);
    }
    mouse(widget, QEvent::MouseButtonRelease, to, Qt::LeftButton, Qt::NoButton, modifiers);
}

} // namespace archmage::testing
