// A headless Vulkan compute device for the GPU evaluation slice (D-12, ADR-0012; spec §7.5; plan S1-6.1).
//
// The CPU evaluator stays authoritative in Stage 1; GPU evaluation is opt-in and compared with it. The
// context picks a physical device with a compute queue (a discrete one first), enables the Khronos
// validation layer in Debug builds (ARCHMAGE_VK_VALIDATION=0 or 1 overrides) and counts its messages,
// registers a VRAM budget with ResourceCoordinator::global(), and remembers a lost device so that callers
// fall back to the CPU. ARCHMAGE_FORCE_CPU=1 makes creation fail as if no device existed (profile C).
//
// Thread safety: create() and the queries are thread-safe; submitting work is serialized by GpuEvaluator.
#pragma once

#include "archmage/base/Error.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace archmage::gpu {

struct DeviceInfo {
    std::string name;
    std::string driver;
    std::uint32_t apiVersion = 0;
    bool discrete = false;
    std::uint64_t deviceLocalBytes = 0; // the largest device-local heap
    std::uint64_t vramBudget = 0;       // registered as Budget::Vram
};

class GpuContext {
public:
    struct Options {
        bool validation = defaultValidation();
    };
    /// Debug builds validate unless ARCHMAGE_VK_VALIDATION=0; other builds only with ARCHMAGE_VK_VALIDATION=1.
    [[nodiscard]] static bool defaultValidation();

    /// provider_unavailable ("gpu.unavailable") without a usable device or with ARCHMAGE_FORCE_CPU=1.
    [[nodiscard]] static base::Result<std::shared_ptr<GpuContext>> create(Options options);
    ~GpuContext();
    GpuContext(const GpuContext &) = delete;
    GpuContext &operator=(const GpuContext &) = delete;

    [[nodiscard]] const DeviceInfo &info() const;
    [[nodiscard]] bool validationEnabled() const;
    [[nodiscard]] std::uint64_t validationErrors() const;
    [[nodiscard]] std::uint64_t validationWarnings() const;
    /// The first validation messages, for test failure output.
    [[nodiscard]] std::vector<std::string> validationLog() const;
    /// Validation errors of every context in this process, including those reported while a context was
    /// destroyed.
    [[nodiscard]] static std::uint64_t totalValidationErrors();

    /// True after the device reported VK_ERROR_DEVICE_LOST (or markLost()); nothing more is submitted.
    [[nodiscard]] bool lost() const;
    void markLost();

    struct Impl;
    [[nodiscard]] Impl &impl() const { return *impl_; }

private:
    explicit GpuContext(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace archmage::gpu
