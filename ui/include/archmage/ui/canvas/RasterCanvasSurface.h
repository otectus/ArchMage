#pragma once

#include "archmage/ui/canvas/CanvasSurface.h"

#include <QPointer>

namespace archmage::ui::canvas {

/// Canvas presented by QWidget painting: the CPU frame is drawn into the widget's backing store.
/// Always available; the safe-mode and device-loss fallback (D-11, spec §26.2, §7.5).
class RasterCanvasSurface final : public CanvasSurface {
    Q_OBJECT

public:
    explicit RasterCanvasSurface(QObject *parent = nullptr);
    ~RasterCanvasSurface() override;

    QWidget *widget() override;
    [[nodiscard]] Kind kind() const override { return Kind::Raster; }
    [[nodiscard]] QString backendDescription() const override;
    void requestFrame() override;

private:
    class Widget;
    friend class Widget;

    QPointer<QWidget> widget_;
};

} // namespace archmage::ui::canvas
