// One coordinator for the RAM, VRAM, scratch and history budgets (D-26, spec §7.1, §25.4).
//
// Memory is charged when allocated and released when freed. When a budget would be exceeded, makeRoom()
// asks registered evictors to free memory: disposable caches (thumbnails, mip levels, render results)
// first, then spoolable authoritative tiles that are already journaled (S1-5). Memory without an evictor,
// such as dirty tiles that are not yet journaled, is never reclaimed. A reserved pool beyond the RAM limit
// stays available for error, save and recovery paths.
//
// Thread safety: charge, release and the queries are lock-free and safe from any thread. Evictor
// registration and makeRoom serialize on an internal mutex; evictors run under it and must not call
// addEvictor, removeEvictor or makeRoom themselves (they may call release).
#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <vector>

namespace archmage::tiles {

enum class Budget : std::uint8_t { Ram, Vram, Scratch, History };
enum class EvictionClass : std::uint8_t { Disposable, Spoolable };

class ResourceCoordinator {
public:
    struct Limits {
        std::uint64_t ram = 0;
        std::uint64_t vram = 0;
        std::uint64_t scratch = 0;
        std::uint64_t history = 0;
        std::uint64_t reservedPool = 64ull << 20; // 64 MiB (D-26)
    };

    /// Frees up to `bytesWanted` (calling release() for what it frees) and returns the bytes freed.
    using Evictor = std::function<std::uint64_t(std::uint64_t bytesWanted)>;
    using EvictorHandle = std::uint64_t;

    /// RAM: half of physical memory (spec §25.4); history: a quarter of RAM; scratch: 32 GiB; VRAM: 0
    /// until a GPU path reports its budget.
    [[nodiscard]] static Limits defaultLimits();
    /// Process-wide coordinator used by tile allocation.
    static ResourceCoordinator &global();

    explicit ResourceCoordinator(Limits limits);

    void charge(Budget budget, std::uint64_t bytes) noexcept;
    void release(Budget budget, std::uint64_t bytes) noexcept;
    [[nodiscard]] std::uint64_t used(Budget budget) const noexcept;
    [[nodiscard]] std::uint64_t peak(Budget budget) const noexcept;
    [[nodiscard]] std::uint64_t limit(Budget budget) const noexcept;
    void setLimit(Budget budget, std::uint64_t bytes) noexcept;
    [[nodiscard]] std::uint64_t reservedPool() const noexcept { return limits_.reservedPool; }

    EvictorHandle addEvictor(Budget budget, EvictionClass evictionClass, Evictor evictor);
    void removeEvictor(EvictorHandle handle);

    /// Ensures `bytes` more fit within the budget's limit, evicting disposable then spoolable entries.
    /// Returns true when the room exists afterwards.
    bool makeRoom(Budget budget, std::uint64_t bytes);
    /// Like makeRoom, but may also use the reserved pool: for saving, recovery and error reporting.
    bool makeRoomFromReserve(Budget budget, std::uint64_t bytes);

private:
    struct Registration {
        EvictorHandle handle;
        Budget budget;
        EvictionClass evictionClass;
        Evictor evictor;
    };
    struct Counter {
        std::atomic<std::uint64_t> used{0};
        std::atomic<std::uint64_t> peak{0};
        std::atomic<std::uint64_t> limit{0};
    };

    bool makeRoomWithin(Budget budget, std::uint64_t bytes, std::uint64_t ceiling);
    Counter &counter(Budget budget) { return counters_[static_cast<std::size_t>(budget)]; }
    const Counter &counter(Budget budget) const { return counters_[static_cast<std::size_t>(budget)]; }

    Limits limits_;
    std::array<Counter, 4> counters_;
    std::mutex mutex_;
    std::vector<Registration> evictors_;
    EvictorHandle nextHandle_ = 1;
};

} // namespace archmage::tiles
