#include "platform/win32/window_provider.h"

#ifdef _WIN32

#include <windows.h>

#include <dwmapi.h>
#include <objbase.h>
#include <shobjidl.h>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace stage_manager::platform::win32 {
namespace {

using stage_manager::window::NativeWindowHandle;
using stage_manager::window::PixelRect;
using stage_manager::window::SnapshotField;
using stage_manager::window::WindowSnapshot;

std::wstring read_class_name(HWND hwnd)
{
    std::wstring class_name(256, L'\0');
    const int length = GetClassNameW(hwnd, class_name.data(), static_cast<int>(class_name.size()));
    if (length <= 0) {
        return {};
    }
    class_name.resize(static_cast<std::size_t>(length));
    return class_name;
}

struct ComScope final {
    HRESULT result = E_FAIL;
    bool shouldUninitialize = false;

    ComScope()
    {
        result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        shouldUninitialize = result == S_OK || result == S_FALSE;
    }

    ~ComScope()
    {
        if (shouldUninitialize) {
            CoUninitialize();
        }
    }
};

struct EnumerationContext final {
    std::vector<HWND>* windows = nullptr;
};

BOOL CALLBACK collect_top_level_window(HWND hwnd, LPARAM parameter)
{
    auto& context = *reinterpret_cast<EnumerationContext*>(parameter);
    if (IsWindow(hwnd) && GetAncestor(hwnd, GA_ROOT) == hwnd) {
        context.windows->push_back(hwnd);
    }
    return TRUE;
}

struct ZOrderResult final {
    std::unordered_map<HWND, std::int32_t> indexes;
    bool valid = true;
};

ZOrderResult collect_z_order()
{
    ZOrderResult result;
    std::unordered_set<HWND> visited;
    constexpr std::size_t kMaximumTopLevelWindows = 100000;

    std::int32_t index = 0;
    for (HWND current = GetTopWindow(nullptr); current != nullptr;
         current = GetWindow(current, GW_HWNDNEXT)) {
        if (!visited.insert(current).second || visited.size() > kMaximumTopLevelWindows) {
            result.valid = false;
            return result;
        }
        result.indexes.emplace(current, index++);
    }
    return result;
}

PixelRect to_pixel_rect(const RECT& rect)
{
    return PixelRect{
        static_cast<std::int32_t>(rect.left),
        static_cast<std::int32_t>(rect.top),
        static_cast<std::int32_t>(rect.right),
        static_cast<std::int32_t>(rect.bottom),
    };
}

WindowSnapshot read_snapshot(HWND hwnd,
                              const std::unordered_map<HWND, std::int32_t>& z_order,
                              IVirtualDesktopManager* desktop_manager)
{
    WindowSnapshot snapshot;
    snapshot.key.hwnd = reinterpret_cast<NativeWindowHandle>(hwnd);
    snapshot.rootHwnd = reinterpret_cast<NativeWindowHandle>(GetAncestor(hwnd, GA_ROOT));
    snapshot.ownerHwnd = reinterpret_cast<NativeWindowHandle>(GetWindow(hwnd, GW_OWNER));
    snapshot.className = read_class_name(hwnd);
    if (snapshot.className.empty()) {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::ClassName);
    }
    snapshot.visible = IsWindowVisible(hwnd) != FALSE;
    snapshot.iconic = IsIconic(hwnd) != FALSE;
    snapshot.zoomed = IsZoomed(hwnd) != FALSE;

    DWORD process_id_value = 0;
    const DWORD process_id = GetWindowThreadProcessId(hwnd, &process_id_value);
    snapshot.key.processId = process_id_value;
    if (process_id == 0 || process_id_value == 0) {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::Process);
    }

    DWORD session_id = 0;
    if (ProcessIdToSessionId(snapshot.key.processId, &session_id)) {
        snapshot.sessionId = session_id;
    } else {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::Session);
    }

    snapshot.style = static_cast<std::uint32_t>(GetWindowLongPtrW(hwnd, GWL_STYLE));
    snapshot.exStyle = static_cast<std::uint32_t>(GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
    snapshot.topmost = (snapshot.exStyle & WS_EX_TOPMOST) != 0;

    RECT placement{};
    if (GetWindowRect(hwnd, &placement) != FALSE) {
        snapshot.placementRect = to_pixel_rect(placement);
    } else {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::PlacementRect);
    }

    RECT visual{};
    if (SUCCEEDED(DwmGetWindowAttribute(
            hwnd, DWMWA_EXTENDED_FRAME_BOUNDS, &visual, sizeof(visual)))) {
        snapshot.visualRect = to_pixel_rect(visual);
    } else {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::VisualRect);
        snapshot.visualRect = snapshot.placementRect;
    }

    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    snapshot.monitor = reinterpret_cast<stage_manager::window::NativeMonitorHandle>(monitor);
    if (monitor == nullptr) {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::Monitor);
    } else {
        MONITORINFO monitor_info{};
        monitor_info.cbSize = sizeof(monitor_info);
        if (GetMonitorInfoW(monitor, &monitor_info) != FALSE) {
            snapshot.workArea = to_pixel_rect(monitor_info.rcWork);
        } else {
            snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::Monitor);
        }
    }

    snapshot.dpi = GetDpiForWindow(hwnd);
    if (snapshot.dpi == 0) {
        snapshot.dpi = 96;
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::Dpi);
    }

    DWORD cloaked = 0;
    if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked)))) {
        snapshot.cloaked = cloaked != 0;
    } else {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::Cloaked);
    }

    if (desktop_manager != nullptr) {
        BOOL on_current_desktop = FALSE;
        if (SUCCEEDED(desktop_manager->IsWindowOnCurrentVirtualDesktop(hwnd, &on_current_desktop))) {
            snapshot.currentDesktop = on_current_desktop != FALSE;
        } else {
            snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::CurrentDesktop);
        }
    } else {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::CurrentDesktop);
    }

    const auto z_iterator = z_order.find(hwnd);
    if (z_iterator != z_order.end()) {
        snapshot.zIndex = z_iterator->second;
        snapshot.zOrderKnown = true;
    } else {
        snapshot.queryFailures |= stage_manager::window::field_bit(SnapshotField::ZOrder);
    }
    return snapshot;
}

} // namespace

window::WindowSnapshotBatch Win32WindowProvider::capture(window::SnapshotRefreshReason reason)
{
    window::WindowSnapshotBatch result;
    result.version = ++nextVersion_;
    result.reason = reason;

    ComScope com_scope;
    IVirtualDesktopManager* desktop_manager = nullptr;
    if (SUCCEEDED(com_scope.result) || com_scope.result == RPC_E_CHANGED_MODE) {
        static_cast<void>(CoCreateInstance(
            CLSID_VirtualDesktopManager,
            nullptr,
            CLSCTX_ALL,
            IID_PPV_ARGS(&desktop_manager)));
    }

    constexpr int kMaximumAttempts = 2;
    for (int attempt = 0; attempt < kMaximumAttempts; ++attempt) {
        std::vector<HWND> handles;
        EnumerationContext context{&handles};
        SetLastError(ERROR_SUCCESS);
        if (EnumWindows(&collect_top_level_window, reinterpret_cast<LPARAM>(&context)) == FALSE) {
            result.status = window::SnapshotStatus::EnumerationFailed;
            result.lastError = GetLastError();
            result.complete = false;
            if (desktop_manager != nullptr) {
                desktop_manager->Release();
            }
            return result;
        }

        const auto z_order = collect_z_order();
        if (!z_order.valid) {
            continue;
        }

        std::vector<window::WindowSnapshot> snapshots;
        snapshots.reserve(handles.size());
        bool stale = false;
        for (const auto hwnd : handles) {
            if (!IsWindow(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd ||
                z_order.indexes.find(hwnd) == z_order.indexes.end()) {
                stale = true;
                break;
            }
            snapshots.push_back(read_snapshot(hwnd, z_order.indexes, desktop_manager));
        }
        if (stale) {
            continue;
        }

        std::stable_sort(snapshots.begin(), snapshots.end(), [](const auto& left, const auto& right) {
            return left.zIndex < right.zIndex;
        });
        result.windows = std::move(snapshots);
        result.status = window::SnapshotStatus::Ok;
        result.complete = true;
        if (desktop_manager != nullptr) {
            desktop_manager->Release();
        }
        return result;
    }

    result.status = window::SnapshotStatus::Stale;
    result.complete = false;
    if (desktop_manager != nullptr) {
        desktop_manager->Release();
    }
    return result;
}

} // namespace stage_manager::platform::win32

#endif
