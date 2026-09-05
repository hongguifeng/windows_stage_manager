#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace stage_manager::diagnostics {

struct BatchObservation {
    std::uint64_t inputEvents = 0;
    std::uint64_t coalescedLocationEvents = 0;
    std::uint64_t solverStates = 0;
    std::uint64_t plannedMoves = 0;
    std::uint64_t appliedMoves = 0;
    std::uint64_t plannedReorders = 0;
    std::uint64_t appliedReorders = 0;
    std::uint64_t droppedEvents = 0;
    std::uint64_t durationUs = 0;
    std::size_t queueDepth = 0;
    bool reconciliation = false;
    bool dryRun = false;
    bool solverFailure = false;
    bool apiFailure = false;
    bool zOrderFallback = false;
};

struct RuntimeMetricsSnapshot {
    std::uint64_t batches = 0;
    std::uint64_t inputEvents = 0;
    std::uint64_t coalescedLocationEvents = 0;
    std::uint64_t solverStates = 0;
    std::uint64_t plannedMoves = 0;
    std::uint64_t appliedMoves = 0;
    std::uint64_t plannedReorders = 0;
    std::uint64_t appliedReorders = 0;
    std::uint64_t droppedEvents = 0;
    std::uint64_t reconciliationBatches = 0;
    std::uint64_t dryRunBatches = 0;
    std::uint64_t solverFailures = 0;
    std::uint64_t apiFailures = 0;
    std::uint64_t zOrderFallbackBatches = 0;
    std::uint64_t maximumQueueDepth = 0;
    std::uint64_t maximumBatchDurationUs = 0;
};

class RuntimeMetrics final {
public:
    void record(const BatchObservation& observation) noexcept;
    RuntimeMetricsSnapshot snapshot() const noexcept;

private:
    std::atomic<std::uint64_t> batches_{0};
    std::atomic<std::uint64_t> input_events_{0};
    std::atomic<std::uint64_t> coalesced_location_events_{0};
    std::atomic<std::uint64_t> solver_states_{0};
    std::atomic<std::uint64_t> planned_moves_{0};
    std::atomic<std::uint64_t> applied_moves_{0};
    std::atomic<std::uint64_t> planned_reorders_{0};
    std::atomic<std::uint64_t> applied_reorders_{0};
    std::atomic<std::uint64_t> dropped_events_{0};
    std::atomic<std::uint64_t> reconciliation_batches_{0};
    std::atomic<std::uint64_t> dry_run_batches_{0};
    std::atomic<std::uint64_t> solver_failures_{0};
    std::atomic<std::uint64_t> api_failures_{0};
    std::atomic<std::uint64_t> z_order_fallback_batches_{0};
    std::atomic<std::uint64_t> maximum_queue_depth_{0};
    std::atomic<std::uint64_t> maximum_batch_duration_us_{0};
};

} // namespace stage_manager::diagnostics
