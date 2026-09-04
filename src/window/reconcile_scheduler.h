#pragma once

#include "window/window_event.h"

#include <cstdint>

namespace stage_manager::window {

class ReconcileScheduler final {
public:
    explicit ReconcileScheduler(std::uint64_t interval_ms);

    ReconcileScheduler(const ReconcileScheduler&) = delete;
    ReconcileScheduler& operator=(const ReconcileScheduler&) = delete;

    bool due(std::uint64_t now_ms) const noexcept;
    WindowEvent poll(std::uint64_t now_ms);
    void reset(std::uint64_t now_ms) noexcept;

private:
    const std::uint64_t interval_ms_;
    std::uint64_t next_due_ms_ = 0;
    std::uint64_t sequence_ = 0;
};

} // namespace stage_manager::window
