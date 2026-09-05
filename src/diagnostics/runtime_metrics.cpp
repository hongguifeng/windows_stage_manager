#include "diagnostics/runtime_metrics.h"

namespace stage_manager::diagnostics {
namespace {

void update_maximum(std::atomic<std::uint64_t>& target, std::uint64_t value) noexcept
{
    auto current = target.load(std::memory_order_relaxed);
    while (current < value &&
           !target.compare_exchange_weak(
               current, value, std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

} // namespace

void RuntimeMetrics::record(const BatchObservation& observation) noexcept
{
    batches_.fetch_add(1, std::memory_order_relaxed);
    input_events_.fetch_add(observation.inputEvents, std::memory_order_relaxed);
    coalesced_location_events_.fetch_add(
        observation.coalescedLocationEvents, std::memory_order_relaxed);
    solver_states_.fetch_add(observation.solverStates, std::memory_order_relaxed);
    planned_moves_.fetch_add(observation.plannedMoves, std::memory_order_relaxed);
    applied_moves_.fetch_add(observation.appliedMoves, std::memory_order_relaxed);
    dropped_events_.fetch_add(observation.droppedEvents, std::memory_order_relaxed);
    reconciliation_batches_.fetch_add(
        observation.reconciliation ? 1 : 0, std::memory_order_relaxed);
    dry_run_batches_.fetch_add(observation.dryRun ? 1 : 0, std::memory_order_relaxed);
    solver_failures_.fetch_add(observation.solverFailure ? 1 : 0,
                               std::memory_order_relaxed);
    api_failures_.fetch_add(observation.apiFailure ? 1 : 0,
                            std::memory_order_relaxed);
    update_maximum(maximum_queue_depth_, observation.queueDepth);
    update_maximum(maximum_batch_duration_us_, observation.durationUs);
}

RuntimeMetricsSnapshot RuntimeMetrics::snapshot() const noexcept
{
    RuntimeMetricsSnapshot result;
    result.batches = batches_.load(std::memory_order_relaxed);
    result.inputEvents = input_events_.load(std::memory_order_relaxed);
    result.coalescedLocationEvents =
        coalesced_location_events_.load(std::memory_order_relaxed);
    result.solverStates = solver_states_.load(std::memory_order_relaxed);
    result.plannedMoves = planned_moves_.load(std::memory_order_relaxed);
    result.appliedMoves = applied_moves_.load(std::memory_order_relaxed);
    result.droppedEvents = dropped_events_.load(std::memory_order_relaxed);
    result.reconciliationBatches = reconciliation_batches_.load(std::memory_order_relaxed);
    result.dryRunBatches = dry_run_batches_.load(std::memory_order_relaxed);
    result.solverFailures = solver_failures_.load(std::memory_order_relaxed);
    result.apiFailures = api_failures_.load(std::memory_order_relaxed);
    result.maximumQueueDepth = maximum_queue_depth_.load(std::memory_order_relaxed);
    result.maximumBatchDurationUs =
        maximum_batch_duration_us_.load(std::memory_order_relaxed);
    return result;
}

} // namespace stage_manager::diagnostics
