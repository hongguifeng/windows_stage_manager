#include "window/reconcile_scheduler.h"

#include <algorithm>

namespace stage_manager::window {

ReconcileScheduler::ReconcileScheduler(std::uint64_t interval_ms)
    : interval_ms_(std::max<std::uint64_t>(interval_ms, 1))
{
}

bool ReconcileScheduler::due(std::uint64_t now_ms) const noexcept
{
    return now_ms >= next_due_ms_;
}

WindowEvent ReconcileScheduler::poll(std::uint64_t now_ms)
{
    WindowEvent event;
    event.type = WindowEventType::Reconcile;
    event.timestampMs = now_ms;
    event.sequence = ++sequence_;
    next_due_ms_ = now_ms + interval_ms_;
    return event;
}

void ReconcileScheduler::reset(std::uint64_t now_ms) noexcept
{
    next_due_ms_ = now_ms + interval_ms_;
}

} // namespace stage_manager::window
