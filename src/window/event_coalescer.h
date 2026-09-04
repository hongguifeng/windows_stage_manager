#pragma once

#include "window/window_event.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stage_manager::window {

struct CoalescedBatch {
    std::vector<WindowEvent> events;
    std::size_t inputCount = 0;
    std::size_t coalescedLocationCount = 0;
    bool requiresFullReconcile = false;
};

class EventCoalescer final {
public:
    CoalescedBatch coalesce(std::span<const WindowEvent> input);
    void request_full_reconcile() noexcept;

private:
    bool reconcile_requested_ = false;
};

} // namespace stage_manager::window
