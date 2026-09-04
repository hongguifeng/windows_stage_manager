#pragma once

#ifdef _WIN32

#include <windows.h>

namespace stage_manager::app {

class AppLifecycle final {
public:
    AppLifecycle() = default;
    ~AppLifecycle();

    AppLifecycle(const AppLifecycle&) = delete;
    AppLifecycle& operator=(const AppLifecycle&) = delete;

    int run(HINSTANCE instance, int show_command);

private:
    static constexpr wchar_t kMutexName[] = L"Local\\WindowsStageManager.SingleInstance";
    static constexpr wchar_t kMessageWindowClass[] = L"WindowsStageManager.MessageWindow";

    static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w_param, LPARAM l_param);

    bool acquire_single_instance();
    bool register_window_class();
    bool create_message_window();
    void destroy_message_window();
    LRESULT handle_message(HWND window, UINT message, WPARAM w_param, LPARAM l_param);
    void request_exit();

    HINSTANCE instance_ = nullptr;
    HANDLE instance_mutex_ = nullptr;
    HWND message_window_ = nullptr;
};

} // namespace stage_manager::app

#endif
