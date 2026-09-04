#include "platform/win32/hit_tester.h"

#ifdef _WIN32

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

constexpr wchar_t kTestClassName[] = L"WindowsStageManager.HitTestingTestWindow";

LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    return DefWindowProcW(window, message, w_param, l_param);
}

} // namespace

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::geometry::Point;
    using stage_manager::geometry::point_belongs_to_window;
    using stage_manager::platform::win32::Win32RootWindowHitTester;

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &test_window_proc;
    window_class.lpszClassName = kTestClassName;
    CHECK(RegisterClassExW(&window_class) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);

    const HWND parent = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        kTestClassName,
        L"Hit testing parent",
        WS_POPUP,
        100,
        100,
        240,
        180,
        nullptr,
        nullptr,
        instance,
        nullptr);
    CHECK(parent != nullptr);
    const HWND child = CreateWindowExW(
        0,
        L"STATIC",
        L"child",
        WS_CHILD | WS_VISIBLE,
        20,
        20,
        100,
        80,
        parent,
        nullptr,
        instance,
        nullptr);
    CHECK(child != nullptr);
    ShowWindow(parent, SW_SHOWNA);
    UpdateWindow(parent);

    POINT child_point{10, 10};
    CHECK(ClientToScreen(child, &child_point));
    Win32RootWindowHitTester hit_tester;
    const auto expected_root = reinterpret_cast<std::uintptr_t>(parent);
    CHECK(point_belongs_to_window(
        hit_tester, Point{child_point.x, child_point.y}, expected_root));
    CHECK(hit_tester.root_window_at(
              Point{std::numeric_limits<std::int64_t>::max(), 0}) == 0);

    DestroyWindow(parent);
    UnregisterClassW(kTestClassName, instance);
    return 0;
}

#else

int main()
{
    return 0;
}

#endif
