#include "platform/win32/window_mover.h"

#ifdef _WIN32

#include <windows.h>

#include <cstdint>
#include <limits>

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

} // namespace stage_manager::platform::win32

#endif
