#pragma once

#ifdef _WIN32

#include "app/settings.h"
#include "app/settings_menu.h"

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace stage_manager::app {

enum class TrayActionType {
    None,
    ToggleEnabled,
    ApplySetting,
    Exit,
};

struct TrayAction {
    TrayActionType type = TrayActionType::None;
    std::optional<SettingSelection> setting;

    bool operator==(const TrayAction&) const noexcept = default;
};

// The caller owns the returned menu and must destroy it with DestroyMenu.
HMENU create_tray_context_menu(const Settings& settings, bool enabled);

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

    bool initialize(HWND owner, const Settings& settings);
    void shutdown();
    void set_enabled(bool enabled);
    bool enabled() const noexcept;
    void set_settings(const Settings& settings);
    bool is_setting_checked(std::uint32_t command) const noexcept;
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
    Settings settings_;
    TrayStatus status_ = TrayStatus::Running;
    std::wstring tooltip_;
};

} // namespace stage_manager::app

#endif
