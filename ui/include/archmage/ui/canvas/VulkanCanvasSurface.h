#pragma once

#include "archmage/ui/canvas/CanvasSurface.h"

#include <QPointer>

#include <memory>

class QVulkanWindow;

namespace archmage::ui::canvas {

class VulkanCanvasWindow;

/// Canvas presented by a QVulkanWindow embedded in the widget tree with QWidget::createWindowContainer
/// (D-11 primary candidate, spec §5.2 "renderer-owned Qt window/surface; Vulkan first"). The window owns
/// its swapchain; each frame uploads the CPU frame through a staging buffer and draws it texel for texel.
class VulkanCanvasSurface final : public CanvasSurface {
    Q_OBJECT

public:
    /// nullptr with `error` set when no Vulkan instance can be created.
    static std::unique_ptr<VulkanCanvasSurface> create(QString *error);
    ~VulkanCanvasSurface() override;

    QWidget *widget() override;
    [[nodiscard]] Kind kind() const override { return Kind::Vulkan; }
    [[nodiscard]] QString backendDescription() const override;
    void requestFrame() override;

    /// The embedded window (input arrives here, not at the container widget).
    [[nodiscard]] QVulkanWindow *vulkanWindow() const;

private:
    VulkanCanvasSurface();

    friend class VulkanCanvasWindow;
    friend class VulkanCanvasRenderer;

    QPointer<VulkanCanvasWindow> window_;
    QPointer<QWidget> container_;
};

} // namespace archmage::ui::canvas
