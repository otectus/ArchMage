#pragma once

#include "archmage/brush/Stroke.h"
#include "archmage/document/Document.h"
#include "archmage/ui/input/PointerSample.h"

#include <QCursor>
#include <QString>

#include <memory>

namespace archmage::ui {

class ColorState;
class DocumentSession;

namespace canvas {
class CanvasSurface;
}

/// Tool identifiers (also the suffix of the "tool.<id>" actions).
namespace tool_ids {
inline constexpr const char *kBrush = "brush";
inline constexpr const char *kHand = "hand";
inline constexpr const char *kZoom = "zoom";
inline constexpr const char *kEyedropper = "eyedropper";
} // namespace tool_ids

/// Options shared by every document in a window; tools read them when an interaction begins. The brush
/// color is not stored here: it is the foreground color (ColorState) at the start of the stroke.
struct ToolSettings {
    brush::BrushSettings brush;
};

/// What a tool may ask of the view hosting it.
class ToolHost {
public:
    virtual ~ToolHost() = default;
    virtual DocumentSession &session() = 0;
    /// The state on screen: the preview while one is shown, otherwise the current revision.
    [[nodiscard]] virtual std::shared_ptr<const document::DocumentState> displayedState() const = 0;
    /// Shows `state` instead of the current revision until a newer revision arrives or clearPreview().
    virtual void showPreview(std::shared_ptr<const document::DocumentState> state) = 0;
    virtual void clearPreview() = 0;
    virtual void reportError(const QString &message) = 0;
    /// The canvas the tool works on; navigation tools change its view transform.
    virtual canvas::CanvasSurface &canvas() = 0;
    /// The window's colors, or null when the host has none (tests): the brush then paints with its own
    /// settings' color and the eyedropper has nowhere to put a sample.
    [[nodiscard]] virtual ColorState *colors() const = 0;
    /// Replaces the canvas cursor while the tool is active.
    virtual void setCanvasCursor(const QCursor &cursor) = 0;
};

/// The spec §11 tool lifecycle: activate, configure, begin, preview, commit, cancel, suspend, resume,
/// deactivate. Begin, preview and commit are driven by pointer phases (press, move, release).
///
/// Interruption rules (spec §11, §16.3; ADR-0105):
/// - Escape cancels the interaction in progress and Enter commits it.
/// - Losing keyboard focus, losing the pointing device, switching to another document or another tool,
///   and holding Space for the temporary hand all commit what was drawn so far (suspend), so a stroke is
///   never left pressed and work is never discarded silently.
///
/// Thread safety: GUI thread only.
class Tool {
public:
    virtual ~Tool() = default;

    [[nodiscard]] virtual QString id() const = 0;
    virtual void activate(ToolHost &host) {
        host_ = &host;
        host.setCanvasCursor(cursor());
    }
    virtual void deactivate() {
        commit();
        host_ = nullptr;
    }
    /// Returns true when the sample was used.
    virtual bool pointer(const input::PointerSample &sample) = 0;
    virtual void cancel() {}
    virtual void commit() {}
    virtual void suspend() { commit(); }
    virtual void resume() {
        if (host_ != nullptr) {
            host_->setCanvasCursor(cursor());
        }
    }
    [[nodiscard]] virtual bool isInteracting() const { return false; }
    /// The canvas cursor while the tool is active and idle.
    [[nodiscard]] virtual QCursor cursor() const { return Qt::ArrowCursor; }

protected:
    ToolHost *host_ = nullptr;
};

/// Creates the tool with `id` (tool_ids), or null for an unknown id. `settings` must outlive the tool.
[[nodiscard]] std::unique_ptr<Tool> createTool(const QString &id, ToolSettings &settings);

} // namespace archmage::ui
