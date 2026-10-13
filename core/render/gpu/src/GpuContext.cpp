#include "archmage/gpu/GpuContext.h"

#include "GpuImpl.h"

#include "archmage/tiles/ResourceCoordinator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace archmage::gpu {
namespace {

constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

std::atomic<std::uint64_t> gTotalValidationErrors{0};

base::Unexpected<base::Error> unavailable(std::string detail) {
    return base::fail(base::ErrorCategory::ProviderUnavailable, "gpu.unavailable",
                      "GPU evaluation is not available; the CPU is used", std::move(detail));
}

bool environmentFlag(const char *name, bool fallback) {
    const char *value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string_view(value) == "1";
}

VKAPI_ATTR VkBool32 VKAPI_CALL onValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT,
                                                   const VkDebugUtilsMessengerCallbackDataEXT *data, void *user) {
    auto &impl = *static_cast<GpuContext::Impl *>(user);
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        impl.validationErrors.fetch_add(1);
        gTotalValidationErrors.fetch_add(1);
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        impl.validationWarnings.fetch_add(1);
    } else {
        return VK_FALSE;
    }
    const std::lock_guard lock(impl.logMutex);
    if (impl.log.size() < 16) {
        impl.log.emplace_back(data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no message)");
    }
    return VK_FALSE;
}

bool hasLayer(const char *name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return std::any_of(layers.begin(), layers.end(),
                       [&](const VkLayerProperties &layer) { return std::strcmp(layer.layerName, name) == 0; });
}

bool hasInstanceExtension(const char *name) {
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(), [&](const VkExtensionProperties &extension) {
        return std::strcmp(extension.extensionName, name) == 0;
    });
}

} // namespace

GpuContext::Impl::~Impl() {
    if (device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device);
        if (commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device, commandPool, nullptr);
        }
        vkDestroyDevice(device, nullptr);
    }
    if (messenger != VK_NULL_HANDLE) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy != nullptr) {
            destroy(instance, messenger, nullptr);
        }
    }
    if (instance != VK_NULL_HANDLE) {
        vkDestroyInstance(instance, nullptr);
    }
    if (info.vramBudget > 0) {
        tiles::ResourceCoordinator::global().setLimit(tiles::Budget::Vram, 0);
    }
}

std::uint32_t GpuContext::Impl::memoryType(std::uint32_t allowed, VkMemoryPropertyFlags required) const {
    for (std::uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i) {
        if ((allowed & (1u << i)) != 0 &&
            (memoryProperties.memoryTypes[i].propertyFlags & required) == required) {
            return i;
        }
    }
    return UINT32_MAX;
}

bool GpuContext::defaultValidation() {
#if defined(ARCHMAGE_GPU_VALIDATION_DEFAULT)
    return environmentFlag("ARCHMAGE_VK_VALIDATION", true);
#else
    return environmentFlag("ARCHMAGE_VK_VALIDATION", false);
#endif
}

GpuContext::GpuContext(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
GpuContext::~GpuContext() = default;

base::Result<std::shared_ptr<GpuContext>> GpuContext::create(Options options) {
    if (environmentFlag("ARCHMAGE_FORCE_CPU", false)) {
        return unavailable("ARCHMAGE_FORCE_CPU=1");
    }
    auto impl = std::make_unique<Impl>();

    const bool validation = options.validation && hasLayer(kValidationLayer) &&
                            hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "ArchMage";
    application.pEngineName = "ArchMage GPU slice";
    application.apiVersion = VK_API_VERSION_1_1;
    std::vector<const char *> layers;
    std::vector<const char *> extensions;
    if (validation) {
        layers.push_back(kValidationLayer);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
    messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    messengerInfo.messageSeverity =
        VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    messengerInfo.pfnUserCallback = onValidationMessage;
    messengerInfo.pUserData = impl.get();
    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &application;
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();
    instanceInfo.pNext = validation ? &messengerInfo : nullptr; // also covers instance creation itself
    if (const VkResult result = vkCreateInstance(&instanceInfo, nullptr, &impl->instance); result != VK_SUCCESS) {
        return unavailable("vkCreateInstance failed: " + std::to_string(result));
    }
    if (validation) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(impl->instance, "vkCreateDebugUtilsMessengerEXT"));
        if (create != nullptr) {
            create(impl->instance, &messengerInfo, nullptr, &impl->messenger);
        }
        impl->validation = impl->messenger != VK_NULL_HANDLE;
    }

    // A device with a compute queue, discrete first.
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(impl->instance, &count, nullptr);
    std::vector<VkPhysicalDevice> devices(count);
    vkEnumeratePhysicalDevices(impl->instance, &count, devices.data());
    int bestScore = -1;
    for (VkPhysicalDevice device : devices) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.apiVersion < VK_API_VERSION_1_1) {
            continue;
        }
        std::uint32_t families = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &families, nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &families, queues.data());
        for (std::uint32_t family = 0; family < families; ++family) {
            if ((queues[family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0) {
                continue;
            }
            const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 3
                              : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 2
                                                                                                : 1;
            if (score > bestScore) {
                bestScore = score;
                impl->physicalDevice = device;
                impl->queueFamily = family;
            }
            break;
        }
    }
    if (impl->physicalDevice == VK_NULL_HANDLE) {
        return unavailable("no Vulkan 1.1 device with a compute queue");
    }

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(impl->physicalDevice, &properties);
    vkGetPhysicalDeviceMemoryProperties(impl->physicalDevice, &impl->memoryProperties);
    impl->info.name = properties.deviceName;
    impl->info.apiVersion = properties.apiVersion;
    impl->info.discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
    impl->info.driver = "driver " + std::to_string(properties.driverVersion);
    for (std::uint32_t i = 0; i < impl->memoryProperties.memoryHeapCount; ++i) {
        const VkMemoryHeap &heap = impl->memoryProperties.memoryHeaps[i];
        if ((heap.flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0) {
            impl->info.deviceLocalBytes = std::max<std::uint64_t>(impl->info.deviceLocalBytes, heap.size);
        }
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = impl->queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    if (const VkResult result = vkCreateDevice(impl->physicalDevice, &deviceInfo, nullptr, &impl->device);
        result != VK_SUCCESS) {
        return unavailable("vkCreateDevice failed: " + std::to_string(result));
    }
    vkGetDeviceQueue(impl->device, impl->queueFamily, 0, &impl->queue);
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = impl->queueFamily;
    if (const VkResult result = vkCreateCommandPool(impl->device, &poolInfo, nullptr, &impl->commandPool);
        result != VK_SUCCESS) {
        return unavailable("vkCreateCommandPool failed: " + std::to_string(result));
    }

    // Spec §25.4: a conservative VRAM budget, leaving room for the display and other applications.
    impl->info.vramBudget = impl->info.deviceLocalBytes / 2;
    tiles::ResourceCoordinator::global().setLimit(tiles::Budget::Vram, impl->info.vramBudget);
    return std::shared_ptr<GpuContext>(new GpuContext(std::move(impl)));
}

const DeviceInfo &GpuContext::info() const { return impl_->info; }
bool GpuContext::validationEnabled() const { return impl_->validation; }
std::uint64_t GpuContext::validationErrors() const { return impl_->validationErrors.load(); }
std::uint64_t GpuContext::validationWarnings() const { return impl_->validationWarnings.load(); }

std::vector<std::string> GpuContext::validationLog() const {
    const std::lock_guard lock(impl_->logMutex);
    return impl_->log;
}

std::uint64_t GpuContext::totalValidationErrors() { return gTotalValidationErrors.load(); }

bool GpuContext::lost() const { return impl_->lost.load(); }
void GpuContext::markLost() { impl_->lost.store(true); }

} // namespace archmage::gpu
