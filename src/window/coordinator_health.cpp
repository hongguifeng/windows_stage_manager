#include "window/coordinator_health.h"

#include <algorithm>
#include <limits>

namespace stage_manager::window {

CoordinatorHealthMonitor::CoordinatorHealthMonitor(std::uint32_t failure_threshold)
{
    set_failure_threshold(failure_threshold);
}

void CoordinatorHealthMonitor::set_failure_threshold(
    std::uint32_t failure_threshold)
{
    const std::scoped_lock lock(mutex_);
    failure_threshold_ = std::clamp(failure_threshold, 1u, 100u);
    state_ = {};
}

HealthAction CoordinatorHealthMonitor::observe(MvpBatchStatus status,
                                               MvpSuspendReason reason)
{
    const std::scoped_lock lock(mutex_);
    if (state_.tripped) {
        return HealthAction::DisableAutomation;
    }
    const bool infrastructure_failure = status == MvpBatchStatus::ApiError ||
        reason == MvpSuspendReason::SnapshotUnavailable;
    if (!infrastructure_failure) {
        state_.consecutiveFailures = 0;
        return HealthAction::Continue;
    }
    if (state_.consecutiveFailures != std::numeric_limits<std::uint32_t>::max()) {
        ++state_.consecutiveFailures;
    }
    if (state_.consecutiveFailures >= failure_threshold_) {
        state_.tripped = true;
        return HealthAction::DisableAutomation;
    }
    return HealthAction::Continue;
}

void CoordinatorHealthMonitor::reset()
{
    const std::scoped_lock lock(mutex_);
    state_ = {};
}

CoordinatorHealthState CoordinatorHealthMonitor::state() const
{
    const std::scoped_lock lock(mutex_);
    return state_;
}

} // namespace stage_manager::window
