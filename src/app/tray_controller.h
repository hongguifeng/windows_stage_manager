#pragma once

#ifdef _WIN32

#include <windows.h>

#include <string>
#include <string_view>

namespace stage_manager::app {

enum class TrayAction {
    None,
    ToggleEnabled,
    Exit,
};

enum class TrayStatus {
    Running,
    Paused,
    Unsatisfiable,
    ApiError,
    Rebuilding,
};

class TrayController final {
public:
    TrayController() = default;
    ~TrayController();

    TrayController(const TrayController&) = delete;
    TrayController& operator=(const TrayController&) = delete;

    bool initialize(HWND owner, bool enabled);
    void shutdown();
    void set_enabled(bool enabled);
    bool enabled() const noexcept;
    void set_status(TrayStatus status);
    TrayStatus status() const noexcept;

    TrayAction handle_callback(LPARAM event);
    TrayAction handle_command(WPARAM command);

    static constexpr UINT kTrayCallbackMessage = WM_APP + 1;
    static constexpr UINT kCommandToggle = 1001;
    static constexpr UINT kCommandExit = 1002;

private:
    void show_context_menu();
    void update_tooltip();

    HWND owner_ = nullptr;
    NOTIFYICONDATAW icon_data_{};
    bool installed_ = false;
    bool enabled_ = true;
    TrayStatus status_ = TrayStatus::Running;
    std::wstring tooltip_;
};

} // namespace stage_manager::app

#endif
