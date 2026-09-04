#include "app/app_lifecycle.h"

#ifdef _WIN32

#include "app/version.h"

namespace stage_manager::app {

AppLifecycle::~AppLifecycle()
{
    destroy_message_window();
    if (instance_ != nullptr) {
        UnregisterClassW(kMessageWindowClass, instance_);
    }
    if (instance_mutex_ != nullptr) {
        CloseHandle(instance_mutex_);
    }
}

int AppLifecycle::run(HINSTANCE instance, int)
{
    instance_ = instance;
    if (!acquire_single_instance() || !register_window_class() || !create_message_window()) {
        return 1;
    }

    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result == -1) {
            return 1;
        }
        if (result == 0) {
            return static_cast<int>(message.wParam);
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

bool AppLifecycle::acquire_single_instance()
{
    instance_mutex_ = CreateMutexW(nullptr, TRUE, kMutexName);
    if (instance_mutex_ == nullptr) {
        return false;
    }
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(instance_mutex_);
        instance_mutex_ = nullptr;
        return false;
    }
    return true;
}

bool AppLifecycle::register_window_class()
{
    WNDCLASSEXW window_class{};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = instance_;
    window_class.lpfnWndProc = &AppLifecycle::window_proc;
    window_class.lpszClassName = kMessageWindowClass;

    if (RegisterClassExW(&window_class) != 0) {
        return true;
    }
    return GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}

bool AppLifecycle::create_message_window()
{
    message_window_ = CreateWindowExW(
        0,
        kMessageWindowClass,
        L"WindowsStageManager",
        0,
        0,
        0,
        0,
        0,
        HWND_MESSAGE,
        nullptr,
        instance_,
        this);
    return message_window_ != nullptr;
}

void AppLifecycle::destroy_message_window()
{
    if (message_window_ != nullptr) {
        DestroyWindow(message_window_);
        message_window_ = nullptr;
    }
}

LRESULT CALLBACK AppLifecycle::window_proc(
    HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* lifecycle = reinterpret_cast<AppLifecycle*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create_struct = reinterpret_cast<const CREATESTRUCTW*>(l_param);
        lifecycle = static_cast<AppLifecycle*>(create_struct->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(lifecycle));
    }
    if (lifecycle != nullptr) {
        return lifecycle->handle_message(window, message, w_param, l_param);
    }
    return DefWindowProcW(window, message, w_param, l_param);
}

LRESULT AppLifecycle::handle_message(HWND window, UINT message, WPARAM w_param, LPARAM l_param)
{
    switch (message) {
    case WM_QUERYENDSESSION:
        return TRUE;
    case WM_ENDSESSION:
        if (w_param != FALSE) {
            request_exit();
        }
        return 0;
    case WM_CLOSE:
        request_exit();
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(window, message, w_param, l_param);
    }
}

void AppLifecycle::request_exit()
{
    if (message_window_ != nullptr) {
        DestroyWindow(message_window_);
        message_window_ = nullptr;
    }
}

} // namespace stage_manager::app

#endif
