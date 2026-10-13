#include "archmage/tiles/ResourceCoordinator.h"

#include <algorithm>

#if defined(__linux__)
#include <unistd.h>
#endif

namespace archmage::tiles {
namespace {

std::uint64_t physicalMemory() {
#if defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long pageSize = sysconf(_SC_PAGE_SIZE);
    if (pages > 0 && pageSize > 0) {
        return static_cast<std::uint64_t>(pages) * static_cast<std::uint64_t>(pageSize);
    }
#endif
    return 16ull << 30; // profile B's 16 GiB when the platform cannot say (ADR-0021 portability stub)
}

} // namespace

ResourceCoordinator::Limits ResourceCoordinator::defaultLimits() {
    Limits limits;
    limits.ram = physicalMemory() / 2;
    limits.history = limits.ram / 4;
    limits.scratch = 32ull << 30;
    limits.vram = 0;
    return limits;
}

ResourceCoordinator &ResourceCoordinator::global() {
    static ResourceCoordinator coordinator{defaultLimits()};
    return coordinator;
}

ResourceCoordinator::ResourceCoordinator(Limits limits) : limits_(limits) {
    counter(Budget::Ram).limit = limits.ram;
    counter(Budget::Vram).limit = limits.vram;
    counter(Budget::Scratch).limit = limits.scratch;
    counter(Budget::History).limit = limits.history;
}

void ResourceCoordinator::charge(Budget budget, std::uint64_t bytes) noexcept {
    Counter &c = counter(budget);
    const std::uint64_t now = c.used.fetch_add(bytes, std::memory_order_relaxed) + bytes;
    std::uint64_t peak = c.peak.load(std::memory_order_relaxed);
    while (now > peak && !c.peak.compare_exchange_weak(peak, now, std::memory_order_relaxed)) {
    }
}

void ResourceCoordinator::release(Budget budget, std::uint64_t bytes) noexcept {
    counter(budget).used.fetch_sub(bytes, std::memory_order_relaxed);
}

std::uint64_t ResourceCoordinator::used(Budget budget) const noexcept {
    return counter(budget).used.load(std::memory_order_relaxed);
}

std::uint64_t ResourceCoordinator::peak(Budget budget) const noexcept {
    return counter(budget).peak.load(std::memory_order_relaxed);
}

std::uint64_t ResourceCoordinator::limit(Budget budget) const noexcept {
    return counter(budget).limit.load(std::memory_order_relaxed);
}

void ResourceCoordinator::setLimit(Budget budget, std::uint64_t bytes) noexcept {
    counter(budget).limit.store(bytes, std::memory_order_relaxed);
}

ResourceCoordinator::EvictorHandle ResourceCoordinator::addEvictor(Budget budget, EvictionClass evictionClass,
                                                                   Evictor evictor) {
    const std::lock_guard lock(mutex_);
    const EvictorHandle handle = nextHandle_++;
    evictors_.push_back({handle, budget, evictionClass, std::move(evictor)});
    return handle;
}

void ResourceCoordinator::removeEvictor(EvictorHandle handle) {
    const std::lock_guard lock(mutex_);
    std::erase_if(evictors_, [handle](const Registration &r) { return r.handle == handle; });
}

bool ResourceCoordinator::makeRoom(Budget budget, std::uint64_t bytes) {
    return makeRoomWithin(budget, bytes, limit(budget));
}

bool ResourceCoordinator::makeRoomFromReserve(Budget budget, std::uint64_t bytes) {
    const std::uint64_t extra = budget == Budget::Ram ? limits_.reservedPool : 0;
    return makeRoomWithin(budget, bytes, limit(budget) + extra);
}

bool ResourceCoordinator::makeRoomWithin(Budget budget, std::uint64_t bytes, std::uint64_t ceiling) {
    if (bytes > ceiling) {
        return false; // no amount of eviction can help
    }
    const auto fits = [&] { return used(budget) <= ceiling - bytes; };
    if (fits()) {
        return true;
    }
    const std::lock_guard lock(mutex_);
    for (const EvictionClass evictionClass : {EvictionClass::Disposable, EvictionClass::Spoolable}) {
        for (Registration &r : evictors_) {
            if (r.budget != budget || r.evictionClass != evictionClass) {
                continue;
            }
            if (fits()) {
                return true;
            }
            r.evictor(used(budget) - (ceiling - bytes)); // bytes still over the ceiling
        }
    }
    return fits();
}

} // namespace archmage::tiles
