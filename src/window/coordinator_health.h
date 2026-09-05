#pragma once

#include "window/mvp_coordinator.h"

#include <cstdint>
#include <mutex>

namespace stage_manager::window {

enum class HealthAction : std::uint8_t {
    Continue,
    DisableAutomation,
};

struct CoordinatorHealthState {
    std::uint32_t consecutiveFailures = 0;
    bool tripped = false;
};

class CoordinatorHealthMonitor final {
public:
    explicit CoordinatorHealthMonitor(std::uint32_t failure_threshold = 3);

    void set_failure_threshold(std::uint32_t failure_threshold);
    HealthAction observe(MvpBatchStatus status, MvpSuspendReason reason);
    void reset();
    CoordinatorHealthState state() const;

private:
    std::uint32_t failure_threshold_ = 3;
    CoordinatorHealthState state_;
    mutable std::mutex mutex_;
};

} // namespace stage_manager::window
