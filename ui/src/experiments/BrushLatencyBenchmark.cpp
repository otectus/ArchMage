#include "archmage/ui/experiments/BrushLatencyBenchmark.h"

#include "archmage/commands/Command.h"
#include "archmage/render/CpuEvaluator.h"
#include "archmage/ui/DocumentView.h"
#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/tools/BrushTool.h"

#include <QApplication>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointingDevice>
#include <QTabletEvent>
#include <QTimer>

#include <cmath>
#include <cstdio>
#include <numbers>

namespace archmage::ui::experiments {

int runBrushLatencyBenchmark(QApplication &app, const BrushLatencyOptions &options) {
    auto evaluator = std::make_shared<render::CpuEvaluator>();
    auto created = commands::createDocument(commands::CommandEnvelope::make(
        "archmage.document.create",
        {{"width", options.canvasSize}, {"height", options.canvasSize}, {"title", "Latency"}}));
    if (!created) {
        std::printf("EVIDENCE brush-latency {\"error\":\"%s\"}\n", base::format(created.error()).c_str());
        return 2;
    }
    DocumentSession session(std::move(created).value().state, QStringLiteral("New Document"), evaluator);
    DocumentView view(session);
    view.setTool(std::make_unique<BrushTool>());
    view.resize(1280, 800);
    view.show();

    // A pen with pressure, so the pressure-to-size path runs (spec §16.1 PNT-03).
    static const QPointingDevice pen(QStringLiteral("ArchMage benchmark pen"), 4242,
                                     QInputDevice::DeviceType::Stylus, QPointingDevice::PointerType::Pen,
                                     QInputDevice::Capability::Position | QInputDevice::Capability::Pressure,
                                     1, 3);
    const auto send = [&view](QEvent::Type type, QPointF position, double pressure) {
        QTabletEvent event(type, &pen, position, position, pressure, 0.0, 0.0, 0.0, 0.0, 0.0, Qt::NoModifier,
                           type == QEvent::TabletRelease ? Qt::LeftButton : Qt::LeftButton,
                           type == QEvent::TabletRelease ? Qt::NoButton : Qt::LeftButton);
        QCoreApplication::sendEvent(view.surface().widget(), &event);
    };

    int events = 0;
    int strokes = 0;
    int exitCode = 0;
    QTimer timer;
    timer.setTimerType(Qt::PreciseTimer);
    timer.setInterval(std::max(1, static_cast<int>(std::lround(1000.0 / options.rateHz))));
    std::int64_t started = 0;
    QTimer::singleShot(1500, &view, [&] {
        // Actual pixels at the canvas center: a full window of visible canvas, the costly common case.
        const double dpr = view.surface().devicePixelRatio();
        const QSize device = view.surface().deviceSize();
        view.surface().setViewTransform(render::ViewTransform::actualPixels(
            {options.canvasSize / 2.0, options.canvasSize / 2.0},
            {static_cast<double>(device.width()), static_cast<double>(device.height())}, dpr));
        view.surface().latency().reset();
        started = canvas::LatencyTrace::nowNs();
        timer.start();
    });
    QObject::connect(&timer, &QTimer::timeout, &view, [&] {
        const double t = static_cast<double>(canvas::LatencyTrace::nowNs() - started) / 1.0e9;
        const QPointF center(view.width() / 2.0, view.height() / 2.0);
        const double phase = std::fmod(t, 1.0); // one stroke per second
        const QPointF at = center + QPointF(std::cos(t * 2.0 * std::numbers::pi * 0.7) * 300.0,
                                            std::sin(t * 2.0 * std::numbers::pi * 1.1) * 200.0);
        const double pressure = 0.2 + 0.8 * std::sin(phase * std::numbers::pi);
        if (events == 0 || (events % static_cast<int>(options.rateHz) == 0)) {
            if (events > 0) {
                send(QEvent::TabletRelease, at, pressure);
            }
            send(QEvent::TabletPress, at, pressure);
            ++strokes;
        } else {
            send(QEvent::TabletMove, at, pressure);
        }
        ++events;
        if (t >= options.seconds) {
            timer.stop();
            send(QEvent::TabletRelease, at, pressure);
            QTimer::singleShot(500, &view, [&] {
                const canvas::LatencyTrace::Stats stats = view.surface().latency().total();
                const QJsonObject result{
                    {QStringLiteral("platform"), QGuiApplication::platformName()},
                    {QStringLiteral("surface"), canvas::CanvasSurface::kindName(view.surface().kind())},
                    {QStringLiteral("canvas"),
                     QStringLiteral("%1x%1 RGBA8, white background").arg(options.canvasSize)},
                    {QStringLiteral("view"), QStringLiteral("actual pixels, %1x%2 device")
                                                 .arg(view.surface().deviceSize().width())
                                                 .arg(view.surface().deviceSize().height())},
                    {QStringLiteral("brush"),
                     QStringLiteral("round, size 24, hardness 0.8, spacing 0.1, pressure to size")},
                    {QStringLiteral("input_rate_hz"), options.rateHz},
                    {QStringLiteral("events"), events},
                    {QStringLiteral("strokes"), strokes},
                    {QStringLiteral("frames"), stats.frames},
                    {QStringLiteral("p50_ms"), stats.p50Ms},
                    {QStringLiteral("p95_ms"), stats.p95Ms},
                    {QStringLiteral("max_ms"), stats.maxMs},
                    {QStringLiteral("render_p50_ms"), stats.renderP50Ms},
                    {QStringLiteral("render_p95_ms"), stats.renderP95Ms},
                    {QStringLiteral("history_entries"),
                     static_cast<int>(session.executor().history().size())},
                    {QStringLiteral("budget_p95_ms"), 25.0},
                    {QStringLiteral("within_budget"), stats.p95Ms <= 25.0}};
                std::printf("EVIDENCE brush-latency %s\n",
                            QJsonDocument(result).toJson(QJsonDocument::Compact).constData());
                std::fflush(stdout);
                QApplication::quit();
            });
        }
    });
    exitCode = QApplication::exec();
    (void)app;
    return exitCode;
}

} // namespace archmage::ui::experiments
