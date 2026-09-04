#include "window/event_coalescer.h"

#include <algorithm>
#include <unordered_map>

namespace stage_manager::window {

CoalescedBatch EventCoalescer::coalesce(std::span<const WindowEvent> input)
{
    CoalescedBatch batch;
    batch.inputCount = input.size();
    batch.requiresFullReconcile = reconcile_requested_;
    reconcile_requested_ = false;

    std::unordered_map<std::uintptr_t, WindowEvent> latest_locations;
    batch.events.reserve(input.size());
    for (const auto& event : input) {
        if (event.type == WindowEventType::LocationChange) {
            latest_locations[event.hwnd] = event;
            continue;
        }
        if (event.type == WindowEventType::Reconcile ||
            event.type == WindowEventType::HookError) {
            batch.requiresFullReconcile = true;
        }
        batch.events.push_back(event);
    }

    batch.coalescedLocationCount = latest_locations.size();
    for (const auto& [unused_hwnd, event] : latest_locations) {
        static_cast<void>(unused_hwnd);
        batch.events.push_back(event);
    }

    std::stable_sort(batch.events.begin(), batch.events.end(), [](const auto& left, const auto& right) {
        if (left.sequence != right.sequence) {
            return left.sequence < right.sequence;
        }
        if (left.hwnd != right.hwnd) {
            return left.hwnd < right.hwnd;
        }
        return static_cast<std::uint8_t>(left.type) < static_cast<std::uint8_t>(right.type);
    });
    return batch;
}

void EventCoalescer::request_full_reconcile() noexcept
{
    reconcile_requested_ = true;
}

} // namespace stage_manager::window
