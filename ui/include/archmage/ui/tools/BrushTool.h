#pragma once

#include "archmage/brush/Stroke.h"
#include "archmage/ui/tools/Tool.h"

#include <memory>

namespace archmage::ui {

/// Brush and eraser (plan S1-3.5; spec §16.1). A press starts a StrokeSession on the displayed state and
/// the active layer; moves extend it and the host shows its preview; release submits one
/// archmage.raster.paintStroke command, which reproduces the preview exactly.
class BrushTool final : public Tool {
public:
    explicit BrushTool(brush::PaintMode mode = brush::PaintMode::Normal);

    [[nodiscard]] QString id() const override;
    bool pointer(const input::PointerSample &sample) override;
    void cancel() override;
    void commit() override;
    [[nodiscard]] bool isInteracting() const override { return session_ != nullptr; }

    brush::BrushSettings &settings() { return settings_; }

private:
    void begin(const input::PointerSample &sample);
    void extend(const input::PointerSample &sample);

    brush::BrushSettings settings_;
    std::unique_ptr<brush::StrokeSession> session_;
};

} // namespace archmage::ui
