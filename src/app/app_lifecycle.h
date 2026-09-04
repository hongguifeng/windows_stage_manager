#pragma once

#ifdef _WIN32

#include <windows.h>

#include <cstdint>
#include <filesystem>

#include "app/settings.h"
#include "app/tray_controller.h"

namespace stage_manager::app {

class AppLifecycle final {
public:
    AppLifecycle() = default;
    ~AppLifecycle();

    AppLifecycle(const AppLifecycle&) = delete;
    AppLifecycle& operator=(const AppLifecycle&) = delete;

    int run(HINSTANCE instance, int show_command);
    bool enabled() const noexcept;
    bool dry_run() const noexcept;
    std::uint64_t environment_generation() const noexcept;

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
    void mark_environment_changed(const char* reason);
    void persist_settings();

    HINSTANCE instance_ = nullptr;
    HANDLE instance_mutex_ = nullptr;
    HWND message_window_ = nullptr;
    TrayController tray_;
    Settings settings_;
    std::filesystem::path settings_path_;
    bool enabled_ = true;
    bool dry_run_ = true;
    bool emergency_hotkey_registered_ = false;
    std::uint64_t environment_generation_ = 0;
};

} // namespace stage_manager::app

#endif
