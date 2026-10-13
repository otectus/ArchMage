#include "archmage/ui/canvas/VulkanSupport.h"

#include <QVulkanInstance>
#include <QtGui/private/qvulkandefaultinstance_p.h>

#include <atomic>
#include <cstdio>
#include <memory>

namespace archmage::ui::canvas {
namespace {

constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

std::unique_ptr<QVulkanInstance> &storage() {
    static std::unique_ptr<QVulkanInstance> instance;
    return instance;
}

std::atomic<int> &messageCount() {
    static std::atomic<int> count{0};
    return count;
}

bool &validationActiveFlag() {
    static bool active = false;
    return active;
}

const char *severityName(QVulkanInstance::DebugMessageSeverityFlags severity) {
    if (severity & QVulkanInstance::ErrorSeverity) {
        return "error";
    }
    if (severity & QVulkanInstance::WarningSeverity) {
        return "warning";
    }
    return "info";
}

} // namespace

bool VulkanSupport::validationRequested() {
    return qEnvironmentVariableIntValue("ARCHMAGE_VK_VALIDATION") == 1;
}

void VulkanSupport::configureDefaultInstance() {
    if (validationRequested()) {
        QVulkanDefaultInstance::setFlag(QVulkanDefaultInstance::EnableValidation, true);
    }
}

QVulkanInstance *VulkanSupport::instance(QString *error) {
    auto &instance = storage();
    if (instance) {
        return instance.get();
    }
    auto created = std::make_unique<QVulkanInstance>();
    if (validationRequested()) {
        bool available = false;
        for (const QVulkanLayer &layer : created->supportedLayers()) {
            available = available || layer.name == kValidationLayer;
        }
        if (available) {
            created->setLayers({kValidationLayer});
            created->installDebugOutputFilter([](QVulkanInstance::DebugMessageSeverityFlags severity,
                                                 QVulkanInstance::DebugMessageTypeFlags /*type*/,
                                                 const void *message) {
                if (!(severity & (QVulkanInstance::WarningSeverity | QVulkanInstance::ErrorSeverity))) {
                    return true;
                }
                ++messageCount();
                const auto *data = static_cast<const VkDebugUtilsMessengerCallbackDataEXT *>(message);
                std::printf("VULKAN-VALIDATION severity=%s %s\n", severityName(severity),
                            data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no message)");
                std::fflush(stdout);
                return true;
            });
            validationActiveFlag() = true;
        } else {
            std::printf("VULKAN-VALIDATION layer %s is not installed; running without validation\n",
                        kValidationLayer);
            std::fflush(stdout);
        }
    }
    // An explicit API version (the loader's) keeps the validation layer from treating the instance as
    // version 0. VK_EXT_swapchain_colorspace is enabled when present because some drivers report extended
    // color spaces regardless, and QVulkanWindow then picks one (it offers no color space selection).
    created->setApiVersion(created->supportedApiVersion());
    for (const QVulkanExtension &extension : created->supportedExtensions()) {
        if (extension.name == "VK_EXT_swapchain_colorspace") {
            created->setExtensions({extension.name});
        }
    }
    if (!created->create()) {
        if (error != nullptr) {
            *error = QStringLiteral("vkCreateInstance failed with VkResult %1").arg(created->errorCode());
        }
        return nullptr;
    }
    instance = std::move(created);
    return instance.get();
}

bool VulkanSupport::validationActive() { return validationActiveFlag(); }

int VulkanSupport::validationMessageCount() { return messageCount().load(); }

void VulkanSupport::shutdown() {
    storage().reset();
    // Qt's default instance (used by QRhiWidget) is otherwise destroyed with QGuiApplication; releasing it
    // here lets teardown validation messages be counted before the process reports its results.
    if (QVulkanDefaultInstance::hasInstance()) {
        QVulkanDefaultInstance::cleanup();
    }
}

} // namespace archmage::ui::canvas
