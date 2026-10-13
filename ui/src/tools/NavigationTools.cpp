#include "archmage/ui/tools/NavigationTools.h"

#include "archmage/render/CpuEvaluator.h"
#include "archmage/render/Kernels.h"
#include "archmage/ui/ColorState.h"
#include "archmage/ui/canvas/CanvasSurface.h"
#include "archmage/ui/document/DocumentSession.h"
#include "archmage/ui/tools/BrushTool.h"

#include <QColor>

#include <cmath>

namespace archmage::ui {

bool HandTool::pointer(const input::PointerSample &sample) {
    if (host_ == nullptr) {
        return false;
    }
    switch (sample.phase) {
    case input::PointerPhase::Press:
        if (sample.button != Qt::LeftButton) {
            return false;
        }
        dragging_ = true;
        last_ = sample.posDevice;
        host_->setCanvasCursor(Qt::ClosedHandCursor);
        return true;
    case input::PointerPhase::Move:
        if (!dragging_) {
            return false;
        }
        {
            canvas::CanvasSurface &canvas = host_->canvas();
            canvas.setViewTransform(canvas.viewTransform().panned(
                base::DPoint{sample.posDevice.x - last_.x, sample.posDevice.y - last_.y}));
        }
        last_ = sample.posDevice;
        return true;
    case input::PointerPhase::Release:
        if (!dragging_ || sample.button != Qt::LeftButton) {
            return false;
        }
        endDrag();
        return true;
    case input::PointerPhase::Hover:
        return false;
    }
    return false;
}

void HandTool::endDrag() {
    dragging_ = false;
    if (host_ != nullptr) {
        host_->setCanvasCursor(cursor());
    }
}

bool ZoomTool::pointer(const input::PointerSample &sample) {
    if (host_ == nullptr) {
        return false;
    }
    canvas::CanvasSurface &canvas = host_->canvas();
    switch (sample.phase) {
    case input::PointerPhase::Press:
        if (sample.button != Qt::LeftButton) {
            return false;
        }
        pressed_ = true;
        scrubbing_ = false;
        zoomOut_ = sample.modifiers.testFlag(Qt::AltModifier);
        anchor_ = sample.posDevice;
        startView_ = canvas.viewTransform();
        return true;
    case input::PointerPhase::Move: {
        if (!pressed_) {
            return false;
        }
        const double logicalDx = (sample.posDevice.x - anchor_.x) / canvas.devicePixelRatio();
        if (!scrubbing_ && std::abs(logicalDx) > 4.0) {
            scrubbing_ = true;
        }
        if (scrubbing_) {
            canvas.setViewTransform(startView_.zoomedAbout(startView_.scale() * std::exp2(logicalDx / 120.0), anchor_));
        }
        return true;
    }
    case input::PointerPhase::Release:
        if (!pressed_ || sample.button != Qt::LeftButton) {
            return false;
        }
        if (!scrubbing_) {
            canvas.setViewTransform(startView_.zoomedAbout(startView_.scale() * (zoomOut_ ? 0.5 : 2.0), anchor_));
        }
        pressed_ = false;
        return true;
    case input::PointerPhase::Hover:
        return false;
    }
    return false;
}

void ZoomTool::cancel() {
    if (pressed_ && scrubbing_ && host_ != nullptr) {
        host_->canvas().setViewTransform(startView_); // Escape restores the view the drag started from
    }
    pressed_ = false;
}

bool EyedropperTool::pointer(const input::PointerSample &sample) {
    if (host_ == nullptr) {
        return false;
    }
    switch (sample.phase) {
    case input::PointerPhase::Press:
        if (sample.button != Qt::LeftButton) {
            return false;
        }
        pressed_ = true;
        this->sample(sample.posDocument);
        return true;
    case input::PointerPhase::Move:
        if (!pressed_) {
            return false;
        }
        this->sample(sample.posDocument);
        return true;
    case input::PointerPhase::Release:
        if (!pressed_ || sample.button != Qt::LeftButton) {
            return false;
        }
        pressed_ = false;
        return true;
    case input::PointerPhase::Hover:
        return false;
    }
    return false;
}

void EyedropperTool::sample(base::DPoint document) {
    ColorState *colors = host_->colors();
    const auto state = host_->displayedState();
    if (colors == nullptr || !state) {
        return;
    }
    const auto x = static_cast<std::int64_t>(std::floor(document.x));
    const auto y = static_cast<std::int64_t>(std::floor(document.y));
    if (!state->canvasRect().contains(base::IPoint{x, y})) {
        return;
    }
    const auto root = render::compileGraph(*state);
    auto tile = host_->session().evaluator().renderTile(*root, tiles::tileContaining({x, y}),
                                                        render::EvaluationContext{});
    if (!tile) {
        host_->reportError(QString::fromStdString(tile.error().message));
        return;
    }
    float pixel[4];
    const float *source = (*tile)->pixel(x, y);
    std::copy(source, source + 4, pixel);
    if (pixel[3] <= 0.0f) {
        return; // nothing visible to sample
    }
    render::unpremultiplyRow(std::span<float>(pixel, 4));
    const auto channel = [](float v) { return static_cast<double>(std::clamp(v, 0.0f, 1.0f)); };
    colors->setForeground(QColor::fromRgbF(static_cast<float>(channel(pixel[0])), static_cast<float>(channel(pixel[1])),
                                           static_cast<float>(channel(pixel[2]))));
}

std::unique_ptr<Tool> createTool(const QString &id, ToolSettings &settings) {
    if (id == QLatin1String(tool_ids::kBrush)) {
        return std::make_unique<BrushTool>(&settings);
    }
    if (id == QLatin1String(tool_ids::kHand)) {
        return std::make_unique<HandTool>();
    }
    if (id == QLatin1String(tool_ids::kZoom)) {
        return std::make_unique<ZoomTool>();
    }
    if (id == QLatin1String(tool_ids::kEyedropper)) {
        return std::make_unique<EyedropperTool>();
    }
    return nullptr;
}

} // namespace archmage::ui
