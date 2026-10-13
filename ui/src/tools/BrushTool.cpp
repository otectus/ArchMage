#include "archmage/ui/tools/BrushTool.h"

#include "archmage/commands/Command.h"
#include "archmage/ui/document/DocumentSession.h"

namespace archmage::ui {
namespace {

brush::StrokeSample toStrokeSample(const input::PointerSample &sample) {
    return {sample.posDocument, sample.hasPressure ? static_cast<float>(sample.pressure) : 1.0f,
            sample.timestampNs};
}

} // namespace

BrushTool::BrushTool(brush::PaintMode mode) { settings_.mode = mode; }

QString BrushTool::id() const {
    return settings_.mode == brush::PaintMode::Erase ? QStringLiteral("eraser") : QStringLiteral("brush");
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
    // Pointer devices without pressure paint at the full size (spec §16.1 PNT-03 fallback).
    brush::BrushSettings settings = settings_;
    if (!sample.hasPressure) {
        settings.pressureSize = false;
    }
    session_ = std::make_unique<brush::StrokeSession>(state, layer->id, settings);
    extend(sample);
}

void BrushTool::extend(const input::PointerSample &sample) {
    const brush::StrokeSample stroke = toStrokeSample(sample);
    session_->add(std::span(&stroke, 1));
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
