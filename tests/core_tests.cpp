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
    CHECK(defaults.preferredExposedEdges == 2);
    CHECK(defaults.minimumExposedEdges == 1);
    CHECK(defaults.centerActivatedWindow);

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
        menu_settings, SettingSelection{SettingField::CenterActivatedWindow, 0}));
    CHECK(!menu_settings.centerActivatedWindow);
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::MinimumExposedEdges, 4}));
    CHECK(menu_settings.minimumExposedEdges == 4);
    CHECK(menu_settings.preferredExposedEdges == 4);
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::PreferredExposedEdges, 2}));
    CHECK(menu_settings.preferredExposedEdges == 2);
    CHECK(menu_settings.minimumExposedEdges == 2);
    CHECK(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::MinimumExposedEdgeDip, 96}));
    CHECK(menu_settings.repairTargetEdgeDip >= menu_settings.minExposedEdgeDip);
    CHECK(!stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::MaximumManagedWindows, 99}));
    CHECK(stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::MinimumExposedEdgeDip, 97}));
    CHECK(menu_settings.minExposedEdgeDip == 97);
    CHECK(menu_settings.repairTargetEdgeDip == 97);
    CHECK(stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::MaximumManagedWindows, 13}));
    CHECK(menu_settings.maxManagedWindows == 13);
    CHECK(!stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::MaximumManagedWindows, 21}));
    CHECK(!stage_manager::app::apply_custom_setting_selection(
        menu_settings, SettingSelection{SettingField::DryRun, 1}));

    stage_manager::app::Settings expected;
    expected.enabled = false;
    expected.dryRun = false;
    expected.centerActivatedWindow = false;
    expected.minExposedEdgeDip = 56;
    expected.preferredExposedEdges = 3;
    expected.minimumExposedEdges = 2;
    expected.maxSolverStates = 99;
    expected.maxManagedWindows = 12;
    expected.maxConsecutiveFailures = 5;

    const auto settings_path = root / "settings.ini";
    CHECK(stage_manager::app::save_settings(expected, settings_path));
    const auto actual = stage_manager::app::load_settings(settings_path);
    CHECK(actual.enabled == expected.enabled);
    CHECK(actual.dryRun == expected.dryRun);
    CHECK(actual.centerActivatedWindow == expected.centerActivatedWindow);
    CHECK(actual.minExposedEdgeDip == expected.minExposedEdgeDip);
    CHECK(actual.preferredExposedEdges == expected.preferredExposedEdges);
    CHECK(actual.minimumExposedEdges == expected.minimumExposedEdges);
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
