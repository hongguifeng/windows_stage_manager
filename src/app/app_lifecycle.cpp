#include "app/app_lifecycle.h"

#ifdef _WIN32

#include "diagnostics/logger.h"
#include "app/version.h"

#include <string>

namespace stage_manager::app {

AppLifecycle::~AppLifecycle()
{
    unregister_emergency_hotkey();
    tray_.shutdown();
    destroy_message_window();
    if (instance_ != nullptr) {
        UnregisterClassW(kMessageWindowClass, instance_);
    }
    if (instance_mutex_ != nullptr) {
        CloseHandle(instance_mutex_);
    }
    diagnostics::Logger::instance().shutdown();
}

int AppLifecycle::run(HINSTANCE instance, int)
{
    instance_ = instance;
    if (!acquire_single_instance() || !register_window_class() || !create_message_window()) {
        return 1;
    }
    settings_path_ = default_settings_path();
    settings_ = load_settings(settings_path_);
    enabled_ = settings_.enabled;
    dry_run_ = settings_.dryRun;
    diagnostics::Logger::instance().initialize(default_log_path());
    diagnostics::Logger::instance().log(
        diagnostics::LogLevel::Info,
        "application_start",
        {{"version", kVersion}, {"dry_run", dry_run_ ? "true" : "false"}});
    if (!tray_.initialize(message_window_, enabled_)) {
        return 1;
    }
    register_emergency_hotkey();

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

bool AppLifecycle::enabled() const noexcept
{
    return enabled_;
}

bool AppLifecycle::dry_run() const noexcept
{
    return dry_run_;
}

std::uint64_t AppLifecycle::environment_generation() const noexcept
{
    return environment_generation_;
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
    case TrayController::kTrayCallbackMessage:
        handle_tray_action(tray_.handle_callback(l_param));
        return 0;
    case WM_COMMAND:
        handle_tray_action(tray_.handle_command(w_param));
        return 0;
    case WM_HOTKEY:
        if (w_param == 1) {
            enabled_ = false;
            settings_.enabled = enabled_;
            tray_.set_enabled(false);
            persist_settings();
        }
        return 0;
    case WM_DISPLAYCHANGE:
        mark_environment_changed("display_change");
        return 0;
    case WM_SETTINGCHANGE:
        mark_environment_changed("setting_change");
        return 0;
    case WM_DPICHANGED:
        mark_environment_changed("dpi_change");
        return 0;
    case WM_CLOSE:
        request_exit();
        return 0;
    case WM_DESTROY:
        unregister_emergency_hotkey();
        tray_.shutdown();
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

void AppLifecycle::handle_tray_action(TrayAction action)
{
    switch (action) {
    case TrayAction::ToggleEnabled:
        enabled_ = !enabled_;
        settings_.enabled = enabled_;
        tray_.set_enabled(enabled_);
        persist_settings();
        return;
    case TrayAction::Exit:
        request_exit();
        return;
    case TrayAction::None:
        return;
    }
}

void AppLifecycle::register_emergency_hotkey()
{
    emergency_hotkey_registered_ = RegisterHotKey(
        message_window_, 1, MOD_CONTROL | MOD_ALT | MOD_NOREPEAT, VK_F12) != FALSE;
}

void AppLifecycle::unregister_emergency_hotkey()
{
    if (emergency_hotkey_registered_ && message_window_ != nullptr) {
        UnregisterHotKey(message_window_, 1);
    }
    emergency_hotkey_registered_ = false;
}

void AppLifecycle::mark_environment_changed(const char* reason)
{
    ++environment_generation_;
    diagnostics::Logger::instance().log(
        diagnostics::LogLevel::Debug,
        "environment_changed",
        {{"generation", std::to_string(environment_generation_)}, {"reason", reason}});
}

void AppLifecycle::persist_settings()
{
    if (!settings_path_.empty() && !save_settings(settings_, settings_path_)) {
        diagnostics::Logger::instance().log(
            diagnostics::LogLevel::Warning, "settings_save_failed", {{"path", settings_path_.string()}});
    }
}

} // namespace stage_manager::app

#endif
