#include "platform/win32/win_event_hook.h"

#ifdef _WIN32

#include <windows.h>

#include <cassert>
#include <chrono>
#include <cstdint>

namespace {

constexpr wchar_t kTestClassName[] = L"WindowsStageManager.WinEventTestWindow";

LRESULT CALLBACK test_window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    return DefWindowProcW(window, message, w_param, l_param);
}

bool register_test_class(HINSTANCE instance)
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance;
    window_class.lpfnWndProc = &test_window_proc;
    window_class.lpszClassName = kTestClassName;
    return RegisterClassExW(&window_class) != 0;
}

} // namespace

int main()
{
    using namespace std::chrono_literals;
    using stage_manager::platform::win32::WinEventHook;
    using stage_manager::window::EventQueue;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;

    assert(stage_manager::platform::win32::suppress_layout_for_right_button(
        EVENT_SYSTEM_FOREGROUND, static_cast<SHORT>(0x8000)));
    assert(!stage_manager::platform::win32::suppress_layout_for_right_button(
        EVENT_SYSTEM_FOREGROUND, 0));
    assert(!stage_manager::platform::win32::suppress_layout_for_right_button(
        EVENT_OBJECT_LOCATIONCHANGE, static_cast<SHORT>(0x8000)));

    EventQueue queue(128);
    WinEventHook hook(queue);
    assert(hook.start());

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    assert(register_test_class(instance));
    const HWND window = CreateWindowExW(
        WS_EX_TOOLWINDOW,
        kTestClassName,
        L"WinEvent test",
        WS_POPUP,
        0,
        0,
        160,
        120,
        nullptr,
        nullptr,
        instance,
        nullptr);
    assert(window != nullptr);

    ShowWindow(window, SW_SHOWNA);
    SetWindowPos(window, nullptr, 20, 20, 160, 120, SWP_NOACTIVATE | SWP_NOZORDER);
    ShowWindow(window, SW_HIDE);
    DestroyWindow(window);

    bool saw_show = false;
    bool saw_location = false;
    bool saw_hide = false;
    bool saw_destroy = false;
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline && !saw_destroy) {
        WindowEvent event{};
        if (!queue.wait_pop(event, 50ms)) {
            continue;
        }
        if (event.hwnd != reinterpret_cast<std::uintptr_t>(window)) {
            continue;
        }
        saw_show = saw_show || event.type == WindowEventType::Show;
        saw_location = saw_location || event.type == WindowEventType::LocationChange;
        saw_hide = saw_hide || event.type == WindowEventType::Hide;
        saw_destroy = saw_destroy || event.type == WindowEventType::Destroy;
    }

    hook.stop();
    UnregisterClassW(kTestClassName, instance);

    assert(saw_show);
    assert(saw_location);
    assert(saw_hide);
    assert(saw_destroy);
    return 0;
}

#else

int main()
{
    return 0;
}

#endif
