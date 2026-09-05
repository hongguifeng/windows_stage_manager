#include "diagnostics/runtime_metrics.h"

#include <cstdio>
#include <thread>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::diagnostics::BatchObservation;
    using stage_manager::diagnostics::RuntimeMetrics;

    RuntimeMetrics metrics;
    constexpr std::uint64_t kThreads = 4;
    constexpr std::uint64_t kBatchesPerThread = 1'000;
    std::vector<std::thread> writers;
    for (std::uint64_t thread_index = 0; thread_index < kThreads; ++thread_index) {
        writers.emplace_back([thread_index, &metrics] {
            for (std::uint64_t batch = 0; batch < kBatchesPerThread; ++batch) {
                BatchObservation observation;
                observation.inputEvents = 10;
                observation.coalescedLocationEvents = 2;
                observation.solverStates = 3;
                observation.plannedMoves = 2;
                observation.appliedMoves = 1;
                observation.plannedReorders = 1;
                observation.appliedReorders = batch % 2;
                observation.droppedEvents = batch % 10 == 0 ? 1 : 0;
                observation.durationUs = thread_index * kBatchesPerThread + batch;
                observation.queueDepth = static_cast<std::size_t>(thread_index + 4);
                observation.reconciliation = batch % 10 == 0;
                observation.dryRun = batch % 2 == 0;
                observation.solverFailure = batch % 20 == 0;
                observation.apiFailure = batch % 100 == 0;
                observation.zOrderFallback = batch % 4 == 0;
                metrics.record(observation);
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    const auto result = metrics.snapshot();
    const auto batches = kThreads * kBatchesPerThread;
    CHECK(result.batches == batches);
    CHECK(result.inputEvents == batches * 10);
    CHECK(result.coalescedLocationEvents == batches * 2);
    CHECK(result.solverStates == batches * 3);
    CHECK(result.plannedMoves == batches * 2);
    CHECK(result.appliedMoves == batches);
    CHECK(result.plannedReorders == batches);
    CHECK(result.appliedReorders == batches / 2);
    CHECK(result.droppedEvents == kThreads * 100);
    CHECK(result.reconciliationBatches == kThreads * 100);
    CHECK(result.dryRunBatches == batches / 2);
    CHECK(result.solverFailures == kThreads * 50);
    CHECK(result.apiFailures == kThreads * 10);
    CHECK(result.zOrderFallbackBatches == batches / 4);
    CHECK(result.maximumQueueDepth == 7);
    CHECK(result.maximumBatchDurationUs == batches - 1);
    return 0;
}
