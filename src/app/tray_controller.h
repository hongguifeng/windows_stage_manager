#pragma once

#ifdef _WIN32

#include "app/settings.h"
#include "app/settings_menu.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace stage_manager::app {

enum class TrayActionType {
    None,
    ToggleEnabled,
    ApplySetting,
    RequestCustomSetting,
    ApplyCustomSetting,
    OpenSettings,
    Exit,
};

struct TrayAction {
    TrayActionType type = TrayActionType::None;
    std::optional<SettingSelection> setting;

    bool operator==(const TrayAction&) const noexcept = default;
};

// The caller owns the returned menu and must destroy it with DestroyMenu.
HMENU create_tray_context_menu(const Settings& settings, bool enabled);
std::optional<std::uint32_t> prompt_custom_setting_value(
    HWND owner, SettingField field, std::uint32_t current_value);

enum class TrayStatus {
    Running,
    Paused,
    Unsatisfiable,
    ApiError,
    Rebuilding,
};

struct UnsatisfiableNotification final {
    std::wstring_view title;
    std::wstring_view message;
    std::uint32_t timeoutMs = 0;
    DWORD flags = 0;
};

constexpr std::uint64_t kUnsatisfiableNotificationCooldownMs = 10'000;

UnsatisfiableNotification unsatisfiable_notification() noexcept;
bool unsatisfiable_notification_due(
    std::optional<std::uint64_t> last_notification_ms,
    std::uint64_t now_ms) noexcept;

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
    bool show_unsatisfiable_notification(std::uint64_t now_ms);

    TrayAction handle_callback(LPARAM event);
    TrayAction handle_command(WPARAM command);

    static constexpr UINT kTrayCallbackMessage = WM_APP + 1;
    static constexpr UINT kCommandToggle = 1001;
    static constexpr UINT kCommandExit = 1002;
    static constexpr UINT kCommandSettings = 1003;

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
    std::optional<std::uint64_t> last_unsatisfiable_notification_ms_;
};

} // namespace stage_manager::app

#endif
