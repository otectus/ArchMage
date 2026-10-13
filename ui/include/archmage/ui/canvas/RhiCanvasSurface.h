#pragma once

#include "archmage/ui/canvas/CanvasSurface.h"

#include <QPointer>

namespace archmage::ui::canvas {

/// Canvas presented through QRhiWidget: the CPU frame is uploaded to a texture and drawn with QRhi into
/// the widget's render target, which Qt composites with the rest of the window (D-11 candidate).
///
/// The graphics API comes from ARCHMAGE_RHI_API ("vulkan", the default, or "opengl"). QRhi is offered
/// with limited compatibility guarantees and its headers ship with Qt's private Gui module.
class RhiCanvasSurface final : public CanvasSurface {
    Q_OBJECT

public:
    explicit RhiCanvasSurface(QObject *parent = nullptr);
    ~RhiCanvasSurface() override;

    QWidget *widget() override;
    [[nodiscard]] Kind kind() const override { return Kind::Rhi; }
    [[nodiscard]] QString backendDescription() const override;
    void requestFrame() override;

private:
    class Widget;
    friend class Widget;

    QPointer<QWidget> widget_;
};

} // namespace archmage::ui::canvas
