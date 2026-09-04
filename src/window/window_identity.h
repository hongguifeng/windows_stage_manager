#pragma once

#include "window/window_event.h"
#include "window/window_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace stage_manager::window {

class WindowIdentityTracker final {
public:
    WindowIdentityTracker() = default;

    WindowIdentityTracker(const WindowIdentityTracker&) = delete;
    WindowIdentityTracker& operator=(const WindowIdentityTracker&) = delete;

    WindowKey observe(NativeWindowHandle hwnd, std::uint32_t process_id);
    void handle_event(const WindowEvent& event);
    void apply(WindowSnapshotBatch& batch);
    void forget(NativeWindowHandle hwnd);
    void clear();

    std::size_t size() const noexcept;

private:
    struct Entry final {
        std::uint32_t processId = 0;
        std::uint64_t generation = 0;
    };

    std::uint64_t next_generation_ = 0;
    std::unordered_map<NativeWindowHandle, Entry> entries_;
};

} // namespace stage_manager::window
