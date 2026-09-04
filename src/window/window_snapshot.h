#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace stage_manager::window {

using NativeWindowHandle = std::uintptr_t;
using NativeMonitorHandle = std::uintptr_t;

struct PixelRect {
    std::int32_t left = 0;
    std::int32_t top = 0;
    std::int32_t right = 0;
    std::int32_t bottom = 0;

    constexpr bool valid() const noexcept
    {
        return right >= left && bottom >= top;
    }
};

struct WindowKey {
    NativeWindowHandle hwnd = 0;
    std::uint32_t processId = 0;
    std::uint64_t instanceGeneration = 0;
};

enum class SnapshotRefreshReason : std::uint8_t {
    Initial,
    Event,
    Reconcile,
    EnvironmentChange,
    HookRecovery,
    Manual,
};

enum class SnapshotStatus : std::uint8_t {
    Ok,
    EnumerationFailed,
    Stale,
};

enum class SnapshotField : std::uint32_t {
    Process = 1u << 0,
    Session = 1u << 1,
    PlacementRect = 1u << 2,
    VisualRect = 1u << 3,
    Monitor = 1u << 4,
    Dpi = 1u << 5,
    Cloaked = 1u << 6,
    CurrentDesktop = 1u << 7,
    ZOrder = 1u << 8,
    ClassName = 1u << 9,
};

constexpr std::uint32_t field_bit(SnapshotField field) noexcept
{
    return static_cast<std::uint32_t>(field);
}

struct WindowSnapshot {
    WindowKey key;
    NativeWindowHandle rootHwnd = 0;
    NativeWindowHandle ownerHwnd = 0;
    std::wstring className;

    PixelRect placementRect;
    PixelRect visualRect;
    PixelRect workArea;
    NativeMonitorHandle monitor = 0;

    std::uint32_t sessionId = 0;
    std::uint32_t dpi = 96;
    std::uint32_t style = 0;
    std::uint32_t exStyle = 0;
    std::uint32_t queryFailures = 0;
    std::int32_t zIndex = -1;

    bool visible = false;
    bool iconic = false;
    bool zoomed = false;
    bool topmost = false;
    bool cloaked = false;
    bool currentDesktop = false;
    bool managed = false;
    bool zOrderKnown = false;
};

struct WindowSnapshotBatch {
    std::uint64_t version = 0;
    SnapshotRefreshReason reason = SnapshotRefreshReason::Manual;
    SnapshotStatus status = SnapshotStatus::Ok;
    std::uint32_t lastError = 0;
    bool complete = false;
    std::vector<WindowSnapshot> windows;
};

} // namespace stage_manager::window
