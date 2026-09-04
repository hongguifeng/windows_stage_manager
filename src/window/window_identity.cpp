#include "window/window_identity.h"

namespace stage_manager::window {

WindowKey WindowIdentityTracker::observe(NativeWindowHandle hwnd, std::uint32_t process_id)
{
    if (hwnd == 0 || process_id == 0) {
        if (hwnd != 0) {
            forget(hwnd);
        }
        return WindowKey{hwnd, process_id, 0};
    }

    const auto iterator = entries_.find(hwnd);
    if (iterator != entries_.end() && iterator->second.processId == process_id) {
        return WindowKey{hwnd, process_id, iterator->second.generation};
    }

    const auto generation = ++next_generation_;
    entries_[hwnd] = Entry{process_id, generation};
    return WindowKey{hwnd, process_id, generation};
}

void WindowIdentityTracker::handle_event(const WindowEvent& event)
{
    if (event.type == WindowEventType::Destroy) {
        forget(event.hwnd);
    }
}

void WindowIdentityTracker::apply(WindowSnapshotBatch& batch)
{
    for (auto& snapshot : batch.windows) {
        snapshot.key = observe(snapshot.key.hwnd, snapshot.key.processId);
    }
}

void WindowIdentityTracker::forget(NativeWindowHandle hwnd)
{
    entries_.erase(hwnd);
}

void WindowIdentityTracker::clear()
{
    entries_.clear();
}

std::size_t WindowIdentityTracker::size() const noexcept
{
    return entries_.size();
}

} // namespace stage_manager::window
