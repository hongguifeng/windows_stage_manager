#include "platform/win32/window_mover.h"

#ifdef _WIN32

#include <windows.h>

#include <cstdint>
#include <limits>

namespace {

bool is_below(HWND target, HWND reference)
{
    for (auto current = GetWindow(reference, GW_HWNDNEXT); current != nullptr;
         current = GetWindow(current, GW_HWNDNEXT)) {
        if (current == target) {
            return true;
        }
    }
    return false;
}

} // namespace

namespace stage_manager::platform::win32 {

window::NativeMoveResult Win32WindowMover::move(const window::WindowKey& window,
                                                 const geometry::Rect& destination)
{
    const auto hwnd = reinterpret_cast<HWND>(window.hwnd);
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return {window::NativeMoveStatus::InvalidWindow, ERROR_INVALID_WINDOW_HANDLE};
    }

    DWORD process_id = 0;
    if (GetWindowThreadProcessId(hwnd, &process_id) == 0 || process_id != window.processId) {
        return {window::NativeMoveStatus::IdentityMismatch, ERROR_INVALID_OWNER};
    }
    if (destination.left < std::numeric_limits<int>::min() ||
        destination.left > std::numeric_limits<int>::max() ||
        destination.top < std::numeric_limits<int>::min() ||
        destination.top > std::numeric_limits<int>::max()) {
        return {window::NativeMoveStatus::InvalidPosition, ERROR_INVALID_PARAMETER};
    }

    SetLastError(ERROR_SUCCESS);
    const auto flags = static_cast<UINT>(position_only_flags());
    if (SetWindowPos(hwnd,
                     nullptr,
                     static_cast<int>(destination.left),
                     static_cast<int>(destination.top),
                     0,
                     0,
                     flags) == FALSE) {
        return {window::NativeMoveStatus::ApiFailure, GetLastError()};
    }
    return {window::NativeMoveStatus::Moved, ERROR_SUCCESS};
}

window::NativeReorderResult Win32WindowMover::reorder(
    const window::WindowKey& window,
    const window::WindowKey& insert_after)
{
    const auto hwnd = reinterpret_cast<HWND>(window.hwnd);
    const auto reference = reinterpret_cast<HWND>(insert_after.hwnd);
    if (hwnd == nullptr || IsWindow(hwnd) == FALSE) {
        return {window::NativeReorderStatus::InvalidWindow, ERROR_INVALID_WINDOW_HANDLE};
    }
    DWORD process_id = 0;
    if (GetWindowThreadProcessId(hwnd, &process_id) == 0 || process_id != window.processId) {
        return {window::NativeReorderStatus::IdentityMismatch, ERROR_INVALID_OWNER};
    }
    if (reference == nullptr || IsWindow(reference) == FALSE) {
        return {window::NativeReorderStatus::InvalidReference, ERROR_INVALID_WINDOW_HANDLE};
    }
    process_id = 0;
    if (GetWindowThreadProcessId(reference, &process_id) == 0 ||
        process_id != insert_after.processId) {
        return {window::NativeReorderStatus::IdentityMismatch, ERROR_INVALID_OWNER};
    }
    if (hwnd == reference) {
        return {window::NativeReorderStatus::InvalidReference, ERROR_INVALID_PARAMETER};
    }
    const auto foreground = GetForegroundWindow();
    if (foreground == nullptr ||
        (GetWindowLongPtrW(hwnd, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0 ||
        (GetWindowLongPtrW(reference, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0 ||
        !is_below(hwnd, foreground) ||
        (reference != foreground && !is_below(reference, foreground)) ||
        !is_below(hwnd, reference)) {
        return {window::NativeReorderStatus::UnsafeZOrder, ERROR_ACCESS_DENIED};
    }

    SetLastError(ERROR_SUCCESS);
    if (SetWindowPos(hwnd,
                     reference,
                     0,
                     0,
                     0,
                     0,
                     static_cast<UINT>(z_order_only_flags())) == FALSE) {
        return {window::NativeReorderStatus::ApiFailure, GetLastError()};
    }
    if (GetForegroundWindow() != foreground || !is_below(hwnd, foreground) ||
        !is_below(hwnd, reference)) {
        return {window::NativeReorderStatus::UnsafeZOrder, ERROR_ACCESS_DENIED};
    }
    return {window::NativeReorderStatus::Reordered, ERROR_SUCCESS};
}

} // namespace stage_manager::platform::win32

#endif
