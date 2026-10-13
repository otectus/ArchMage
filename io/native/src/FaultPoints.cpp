#include "archmage/native/FaultPoints.h"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <string>

#include <unistd.h>

namespace archmage::native {

std::string_view faultPointName(FaultPoint point) {
    switch (point) {
    case FaultPoint::TileWrite:
        return "tile_write";
    case FaultPoint::TxCommit:
        return "tx_commit";
    case FaultPoint::Backup:
        return "backup";
    case FaultPoint::Checkpoint:
        return "checkpoint";
    case FaultPoint::Rename:
        return "rename";
    case FaultPoint::Compaction:
        return "compaction";
    case FaultPoint::JournalWrite:
        return "journal_write";
    case FaultPoint::DirSync:
        return "dir_sync";
    }
    return "unknown";
}

std::optional<FaultPoint> faultPointFromName(std::string_view name) {
    for (const FaultPoint point : kFaultPoints) {
        if (faultPointName(point) == name) {
            return point;
        }
    }
    return std::nullopt;
}

std::optional<FaultSpec> parseFaultSpec(std::string_view text) {
    FaultSpec spec;
    if (const auto at = text.find('@'); at != std::string_view::npos) {
        const std::string_view mode = text.substr(0, at);
        if (mode == "error") {
            spec.mode = FaultMode::Error;
        } else if (mode == "enospc") {
            spec.mode = FaultMode::Enospc;
        } else {
            return std::nullopt;
        }
        text.remove_prefix(at + 1);
    }
    if (const auto colon = text.find(':'); colon != std::string_view::npos) {
        const std::string_view count = text.substr(colon + 1);
        std::uint64_t hit = 0;
        const auto [end, ec] = std::from_chars(count.data(), count.data() + count.size(), hit);
        if (ec != std::errc() || end != count.data() + count.size() || hit == 0) {
            return std::nullopt;
        }
        spec.hit = hit;
        text = text.substr(0, colon);
    }
    const auto point = faultPointFromName(text);
    if (!point) {
        return std::nullopt;
    }
    spec.point = *point;
    return spec;
}

FaultInjector &FaultInjector::global() {
    static FaultInjector injector;
    return injector;
}

FaultInjector::FaultInjector() {
    if (const char *text = std::getenv("ARCHMAGE_FAULT"); text != nullptr && *text != '\0') {
        if (const auto spec = parseFaultSpec(text)) {
            armed_ = spec;
        } else {
            std::fprintf(stderr, "archmage: ignoring invalid ARCHMAGE_FAULT '%s'\n", text);
        }
    }
}

void FaultInjector::arm(FaultSpec spec) {
    const std::lock_guard lock(mutex_);
    armed_ = spec;
    armedHits_ = 0;
}

void FaultInjector::disarm() {
    const std::lock_guard lock(mutex_);
    armed_.reset();
    armedHits_ = 0;
}

bool FaultInjector::armed() const {
    const std::lock_guard lock(mutex_);
    return armed_.has_value();
}

std::uint64_t FaultInjector::hits(FaultPoint point) const {
    return hits_[static_cast<std::size_t>(point)].load(std::memory_order_relaxed);
}

void FaultInjector::resetHits() {
    for (auto &count : hits_) {
        count.store(0, std::memory_order_relaxed);
    }
}

base::Status FaultInjector::check(FaultPoint point) {
    hits_[static_cast<std::size_t>(point)].fetch_add(1, std::memory_order_relaxed);
    FaultSpec fired;
    {
        const std::lock_guard lock(mutex_);
        if (!armed_ || armed_->point != point || ++armedHits_ < armed_->hit) {
            return {};
        }
        fired = *armed_;
        armed_.reset();
    }
    const std::string where(faultPointName(point));
    switch (fired.mode) {
    case FaultMode::Abort:
        // Like a crash or SIGKILL: no destructors, no atexit handlers, no buffered output.
        _exit(137);
    case FaultMode::Error:
        return base::fail(base::ErrorCategory::Internal, "fault.injected", "An injected fault stopped the operation",
                          "fault point " + where);
    case FaultMode::Enospc:
        return base::fail(base::ErrorCategory::ResourceExhausted, "io.enospc", "There is not enough disk space",
                          "fault point " + where + ": injected ENOSPC");
    }
    return {};
}

} // namespace archmage::native
