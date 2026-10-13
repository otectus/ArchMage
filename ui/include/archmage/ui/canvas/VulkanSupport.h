#pragma once

#include <QString>

class QVulkanInstance;

namespace archmage::ui::canvas {

/// Process-wide Vulkan instance for canvas surfaces (S1-1, D-11).
///
/// ARCHMAGE_VK_VALIDATION=1 enables VK_LAYER_KHRONOS_validation when it is installed, both on this
/// instance and on the default instance Qt creates for QRhiWidget. Messages of warning or error
/// severity from this instance are counted and printed as "VULKAN-VALIDATION ..." lines on stdout.
///
/// Ownership: the instance lives until shutdown(), which must run after every window using it is
/// destroyed and before QGuiApplication is. Thread safety: GUI thread only.
class VulkanSupport {
public:
    /// Applies the validation request to Qt's default Vulkan instance; call before creating widgets.
    static void configureDefaultInstance();
    /// The shared instance, created on first use; nullptr with `error` set when Vulkan is unavailable.
    static QVulkanInstance *instance(QString *error);
    [[nodiscard]] static bool validationRequested();
    [[nodiscard]] static bool validationActive();
    [[nodiscard]] static int validationMessageCount();
    static void shutdown();
};

} // namespace archmage::ui::canvas
