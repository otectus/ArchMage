#pragma once

#include "archmage/render/ViewTransform.h"
#include "archmage/ui/tools/Tool.h"

namespace archmage::ui {

/// Hand (H, and Space held over the canvas): dragging pans the view (plan S1-7.5, NAV-02).
class HandTool final : public Tool {
public:
    [[nodiscard]] QString id() const override { return QString::fromLatin1(tool_ids::kHand); }
    bool pointer(const input::PointerSample &sample) override;
    void cancel() override { endDrag(); }
    void commit() override { endDrag(); }
    [[nodiscard]] bool isInteracting() const override { return dragging_; }
    [[nodiscard]] QCursor cursor() const override { return Qt::OpenHandCursor; }

private:
    void endDrag();

    bool dragging_ = false;
    base::DPoint last_;
};

/// Zoom (Z; plan S1-7.5, NAV-01): a click zooms in 2× about the pointer, Alt-click zooms out; a horizontal
/// drag zooms continuously about the press point (scrub zoom, 2× per 120 logical pixels, right is in).
class ZoomTool final : public Tool {
public:
    [[nodiscard]] QString id() const override { return QString::fromLatin1(tool_ids::kZoom); }
    bool pointer(const input::PointerSample &sample) override;
    void cancel() override;
    void commit() override { pressed_ = false; }
    [[nodiscard]] bool isInteracting() const override { return pressed_; }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

private:
    bool pressed_ = false;
    bool scrubbing_ = false;
    bool zoomOut_ = false;
    base::DPoint anchor_;
    render::ViewTransform startView_;
};

/// Eyedropper (I; plan S1-7.4, NAV-05 subset): pressing or dragging samples the visible composite at the
/// pointer, a single pixel, and makes it the foreground color. Transparent pixels are not sampled.
class EyedropperTool final : public Tool {
public:
    [[nodiscard]] QString id() const override { return QString::fromLatin1(tool_ids::kEyedropper); }
    bool pointer(const input::PointerSample &sample) override;
    void cancel() override { pressed_ = false; }
    void commit() override { pressed_ = false; }
    [[nodiscard]] bool isInteracting() const override { return pressed_; }
    [[nodiscard]] QCursor cursor() const override { return Qt::CrossCursor; }

private:
    void sample(base::DPoint document);

    bool pressed_ = false;
};

} // namespace archmage::ui
