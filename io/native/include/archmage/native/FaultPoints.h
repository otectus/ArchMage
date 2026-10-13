// Fault injection for the durability paths (D-27, ADR-0027; spec §10.4: "Test process termination during
// tile writes, transaction commit, backup, checkpoint, rename, and compaction").
//
// Compiled in when ARCHMAGE_FAULT_INJECTION is on (Debug and test presets; off in dev-release). A fault is
// armed from the environment variable ARCHMAGE_FAULT, read once at first use, or in-process with
// FaultInjector::arm(). Grammar:
//
//   <point>[:<n>]          terminate the process with _exit(137) at the n-th hit of <point> (default 1)
//   error@<point>[:<n>]    return an Internal error there instead
//   enospc@<point>[:<n>]   return a ResourceExhausted "not enough disk space" error there instead
//
// An armed fault fires once and disarms. Hits are counted per point whether or not a fault is armed, so a
// test can assert that a scenario reached a point.
//
// Thread safety: all members are safe from any thread.
#pragma once

#include "archmage/base/Error.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string_view>

namespace archmage::native {

enum class FaultPoint : std::uint8_t {
    TileWrite,    // a tile plane row is about to be written (save and journal)
    TxCommit,     // a write transaction is about to commit (save and journal)
    Backup,       // a database copy, or the save fallback moving the previous file to <name>.bak
    Checkpoint,   // the journal's WAL is about to be checkpointed
    Rename,       // the saved temporary file is about to replace the target
    Compaction,   // the journal is about to drop blobs nothing references
    JournalWrite, // the journal is about to write a revision
    DirSync,      // the target's directory is about to be synchronized after the rename
};

inline constexpr std::array kFaultPoints{FaultPoint::TileWrite,  FaultPoint::TxCommit,   FaultPoint::Backup,
                                         FaultPoint::Checkpoint, FaultPoint::Rename,     FaultPoint::Compaction,
                                         FaultPoint::JournalWrite, FaultPoint::DirSync};

[[nodiscard]] std::string_view faultPointName(FaultPoint point);
[[nodiscard]] std::optional<FaultPoint> faultPointFromName(std::string_view name);

enum class FaultMode : std::uint8_t { Abort, Error, Enospc };

struct FaultSpec {
    FaultPoint point = FaultPoint::TileWrite;
    FaultMode mode = FaultMode::Abort;
    std::uint64_t hit = 1; // fire at this hit, counted from arming
};

/// Parses the ARCHMAGE_FAULT grammar above.
[[nodiscard]] std::optional<FaultSpec> parseFaultSpec(std::string_view text);

class FaultInjector {
public:
#if defined(ARCHMAGE_FAULT_INJECTION)
    static constexpr bool kEnabled = true;
#else
    static constexpr bool kEnabled = false;
#endif

    /// The process-wide injector, armed from ARCHMAGE_FAULT when that is set and valid.
    static FaultInjector &global();

    void arm(FaultSpec spec);
    void disarm();
    [[nodiscard]] bool armed() const;
    [[nodiscard]] std::uint64_t hits(FaultPoint point) const;
    void resetHits();

    /// Counts a hit of `point` and fires the armed fault when it is due.
    [[nodiscard]] base::Status check(FaultPoint point);

private:
    FaultInjector();

    mutable std::mutex mutex_;
    std::optional<FaultSpec> armed_;
    std::uint64_t armedHits_ = 0;
    std::array<std::atomic<std::uint64_t>, kFaultPoints.size()> hits_{};
};

/// The hook placed in durability code: a no-op unless fault injection is compiled in.
[[nodiscard]] inline base::Status faultPoint(FaultPoint point) {
    if constexpr (FaultInjector::kEnabled) {
        return FaultInjector::global().check(point);
    } else {
        return {};
    }
}

} // namespace archmage::native
