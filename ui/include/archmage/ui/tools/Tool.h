#pragma once

#include "archmage/document/Document.h"
#include "archmage/ui/input/PointerSample.h"

#include <QString>

#include <memory>

namespace archmage::ui {

class DocumentSession;

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
};

/// The spec §11 tool lifecycle: activate, configure, begin, preview, commit, cancel, suspend, resume,
/// deactivate. Begin, preview and commit are driven by pointer phases (press, move, release).
///
/// Interruption rules (spec §16.3): Escape cancels the interaction; Enter commits it; losing focus or the
/// pointing device, or switching documents, commits what was drawn so far, so a stroke is never left
/// pressed and work is never discarded silently.
///
/// Thread safety: GUI thread only.
class Tool {
public:
    virtual ~Tool() = default;

    [[nodiscard]] virtual QString id() const = 0;
    virtual void activate(ToolHost &host) { host_ = &host; }
    virtual void deactivate() {
        commit();
        host_ = nullptr;
    }
    /// Returns true when the sample was used.
    virtual bool pointer(const input::PointerSample &sample) = 0;
    virtual void cancel() {}
    virtual void commit() {}
    virtual void suspend() { commit(); }
    virtual void resume() {}
    [[nodiscard]] virtual bool isInteracting() const { return false; }

protected:
    ToolHost *host_ = nullptr;
};

} // namespace archmage::ui
