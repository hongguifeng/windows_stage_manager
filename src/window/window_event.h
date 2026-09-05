#pragma once

#include <cstdint>
#include <string_view>

namespace stage_manager::window {

enum class WindowEventType : std::uint8_t {
    MoveSizeStart,
    MoveSizeEnd,
    Foreground,
    LocationChange,
    Show,
    Hide,
    Destroy,
    Reconcile,
    HookError,
};

struct WindowEvent {
    WindowEventType type = WindowEventType::Reconcile;
    std::uintptr_t hwnd = 0;
    std::uint32_t eventThreadId = 0;
    std::uint64_t timestampMs = 0;
    std::uint64_t sequence = 0;
    bool suppressLayout = false;
};

constexpr std::string_view to_string(WindowEventType type) noexcept
{
    switch (type) {
    case WindowEventType::MoveSizeStart:
        return "movesize_start";
    case WindowEventType::MoveSizeEnd:
        return "movesize_end";
    case WindowEventType::Foreground:
        return "foreground";
    case WindowEventType::LocationChange:
        return "location_change";
    case WindowEventType::Show:
        return "show";
    case WindowEventType::Hide:
        return "hide";
    case WindowEventType::Destroy:
        return "destroy";
    case WindowEventType::Reconcile:
        return "reconcile";
    case WindowEventType::HookError:
        return "hook_error";
    }
    return "unknown";
}

} // namespace stage_manager::window
