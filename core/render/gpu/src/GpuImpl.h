// Vulkan handles behind GpuContext (core/render/gpu internal).
#pragma once

#include "archmage/gpu/GpuContext.h"

#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace archmage::gpu {

struct GpuContext::Impl {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    DeviceInfo info;
    bool validation = false;
    std::atomic<std::uint64_t> validationErrors{0};
    std::atomic<std::uint64_t> validationWarnings{0};
    std::mutex logMutex;
    std::vector<std::string> log;
    std::atomic<bool> lost{false};

    Impl() = default;
    Impl(const Impl &) = delete;
    Impl &operator=(const Impl &) = delete;
    ~Impl();

    /// A memory type allowed by `allowed` with every `required` property; UINT32_MAX when none.
    [[nodiscard]] std::uint32_t memoryType(std::uint32_t allowed, VkMemoryPropertyFlags required) const;
};

} // namespace archmage::gpu
