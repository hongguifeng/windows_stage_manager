#pragma once

#ifdef _WIN32

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <thread>

#include "app/settings.h"
#include "app/tray_controller.h"
#include "diagnostics/runtime_metrics.h"
#include "platform/win32/win_event_hook.h"
#include "platform/win32/window_mover.h"
#include "platform/win32/window_provider.h"
#include "window/move_applier.h"
#include "window/mvp_coordinator.h"
#include "window/tracking_window_provider.h"

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
    static constexpr UINT kCoordinatorStatusMessage = WM_APP + 2;

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
    bool start_window_manager();
    void stop_window_manager();
    void coordinator_loop();
    void update_runtime_status(window::MvpBatchStatus status);

    HINSTANCE instance_ = nullptr;
    HANDLE instance_mutex_ = nullptr;
    HWND message_window_ = nullptr;
    TrayController tray_;
    Settings settings_;
    std::filesystem::path settings_path_;
    std::atomic<bool> enabled_{true};
    std::atomic<bool> dry_run_{true};
    diagnostics::RuntimeMetrics runtime_metrics_;
    bool emergency_hotkey_registered_ = false;
    std::uint64_t environment_generation_ = 0;
    std::unique_ptr<window::EventQueue> event_queue_;
    std::unique_ptr<platform::win32::WinEventHook> event_hook_;
    std::unique_ptr<platform::win32::Win32WindowProvider> raw_provider_;
    std::unique_ptr<window::WindowIdentityTracker> identities_;
    std::unique_ptr<window::TrackingWindowProvider> provider_;
    std::unique_ptr<window::InternalMoveTracker> internal_moves_;
    std::unique_ptr<window::MoveTransactionGuard> move_guard_;
    std::unique_ptr<window::MoveFailureTracker> move_failures_;
    std::unique_ptr<platform::win32::Win32WindowMover> mover_;
    std::unique_ptr<window::VerifiedMoveApplier> move_applier_;
    std::unique_ptr<window::MvpCoordinator> coordinator_;
    std::thread coordinator_thread_;
    std::atomic<bool> coordinator_stop_{false};
};

} // namespace stage_manager::app

#endif
