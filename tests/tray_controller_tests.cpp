#include "app/tray_controller.h"

#include <optional>
#include <string>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main()
{
    using stage_manager::app::TrayActionType;
    using stage_manager::app::TrayController;
    using stage_manager::app::TrayStatus;
    using stage_manager::app::SettingField;

    const auto notification = stage_manager::app::unsatisfiable_notification();
    CHECK(notification.title == L"\u7a97\u53e3\u5e03\u5c40\u6682\u65f6\u65e0\u89e3");
    CHECK(notification.message.find(L"\u672a\u6539\u53d8\u7a97\u53e3\u5c42\u7ea7") !=
          std::wstring_view::npos);
    CHECK(notification.message.find(L"\u7a97\u53e3\u72b6\u6001\u53d8\u5316\u540e\u91cd\u8bd5") !=
          std::wstring_view::npos);
    CHECK(notification.timeoutMs == 5000);
    CHECK((notification.flags & NIIF_WARNING) != 0);
    CHECK((notification.flags & NIIF_NOSOUND) != 0);
    CHECK(stage_manager::app::unsatisfiable_notification_due(std::nullopt, 0));
    CHECK(!stage_manager::app::unsatisfiable_notification_due(100, 10'099));
    CHECK(stage_manager::app::unsatisfiable_notification_due(100, 10'100));
    CHECK(!stage_manager::app::unsatisfiable_notification_due(100, 99));

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
    settings.affordancePreset = stage_manager::app::AffordancePreset::Balanced;
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
    const auto balanced =
        stage_manager::app::setting_command_id(SettingField::AffordancePreset, 1);
    CHECK(controller.is_setting_checked(balanced));
    const auto placement_on =
        stage_manager::app::setting_command_id(SettingField::PlaceActivatedWindow, 1);
    CHECK(controller.is_setting_checked(placement_on));
    const auto horizontal_center =
        stage_manager::app::setting_command_id(
            SettingField::ActivationHorizontalAlignment, 1);
    const auto vertical_bottom =
        stage_manager::app::setting_command_id(
            SettingField::ActivationVerticalAlignment, 2);
    CHECK(controller.is_setting_checked(horizontal_center));
    CHECK(controller.is_setting_checked(vertical_bottom));
    const auto custom_edge_command =
        stage_manager::app::custom_setting_command_id(SettingField::TopDepthDip);
    const auto custom_action = controller.handle_command(custom_edge_command);
    CHECK(custom_action.type == TrayActionType::RequestCustomSetting);
    CHECK(custom_action.setting.has_value());
    CHECK(custom_action.setting->field == SettingField::TopDepthDip);
    CHECK(custom_action.setting->value == settings.topDepthDip);

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
    CHECK(GetMenuStringW(settings_menu, 2, menu_text, 128, MF_BYPOSITION) > 0);
    CHECK(std::wstring(menu_text).find(L"\u6c34\u5e73\u4f4d\u7f6e") != std::wstring::npos);
    CHECK(GetMenuStringW(settings_menu, 3, menu_text, 128, MF_BYPOSITION) > 0);
    CHECK(std::wstring(menu_text).find(L"\u5782\u76f4\u4f4d\u7f6e") != std::wstring::npos);
    const HMENU top_depth_menu = GetSubMenu(settings_menu, 7);
    CHECK(top_depth_menu != nullptr);
    CHECK(GetMenuItemCount(top_depth_menu) == 8);
    CHECK(GetMenuStringW(top_depth_menu,
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
