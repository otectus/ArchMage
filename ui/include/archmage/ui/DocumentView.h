#pragma once

#include "archmage/ui/canvas/CanvasSurface.h"
#include "archmage/ui/canvas/DocumentFrameSource.h"
#include "archmage/ui/tools/Tool.h"

#include <QWidget>

#include <memory>

namespace archmage::ui {

class DocumentSession;

/// One document on screen: the canvas surface (ADR-0011 default adapter), the frame source, navigation
/// and the active tool (plan S1-3.5, S1-7).
///
/// While a tool shows a preview, the view displays it instead of the current revision; the preview is
/// dropped when a newer revision arrives and no interaction is in progress.
///
/// Keys over the canvas (ADR-0105): Escape cancels and Enter commits the interaction in progress; Space
/// held switches to the hand until released (ignored mid-stroke); Tab asks the window to hide or show its
/// panels; Shift+Tab moves focus as usual.
///
/// Thread safety: GUI thread only.
class DocumentView final : public QWidget, public canvas::CanvasInputSink, public ToolHost {
    Q_OBJECT

public:
    explicit DocumentView(DocumentSession &session, QWidget *parent = nullptr);
    ~DocumentView() override;

    DocumentSession &session() override { return session_; }
    [[nodiscard]] std::shared_ptr<const document::DocumentState> displayedState() const override;
    void showPreview(std::shared_ptr<const document::DocumentState> state) override;
    void clearPreview() override;
    void reportError(const QString &message) override;
    canvas::CanvasSurface &canvas() override { return *surface_; }
    [[nodiscard]] ColorState *colors() const override { return colors_; }
    void setCanvasCursor(const QCursor &cursor) override;

    bool canvasEvent(canvas::CanvasSurface &surface, QEvent *event) override;

    void setColors(ColorState *colors) { colors_ = colors; }
    void setTool(std::unique_ptr<Tool> tool);
    [[nodiscard]] Tool *tool() const { return tool_.get(); }
    /// The tool pointer input goes to: the temporary hand while Space is held, otherwise tool().
    [[nodiscard]] Tool *effectiveTool() const { return temporary_ ? temporary_.get() : tool_.get(); }
    [[nodiscard]] bool temporaryHandActive() const { return temporary_ != nullptr; }
    /// Commits any interaction in progress (document switch, Save, Close; ADR-0105).
    void suspendInteraction();

    [[nodiscard]] canvas::CanvasSurface &surface() { return *surface_; }
    [[nodiscard]] canvas::DocumentFrameSource &frameSource() { return frameSource_; }
    void fitToView();
    /// Actual pixels (100 %) about the viewport center.
    void actualPixels();
    /// Multiplies the scale about the viewport center.
    void zoomBy(double factor);

signals:
    void errorReported(const QString &message);
    /// The view transform changed (zoom, pan, device pixel ratio).
    void viewChanged();
    /// The pointer is over document point (x, y) (document pixels, not rounded).
    void pointerMoved(double x, double y);
    void pointerLeft();
    /// Tab pressed over the canvas: hide or show the window's panels.
    void panelsToggleRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onRevisionChanged();
    void refresh();
    void beginTemporaryHand();
    void endTemporaryHand();

    DocumentSession &session_;
    canvas::DocumentFrameSource frameSource_;
    std::unique_ptr<canvas::CanvasSurface> surface_;
    std::unique_ptr<Tool> tool_;
    std::unique_ptr<Tool> temporary_;
    ColorState *colors_ = nullptr;
    std::shared_ptr<const document::DocumentState> preview_;
    std::uint64_t previewBaseRevision_ = 0;
    bool viewInitialized_ = false;
};

} // namespace archmage::ui
