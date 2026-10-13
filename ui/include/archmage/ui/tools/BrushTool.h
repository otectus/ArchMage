#pragma once

#include "archmage/brush/Stroke.h"
#include "archmage/ui/tools/Tool.h"

#include <memory>

namespace archmage::ui {

/// Brush and eraser (plan S1-3.5, S1-7; spec §16.1). A press starts a StrokeSession on the displayed state
/// and the active layer, with the settings current at that moment and the foreground color; moves extend
/// it and the host shows its preview; release submits one archmage.raster.paintStroke command, which
/// reproduces the preview exactly.
class BrushTool final : public Tool {
public:
    /// `settings` is shared with the window's options row; null gives the tool settings of its own.
    explicit BrushTool(ToolSettings *settings = nullptr, brush::PaintMode mode = brush::PaintMode::Normal);

    [[nodiscard]] QString id() const override;
    bool pointer(const input::PointerSample &sample) override;
    void cancel() override;
    void commit() override;
    [[nodiscard]] bool isInteracting() const override { return session_ != nullptr; }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

    brush::BrushSettings &settings() { return settings_->brush; }

private:
    void begin(const input::PointerSample &sample);
    void extend(const input::PointerSample &sample);

    ToolSettings own_;
    ToolSettings *settings_;
    brush::PaintMode mode_;
    std::unique_ptr<brush::StrokeSession> session_;
};

} // namespace archmage::ui
