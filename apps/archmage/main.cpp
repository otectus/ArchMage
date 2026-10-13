// ArchMage desktop executable.
//
// `--experiment=canvas:<raster|rhi|vulkan>` runs the S1-1 canvas adapter experiment (plan S1-1.2) instead
// of the application: optional `--script=<step,step,...>`, `--evidence-dir=<path>` and `--snap-wait=<ms>`.
// `--benchmark=brush-latency [--seconds=<s>] [--canvas=<pixels>]` runs the S1-3.7 brush latency benchmark.
// `--benchmark=launch` starts the application normally, prints "EVIDENCE launch <json>" once the main window
// has painted and the event loop has turned once (a usable workspace, spec §25.2), and exits (S1-6.8).

#include "archmage/document/Version.h"
#include "archmage/ui/MainWindow.h"
#include "archmage/ui/canvas/VulkanSupport.h"
#include "archmage/ui/experiments/BrushLatencyBenchmark.h"
#include "archmage/ui/experiments/CanvasExperiment.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QEvent>
#include <QGuiApplication>
#include <QString>
#include <QTimer>

#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace {

std::optional<archmage::ui::experiments::CanvasExperimentOptions> experimentOptions(int argc, char **argv,
                                                                                    bool *invalid) {
    using archmage::ui::canvas::CanvasSurface;
    std::optional<archmage::ui::experiments::CanvasExperimentOptions> options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        const auto value = [&arg](std::string_view prefix) -> std::optional<QString> {
            if (arg.substr(0, prefix.size()) == prefix) {
                return QString::fromUtf8(arg.substr(prefix.size()));
            }
            return std::nullopt;
        };
        if (const auto experiment = value("--experiment=")) {
            const auto kind = experiment->startsWith(QStringLiteral("canvas:"))
                                  ? CanvasSurface::kindFromName(experiment->mid(7))
                                  : std::nullopt;
            if (!kind) {
                std::fprintf(stderr,
                             "archmage: unknown experiment '%s' (expected canvas:raster|rhi|vulkan)\n",
                             argv[i]);
                *invalid = true;
                return std::nullopt;
            }
            if (!options) {
                options.emplace();
            }
            options->kind = *kind;
        } else if (const auto script = value("--script=")) {
            if (!options) {
                options.emplace();
            }
            options->script = script->split(QLatin1Char(','), Qt::SkipEmptyParts);
        } else if (const auto directory = value("--evidence-dir=")) {
            if (!options) {
                options.emplace();
            }
            options->evidenceDirectory = *directory;
        } else if (const auto wait = value("--snap-wait=")) {
            if (!options) {
                options.emplace();
            }
            options->snapWaitMs = wait->toInt();
        }
    }
    return options;
}

} // namespace

std::optional<archmage::ui::experiments::BrushLatencyOptions> benchmarkOptions(int argc, char **argv) {
    std::optional<archmage::ui::experiments::BrushLatencyOptions> options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg{argv[i]};
        if (arg == "--benchmark=brush-latency") {
            options.emplace();
        } else if (options && arg.substr(0, 10) == "--seconds=") {
            options->seconds = std::stod(std::string(arg.substr(10)));
        } else if (options && arg.substr(0, 9) == "--canvas=") {
            options->canvasSize = std::stoi(std::string(arg.substr(9)));
        }
    }
    return options;
}

int main(int argc, char *argv[]) {
    if (const auto benchmark = benchmarkOptions(argc, argv)) {
        QApplication::setApplicationName(QStringLiteral("archmage-experiment"));
        QApplication::setDesktopFileName(QStringLiteral("archmage-experiment"));
        QApplication app(argc, argv);
        return archmage::ui::experiments::runBrushLatencyBenchmark(app, *benchmark);
    }
    bool invalid = false;
    const auto experiment = experimentOptions(argc, argv, &invalid);
    if (invalid) {
        return 2;
    }

    const QString version = QString::fromUtf8(archmage::document::kVersion.data(),
                                              static_cast<qsizetype>(archmage::document::kVersion.size()));
    if (experiment) {
        // A separate identity lets window rules and screenshots target experiment windows only.
        QApplication::setApplicationName(QStringLiteral("archmage-experiment"));
        QApplication::setDesktopFileName(QStringLiteral("archmage-experiment"));
        archmage::ui::canvas::VulkanSupport::configureDefaultInstance();
        QApplication app(argc, argv);
        QApplication::setApplicationVersion(version);
        return archmage::ui::experiments::runCanvasExperiment(app, *experiment);
    }

    const bool launchBenchmark = [&] {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--benchmark=launch") {
                return true;
            }
        }
        return false;
    }();
    QElapsedTimer sinceMain;
    sinceMain.start();
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ArchMage"));
    QApplication::setApplicationVersion(version);
    // Becomes the Wayland app_id, which Hyprland reports as the client class.
    QApplication::setDesktopFileName(QStringLiteral("archmage"));

    archmage::ui::MainWindow window;
    if (launchBenchmark) {
        // Ready: the first paint of the window, then one turn of the event loop for the remaining paints.
        struct FirstPaint final : QObject {
            QWidget *window = nullptr;
            QElapsedTimer *clock = nullptr;
            bool seen = false;
            bool eventFilter(QObject *object, QEvent *event) override {
                auto *widget = qobject_cast<QWidget *>(object);
                if (!seen && event->type() == QEvent::Paint && widget != nullptr && widget->window() == window) {
                    seen = true;
                    QTimer::singleShot(0, [clock = clock] {
                        std::printf("EVIDENCE launch {\"ready_ms_since_main\":%lld,\"platform\":\"%s\"}\n",
                                    static_cast<long long>(clock->elapsed()),
                                    QGuiApplication::platformName().toUtf8().constData());
                        std::fflush(stdout);
                        QApplication::quit();
                    });
                }
                return false;
            }
        };
        static FirstPaint watcher;
        watcher.window = &window;
        watcher.clock = &sinceMain;
        app.installEventFilter(&watcher);
    }
    window.show();
    return QApplication::exec();
}
