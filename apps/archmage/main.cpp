// ArchMage desktop executable.
//
// `--experiment=canvas:<raster|rhi|vulkan>` runs the S1-1 canvas adapter experiment (plan S1-1.2) instead
// of the application: optional `--script=<step,step,...>`, `--evidence-dir=<path>` and `--snap-wait=<ms>`.

#include "archmage/document/Version.h"
#include "archmage/ui/MainWindow.h"
#include "archmage/ui/canvas/VulkanSupport.h"
#include "archmage/ui/experiments/CanvasExperiment.h"

#include <QApplication>
#include <QString>

#include <cstdio>
#include <cstring>
#include <optional>
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

int main(int argc, char *argv[]) {
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

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("ArchMage"));
    QApplication::setApplicationVersion(version);
    // Becomes the Wayland app_id, which Hyprland reports as the client class.
    QApplication::setDesktopFileName(QStringLiteral("archmage"));

    archmage::ui::MainWindow window;
    window.show();
    return QApplication::exec();
}
