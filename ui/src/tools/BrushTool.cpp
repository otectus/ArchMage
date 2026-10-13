#include "archmage/ui/tools/BrushTool.h"

#include "archmage/commands/Command.h"
#include "archmage/ui/ColorState.h"
#include "archmage/ui/document/DocumentSession.h"

namespace archmage::ui {
namespace {

brush::StrokeSample toStrokeSample(const input::PointerSample &sample) {
    return {sample.posDocument, sample.hasPressure ? static_cast<float>(sample.pressure) : 1.0f,
            sample.timestampNs};
}

} // namespace

BrushTool::BrushTool(ToolSettings *settings, brush::PaintMode mode)
    : settings_(settings != nullptr ? settings : &own_), mode_(mode) {}

QString BrushTool::id() const {
    return mode_ == brush::PaintMode::Erase ? QStringLiteral("eraser") : QString::fromLatin1(tool_ids::kBrush);
}

bool BrushTool::pointer(const input::PointerSample &sample) {
    if (host_ == nullptr) {
        return false;
    }
    switch (sample.phase) {
    case input::PointerPhase::Press:
        if (sample.button == Qt::LeftButton) {
            begin(sample);
            return true;
        }
        return false;
    case input::PointerPhase::Move:
        if (session_) {
            extend(sample);
            return true;
        }
        return false;
    case input::PointerPhase::Release:
        if (session_ && sample.button == Qt::LeftButton) {
            extend(sample);
            commit();
            return true;
        }
        return false;
    case input::PointerPhase::Hover:
        return false;
    }
    return false;
}

void BrushTool::begin(const input::PointerSample &sample) {
    if (session_) {
        commit();
    }
    DocumentSession &document = host_->session();
    const auto state = host_->displayedState();
    const document::Layer *layer = state->findLayer(document.activeLayer());
    if (layer == nullptr) {
        host_->reportError(QStringLiteral("There is no layer to paint on."));
        return;
    }
    if (layer->locked) {
        host_->reportError(
            QStringLiteral("The layer \"%1\" is locked.").arg(QString::fromStdString(layer->name)));
        return;
    }
    brush::BrushSettings settings = settings_->brush;
    settings.mode = mode_;
    if (const ColorState *colors = host_->colors()) {
        const QColor color = colors->foreground();
        settings.color = {static_cast<float>(color.redF()), static_cast<float>(color.greenF()),
                          static_cast<float>(color.blueF())};
    }
    // Pointer devices without pressure paint at the full size (spec §16.1 PNT-03 fallback).
    if (!sample.hasPressure) {
        settings.pressureSize = false;
    }
    session_ = std::make_unique<brush::StrokeSession>(state, layer->id, settings);
    extend(sample);
}

void BrushTool::extend(const input::PointerSample &sample) {
    const brush::StrokeSample stroke = toStrokeSample(sample);
    session_->add(std::span(&stroke, 1));
    if (!session_->status()) {
        // A tile could not be read back from the recovery journal: nothing more of this stroke can be shown.
        host_->reportError(QString::fromStdString(session_->status().error().message));
        cancel();
        return;
    }
    host_->showPreview(session_->preview());
}

void BrushTool::commit() {
    if (!session_) {
        return;
    }
    std::unique_ptr<brush::StrokeSession> session = std::move(session_);
    auto envelope = commands::CommandEnvelope::make(
        "archmage.raster.paintStroke", session->commandParameters(), {session->layer().toString()});
    // The command applies to whatever revision is current when it runs: commands run in order, so a stroke
    // started on a preview of a still-pending stroke lands on top of it.
    host_->session().submit(std::move(envelope));
}

void BrushTool::cancel() {
    if (session_) {
        session_.reset();
        host_->clearPreview();
    }
}

} // namespace archmage::ui
