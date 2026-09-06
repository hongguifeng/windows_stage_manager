#include "window/coordinator_health.h"

#include <cstdio>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::window::CoordinatorHealthMonitor;
    using stage_manager::window::HealthAction;
    using stage_manager::window::MvpBatchStatus;
    using stage_manager::window::MvpSuspendReason;

    CoordinatorHealthMonitor monitor(3);
    CHECK(monitor.observe(MvpBatchStatus::ApiError, MvpSuspendReason::ApplyFailure) == HealthAction::Continue);
    for (int i = 0; i < 100; ++i)
        CHECK(monitor.observe(MvpBatchStatus::Rebuilding, MvpSuspendReason::SnapshotStale) == HealthAction::Continue);
    CHECK(monitor.state().consecutiveFailures == 1);
    CHECK(!monitor.state().tripped);
    CHECK(monitor.observe(MvpBatchStatus::Applied, MvpSuspendReason::None) == HealthAction::Continue);
    CHECK(monitor.state().consecutiveFailures == 0);
    CHECK(monitor.observe(MvpBatchStatus::ApiError, MvpSuspendReason::ApplyFailure) ==
          HealthAction::Continue);
    CHECK(monitor.observe(MvpBatchStatus::Rebuilding,
                          MvpSuspendReason::SnapshotUnavailable) ==
          HealthAction::Continue);
    CHECK(monitor.state().consecutiveFailures == 2);
    CHECK(monitor.observe(MvpBatchStatus::ApiError, MvpSuspendReason::ApplyFailure) ==
          HealthAction::DisableAutomation);
    CHECK(monitor.state().tripped);
    CHECK(monitor.observe(MvpBatchStatus::Idle, MvpSuspendReason::None) ==
          HealthAction::DisableAutomation);

    monitor.reset();
    CHECK(!monitor.state().tripped);
    CHECK(monitor.observe(MvpBatchStatus::ApiError, MvpSuspendReason::ApplyFailure) ==
          HealthAction::Continue);
    CHECK(monitor.observe(MvpBatchStatus::Unsatisfiable, MvpSuspendReason::SolverFailure) ==
          HealthAction::Continue);
    CHECK(monitor.state().consecutiveFailures == 0);

    monitor.set_failure_threshold(0);
    CHECK(monitor.observe(MvpBatchStatus::ApiError, MvpSuspendReason::ApplyFailure) ==
          HealthAction::DisableAutomation);
    monitor.set_failure_threshold(1'000);
    for (std::uint32_t index = 0; index < 99; ++index) {
        CHECK(monitor.observe(MvpBatchStatus::ApiError, MvpSuspendReason::ApplyFailure) ==
              HealthAction::Continue);
    }
    CHECK(monitor.observe(MvpBatchStatus::ApiError, MvpSuspendReason::ApplyFailure) ==
          HealthAction::DisableAutomation);
    return 0;
}
