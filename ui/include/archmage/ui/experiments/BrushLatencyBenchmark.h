#pragma once

#include <QString>

class QApplication;

namespace archmage::ui::experiments {

/// Plan S1-3.7, spec §25.2 "simple brush preview": event-to-submission latency of a pressure stroke on a
/// 4096 × 4096 RGBA8 canvas, driven by synthetic 240 Hz tablet events through the real input, tool,
/// preview and canvas path. Started with `archmage --benchmark=brush-latency`; prints one
/// `EVIDENCE brush-latency <json>` line and exits.
struct BrushLatencyOptions {
    int canvasSize = 4096;
    double seconds = 5.0;
    double rateHz = 240.0;
};

int runBrushLatencyBenchmark(QApplication &app, const BrushLatencyOptions &options);

} // namespace archmage::ui::experiments
