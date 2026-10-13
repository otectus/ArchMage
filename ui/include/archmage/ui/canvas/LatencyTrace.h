#pragma once

#include <cstdint>
#include <optional>
#include <vector>

namespace archmage::ui::canvas {

/// Event-to-submission latency of canvas frames (S1-1.6, spec §25.2 brush budget).
///
/// Timeline per frame: input receipt (markInput), sample enqueue (markEnqueued), render start
/// (markRenderStart) and submission to the platform (markSubmitted). A frame's latency is measured
/// from the earliest input received since the previous submission, so coalesced events count from
/// the oldest one. Submission is the hand-off point each surface can observe (end of paintEvent,
/// QRhiWidget::frameSubmitted, QVulkanWindow::frameReady), not photon time.
///
/// Ownership and thread safety: a plain value owned by one surface; GUI thread only.
class LatencyTrace {
public:
    struct Stats {
        int frames = 0;
        double p50Ms = 0.0;
        double p95Ms = 0.0;
        double maxMs = 0.0;
        double renderP50Ms = 0.0; // render start to submission
        double renderP95Ms = 0.0;
    };

    static std::int64_t nowNs();
    /// True when the ARCHMAGE_TRACE environment variable lists "latency".
    static bool enabledByEnvironment();

    void markInput(std::int64_t receiptNs);
    void markEnqueued(std::int64_t ns);
    void markRenderStart(std::int64_t ns);
    void markSubmitted(std::int64_t ns);

    /// Statistics of the frames submitted since the previous call; nullopt when there were none.
    std::optional<Stats> takeWindow();
    /// Statistics of every frame recorded since construction or reset().
    [[nodiscard]] Stats total() const;
    void reset();

private:
    static Stats summarize(const std::vector<double> &latency, const std::vector<double> &render);

    std::optional<std::int64_t> pendingInputNs_;
    std::optional<std::int64_t> renderStartNs_;
    std::vector<double> windowLatency_;
    std::vector<double> windowRender_;
    std::vector<double> allLatency_;
    std::vector<double> allRender_;
};

} // namespace archmage::ui::canvas
