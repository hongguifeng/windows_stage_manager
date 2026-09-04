#pragma once

#ifdef _WIN32

#include <windows.h>

#include "app/tray_controller.h"

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
    void handle_tray_action(TrayAction action);
    void register_emergency_hotkey();
    void unregister_emergency_hotkey();

    HINSTANCE instance_ = nullptr;
    HANDLE instance_mutex_ = nullptr;
    HWND message_window_ = nullptr;
    TrayController tray_;
    bool enabled_ = true;
    bool emergency_hotkey_registered_ = false;
};

} // namespace stage_manager::app

#endif
