#pragma once

#include "archmage/ui/canvas/CanvasSurface.h"

#include <QString>
#include <QStringList>

class QApplication;

namespace archmage::ui::experiments {

/// Options of the canvas adapter experiment (S1-1.2, M0-E1), started with
/// `archmage --experiment=canvas:<raster|rhi|vulkan>`.
struct CanvasExperimentOptions {
    canvas::CanvasSurface::Kind kind = canvas::CanvasSurface::Kind::Raster;
    /// Steps run after the first frame, e.g. {"report:start", "popup:menu", "quit"}; empty for manual use.
    QStringList script;
    /// Directory receiving frame PNGs written by `report` steps.
    QString evidenceDirectory = QStringLiteral("/tmp/archmage-canvas-experiment");
    /// How long a SNAP point holds still so an external tool can take a screenshot.
    int snapWaitMs = 1500;
};

/// Opens the experiment window and runs its event loop; returns the process exit code.
///
/// The window has a menu bar, an options tool bar, a docked and a floating dock widget, a status bar and
/// the canvas surface. Script steps print one line each to stdout:
///   EVIDENCE <kind> <json>   measured facts (geometry, focus, latency, validation counts)
///   SNAP <tag>               the screen holds still for snapWaitMs: take a screenshot now
///   MOVE <target>            an external driver should move the window to <target>; the step waits for
///                            the device pixel ratio or size to change (6 s timeout)
/// Steps: wait:<ms>, fit, actual, report:<tag>, popup:<tag>, tooltip:<tag>, dock-float:<tag>,
/// dock-redock:<tag>, resize-storm:<tag>, dialog:<tag>, drag:<ms>, move:<target>, quit.
int runCanvasExperiment(QApplication &app, const CanvasExperimentOptions &options);

} // namespace archmage::ui::experiments
