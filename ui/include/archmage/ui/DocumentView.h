#pragma once

#include "archmage/ui/canvas/CanvasSurface.h"
#include "archmage/ui/canvas/DocumentFrameSource.h"
#include "archmage/ui/tools/Tool.h"

#include <QWidget>

#include <memory>

namespace archmage::ui {

class DocumentSession;

/// One document on screen: the canvas surface (ADR-0011 default adapter), the frame source, navigation
/// and the active tool (plan S1-3.5; S1-7 adds tabs, panels and more tools around it).
///
/// While a tool shows a preview, the view displays it instead of the current revision; the preview is
/// dropped when a newer revision arrives and no interaction is in progress.
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

    bool canvasEvent(canvas::CanvasSurface &surface, QEvent *event) override;

    void setTool(std::unique_ptr<Tool> tool);
    [[nodiscard]] Tool *tool() const { return tool_.get(); }
    [[nodiscard]] canvas::CanvasSurface &surface() { return *surface_; }
    [[nodiscard]] canvas::DocumentFrameSource &frameSource() { return frameSource_; }
    void fitToView();

signals:
    void errorReported(const QString &message);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onRevisionChanged();
    void refresh();

    DocumentSession &session_;
    canvas::DocumentFrameSource frameSource_;
    std::unique_ptr<canvas::CanvasSurface> surface_;
    std::unique_ptr<Tool> tool_;
    std::shared_ptr<const document::DocumentState> preview_;
    std::uint64_t previewBaseRevision_ = 0;
    bool viewInitialized_ = false;
};

} // namespace archmage::ui
