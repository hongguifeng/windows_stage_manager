#include "app/tray_controller.h"

#include <string>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main()
{
    using stage_manager::app::TrayActionType;
    using stage_manager::app::TrayController;
    using stage_manager::app::TrayStatus;
    using stage_manager::app::SettingField;

    TrayController controller;
    CHECK(controller.handle_command(TrayController::kCommandToggle).type ==
          TrayActionType::ToggleEnabled);
    CHECK(controller.handle_command(TrayController::kCommandExit).type ==
          TrayActionType::Exit);
    CHECK(controller.handle_command(TrayController::kCommandSettings).type ==
          TrayActionType::OpenSettings);
    CHECK(controller.handle_callback(WM_LBUTTONUP).type == TrayActionType::ToggleEnabled);
    CHECK(controller.handle_callback(WM_RBUTTONUP).type == TrayActionType::None);
    CHECK(controller.handle_command(9999).type == TrayActionType::None);

    stage_manager::app::Settings settings;
    settings.dryRun = false;
    settings.preferredExposedEdges = 2;
    controller.set_settings(settings);
    const auto dry_run_command = stage_manager::app::setting_command_id(SettingField::DryRun, 1);
    const auto setting_action = controller.handle_command(dry_run_command);
    CHECK(setting_action.type == TrayActionType::ApplySetting);
    CHECK(setting_action.setting.has_value());
    CHECK(setting_action.setting->field == SettingField::DryRun);
    CHECK(setting_action.setting->value == 1);
    CHECK(!controller.is_setting_checked(dry_run_command));
    const auto active_command = stage_manager::app::setting_command_id(SettingField::DryRun, 0);
    CHECK(controller.is_setting_checked(active_command));
    const auto preferred_two =
        stage_manager::app::setting_command_id(SettingField::PreferredExposedEdges, 1);
    CHECK(controller.is_setting_checked(preferred_two));
    const auto centering_on =
        stage_manager::app::setting_command_id(SettingField::CenterActivatedWindow, 1);
    CHECK(controller.is_setting_checked(centering_on));
    const auto custom_edge_command =
        stage_manager::app::custom_setting_command_id(SettingField::MinimumExposedEdgeDip);
    const auto custom_action = controller.handle_command(custom_edge_command);
    CHECK(custom_action.type == TrayActionType::RequestCustomSetting);
    CHECK(custom_action.setting.has_value());
    CHECK(custom_action.setting->field == SettingField::MinimumExposedEdgeDip);
    CHECK(custom_action.setting->value == settings.minExposedEdgeDip);

    const HMENU menu = stage_manager::app::create_tray_context_menu(settings, true);
    CHECK(menu != nullptr);
    CHECK(GetMenuItemCount(menu) == 6);
    wchar_t menu_text[128]{};
    CHECK(GetMenuStringW(menu, 0, menu_text, 128, MF_BYPOSITION) > 0);
    CHECK(std::wstring(menu_text) == L"\u6682\u505c\u7ba1\u7406");
    CHECK(GetMenuStringW(menu, 2, menu_text, 128, MF_BYPOSITION) > 0);
    CHECK(std::wstring(menu_text) == L"\u6253\u5f00\u53ef\u89c6\u5316\u8bbe\u7f6e\u2026");
    CHECK(GetMenuStringW(menu, 3, menu_text, 128, MF_BYPOSITION) > 0);
    CHECK(std::wstring(menu_text) == L"\u5feb\u901f\u53c2\u6570\u8bbe\u7f6e");
    CHECK(GetMenuStringW(menu, 5, menu_text, 128, MF_BYPOSITION) > 0);
    CHECK(std::wstring(menu_text) == L"\u9000\u51fa");
    const HMENU settings_menu = GetSubMenu(menu, 3);
    CHECK(settings_menu != nullptr);
    CHECK(GetMenuItemCount(settings_menu) ==
          static_cast<int>(stage_manager::app::setting_fields().size()));
    const HMENU run_mode_menu = GetSubMenu(settings_menu, 0);
    CHECK(run_mode_menu != nullptr);
    CHECK(GetMenuItemCount(run_mode_menu) == 2);
    CHECK((GetMenuState(run_mode_menu, active_command, MF_BYCOMMAND) & MF_CHECKED) != 0);
    CHECK((GetMenuState(run_mode_menu, dry_run_command, MF_BYCOMMAND) & MF_CHECKED) == 0);
    const HMENU edge_length_menu = GetSubMenu(settings_menu, 2);
    CHECK(edge_length_menu != nullptr);
    CHECK(GetMenuItemCount(edge_length_menu) == 7);
    CHECK(GetMenuStringW(edge_length_menu,
                         custom_edge_command,
                         menu_text,
                         128,
                         MF_BYCOMMAND) > 0);
    CHECK(std::wstring(menu_text) == L"\u81ea\u5b9a\u4e49\u2026");
    CHECK(DestroyMenu(menu) != FALSE);

    CHECK(controller.status() == TrayStatus::Running);
    controller.set_status(TrayStatus::Unsatisfiable);
    CHECK(controller.status() == TrayStatus::Unsatisfiable);
    CHECK(controller.enabled());
    controller.set_status(TrayStatus::ApiError);
    CHECK(controller.status() == TrayStatus::ApiError);
    controller.set_status(TrayStatus::Rebuilding);
    CHECK(controller.status() == TrayStatus::Rebuilding);
    controller.set_enabled(false);
    CHECK(controller.status() == TrayStatus::Paused);
    CHECK(!controller.enabled());
    return 0;
}
