#include "archmage/ui/canvas/LatencyTrace.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <string_view>

namespace archmage::ui::canvas {
namespace {

double percentile(std::vector<double> values, double p) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    const auto rank = static_cast<std::size_t>(p * static_cast<double>(values.size() - 1) + 0.5);
    return values[std::min(rank, values.size() - 1)];
}

} // namespace

std::int64_t LatencyTrace::nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

bool LatencyTrace::enabledByEnvironment() {
    const char *value = std::getenv("ARCHMAGE_TRACE");
    return value != nullptr && std::string_view{value}.find("latency") != std::string_view::npos;
}

void LatencyTrace::markInput(std::int64_t receiptNs) {
    if (!pendingInputNs_) {
        pendingInputNs_ = receiptNs;
    }
}

void LatencyTrace::markEnqueued(std::int64_t /*ns*/) {
    // Enqueue happens synchronously after receipt in the S1-1 input path; the timestamp is accepted so
    // the brush pipeline (S1-3) can report queueing delay without an API change.
}

void LatencyTrace::markRenderStart(std::int64_t ns) { renderStartNs_ = ns; }

void LatencyTrace::markSubmitted(std::int64_t ns) {
    if (renderStartNs_) {
        const double render = static_cast<double>(ns - *renderStartNs_) / 1.0e6;
        windowRender_.push_back(render);
        allRender_.push_back(render);
        renderStartNs_.reset();
    }
    if (pendingInputNs_) {
        const double latency = static_cast<double>(ns - *pendingInputNs_) / 1.0e6;
        windowLatency_.push_back(latency);
        allLatency_.push_back(latency);
        pendingInputNs_.reset();
    }
}

LatencyTrace::Stats LatencyTrace::summarize(const std::vector<double> &latency,
                                            const std::vector<double> &render) {
    Stats s;
    s.frames = static_cast<int>(latency.size());
    s.p50Ms = percentile(latency, 0.50);
    s.p95Ms = percentile(latency, 0.95);
    s.maxMs = latency.empty() ? 0.0 : *std::max_element(latency.begin(), latency.end());
    s.renderP50Ms = percentile(render, 0.50);
    s.renderP95Ms = percentile(render, 0.95);
    return s;
}

std::optional<LatencyTrace::Stats> LatencyTrace::takeWindow() {
    if (windowLatency_.empty()) {
        windowRender_.clear();
        return std::nullopt;
    }
    Stats s = summarize(windowLatency_, windowRender_);
    windowLatency_.clear();
    windowRender_.clear();
    return s;
}

LatencyTrace::Stats LatencyTrace::total() const { return summarize(allLatency_, allRender_); }

void LatencyTrace::reset() {
    pendingInputNs_.reset();
    renderStartNs_.reset();
    windowLatency_.clear();
    windowRender_.clear();
    allLatency_.clear();
    allRender_.clear();
}

} // namespace archmage::ui::canvas
