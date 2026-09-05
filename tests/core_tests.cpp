#include "app/settings.h"
#include "app/settings_menu.h"
#include "diagnostics/logger.h"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "windows_stage_manager_core_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto defaults = stage_manager::app::load_settings(root / "missing.ini");
    if (defaults.dryRun) {
        return 1;
    }
    CHECK(defaults.affordancePreset == stage_manager::app::AffordancePreset::Balanced);
    CHECK(defaults.placeActivatedWindow);
    CHECK(defaults.activationHorizontalAlignment ==
          stage_manager::app::ActivationHorizontalAlignment::Center);
    CHECK(defaults.activationVerticalAlignment ==
          stage_manager::app::ActivationVerticalAlignment::Bottom);
    CHECK(defaults.topDepthDip < defaults.leftDepthDip);
    CHECK(defaults.leftDepthDip < defaults.rightDepthDip);
    CHECK(defaults.bottomMinimumLengthDip > defaults.topMinimumLengthDip);

    for (const auto field : stage_manager::app::setting_fields()) {
        const auto choices = stage_manager::app::setting_choices(field);
        CHECK(!choices.empty());
        for (std::size_t index = 0; index < choices.size(); ++index) {
            const auto command = stage_manager::app::setting_command_id(field, index);
            const auto decoded = stage_manager::app::decode_setting_command(command);
            CHECK(decoded.has_value());
            CHECK(decoded->field == field);
            CHECK(decoded->value == choices[index]);
        }
        const auto custom_range = stage_manager::app::custom_setting_range(field);
        const auto custom_command = stage_manager::app::custom_setting_command_id(field);
        const auto custom_field =
            stage_manager::app::decode_custom_setting_command(custom_command);
        CHECK(custom_field.has_value() == custom_range.has_value());
        if (custom_field) {
            CHECK(*custom_field == field);
            CHECK(custom_range->minimum <= custom_range->maximum);
        }
    }
    CHECK(!stage_manager::app::decode_setting_command(1999));
    CHECK(!stage_manager::app::decode_setting_command(
        stage_manager::app::kSettingCommandBase +
        static_cast<std::uint32_t>(stage_manager::app::SettingField::Count) *
            stage_manager::app::kSettingCommandStride));

    auto menu_settings = defaults;
    using stage_manager::app::SettingField;
    using stage_manager::app::SettingSelection;
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::DryRun, 1}));
    CHECK(menu_settings.dryRun);
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::PlaceActivatedWindow, 0}));
    CHECK(!menu_settings.placeActivatedWindow);
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings,
        SettingSelection{SettingField::ActivationHorizontalAlignment, 2}));
    CHECK(menu_settings.activationHorizontalAlignment ==
          stage_manager::app::ActivationHorizontalAlignment::Right);
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings,
        SettingSelection{SettingField::ActivationVerticalAlignment, 1}));
    CHECK(menu_settings.activationVerticalAlignment ==
          stage_manager::app::ActivationVerticalAlignment::Center);
    CHECK(!stage_manager::app::apply_setting_selection(
        menu_settings,
        SettingSelection{SettingField::ActivationVerticalAlignment, 3}));
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::AffordancePreset, 2}));
    CHECK(menu_settings.affordancePreset == stage_manager::app::AffordancePreset::Prominent);
    CHECK(menu_settings.rightDepthDip == 80);
    CHECK(!stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::MaximumManagedWindows, 99}));
    CHECK(stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::TopDepthDip, 97}));
    CHECK(menu_settings.topDepthDip == 97);
    CHECK(menu_settings.affordancePreset == stage_manager::app::AffordancePreset::Custom);
    CHECK(stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::MaximumManagedWindows, 13}));
    CHECK(menu_settings.maxManagedWindows == 13);
    CHECK(!stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::MaximumManagedWindows, 21}));
    CHECK(!stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::DryRun, 1}));
    CHECK(stage_manager::app::apply_setting_input(
        menu_settings, SettingSelection{SettingField::DryRun, 0}));
    CHECK(!menu_settings.dryRun);
    CHECK(stage_manager::app::apply_setting_input(
        menu_settings, SettingSelection{SettingField::TopMinimumLengthDip, 301}));
    CHECK(menu_settings.topMinimumLengthDip == 301);
    CHECK(menu_settings.topMaximumLengthDip == 301);
    CHECK(!stage_manager::app::apply_setting_input(
        menu_settings, SettingSelection{SettingField::TopLengthPercent, 101}));

    stage_manager::app::Settings expected;
    expected.enabled = false;
    expected.dryRun = false;
    expected.placeActivatedWindow = false;
    expected.activationHorizontalAlignment =
        stage_manager::app::ActivationHorizontalAlignment::Right;
    expected.activationVerticalAlignment =
        stage_manager::app::ActivationVerticalAlignment::Top;
    expected.affordancePreset = stage_manager::app::AffordancePreset::Custom;
    expected.topMinimumLengthDip = 156;
    expected.topMaximumLengthDip = 256;
    expected.rightDepthDip = 72;
    expected.bottomLengthPercent = 42;
    expected.maxSolverStates = 99;
    expected.maxManagedWindows = 12;
    expected.maxConsecutiveFailures = 5;

    const auto settings_path = root / "settings.ini";
    CHECK(stage_manager::app::save_settings(expected, settings_path));
    const auto actual = stage_manager::app::load_settings(settings_path);
    CHECK(actual.enabled == expected.enabled);
    CHECK(actual.dryRun == expected.dryRun);
    CHECK(actual.placeActivatedWindow == expected.placeActivatedWindow);
    CHECK(actual.activationHorizontalAlignment == expected.activationHorizontalAlignment);
    CHECK(actual.activationVerticalAlignment == expected.activationVerticalAlignment);
    CHECK(actual.affordancePreset == expected.affordancePreset);
    CHECK(actual.topMinimumLengthDip == expected.topMinimumLengthDip);
    CHECK(actual.topMaximumLengthDip == expected.topMaximumLengthDip);
    CHECK(actual.rightDepthDip == expected.rightDepthDip);
    CHECK(actual.bottomLengthPercent == expected.bottomLengthPercent);
    CHECK(actual.maxSolverStates == expected.maxSolverStates);
    CHECK(actual.maxManagedWindows == expected.maxManagedWindows);
    CHECK(actual.maxConsecutiveFailures == expected.maxConsecutiveFailures);

    const auto log_path = root / "logs" / "manager.log";
    auto& logger = stage_manager::diagnostics::Logger::instance();
    CHECK(logger.initialize(log_path));
    logger.log(stage_manager::diagnostics::LogLevel::Info,
               "core_test",
               {{"batch", "test"}, {"message", "quoted \"value\""}});
    logger.shutdown();

    std::ifstream log_input(log_path);
    const std::string log_content((std::istreambuf_iterator<char>(log_input)),
                                  std::istreambuf_iterator<char>());
    CHECK(log_content.find("level=info") != std::string::npos);
    CHECK(log_content.find("batch=\"test\"") != std::string::npos);
    CHECK(log_content.find("quoted \\\"value\\\"") != std::string::npos);

    std::filesystem::remove_all(root, error);
    return 0;
}
