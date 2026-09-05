#include "app/settings.h"
#include "app/settings_menu.h"
#include "diagnostics/logger.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

int main()
{
    const auto root = std::filesystem::temp_directory_path() / "windows_stage_manager_core_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);

    const auto defaults = stage_manager::app::load_settings(root / "missing.ini");
    if (defaults.dryRun) {
        return 1;
    }
    assert(defaults.preferredExposedEdges == 2);
    assert(defaults.minimumExposedEdges == 1);

    for (const auto field : stage_manager::app::setting_fields()) {
        const auto choices = stage_manager::app::setting_choices(field);
        assert(!choices.empty());
        for (std::size_t index = 0; index < choices.size(); ++index) {
            const auto command = stage_manager::app::setting_command_id(field, index);
            const auto decoded = stage_manager::app::decode_setting_command(command);
            assert(decoded.has_value());
            assert(decoded->field == field);
            assert(decoded->value == choices[index]);
        }
    }
    assert(!stage_manager::app::decode_setting_command(1999));
    assert(!stage_manager::app::decode_setting_command(
        stage_manager::app::kSettingCommandBase +
        static_cast<std::uint32_t>(stage_manager::app::SettingField::Count) *
            stage_manager::app::kSettingCommandStride));

    auto menu_settings = defaults;
    using stage_manager::app::SettingField;
    using stage_manager::app::SettingSelection;
    assert(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::DryRun, 1}));
    assert(menu_settings.dryRun);
    assert(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::MinimumExposedEdges, 4}));
    assert(menu_settings.minimumExposedEdges == 4);
    assert(menu_settings.preferredExposedEdges == 4);
    assert(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::PreferredExposedEdges, 2}));
    assert(menu_settings.preferredExposedEdges == 2);
    assert(menu_settings.minimumExposedEdges == 2);
    assert(stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::MinimumExposedEdgeDip, 96}));
    assert(menu_settings.repairTargetEdgeDip >= menu_settings.minExposedEdgeDip);
    assert(!stage_manager::app::apply_setting_selection(
        menu_settings, SettingSelection{SettingField::MaximumManagedWindows, 99}));

    stage_manager::app::Settings expected;
    expected.enabled = false;
    expected.dryRun = false;
    expected.minExposedEdgeDip = 56;
    expected.preferredExposedEdges = 3;
    expected.minimumExposedEdges = 2;
    expected.maxSolverStates = 99;
    expected.maxManagedWindows = 12;
    expected.maxConsecutiveFailures = 5;

    const auto settings_path = root / "settings.ini";
    assert(stage_manager::app::save_settings(expected, settings_path));
    const auto actual = stage_manager::app::load_settings(settings_path);
    assert(actual.enabled == expected.enabled);
    assert(actual.dryRun == expected.dryRun);
    assert(actual.minExposedEdgeDip == expected.minExposedEdgeDip);
    assert(actual.preferredExposedEdges == expected.preferredExposedEdges);
    assert(actual.minimumExposedEdges == expected.minimumExposedEdges);
    assert(actual.maxSolverStates == expected.maxSolverStates);
    assert(actual.maxManagedWindows == expected.maxManagedWindows);
    assert(actual.maxConsecutiveFailures == expected.maxConsecutiveFailures);

    const auto log_path = root / "logs" / "manager.log";
    auto& logger = stage_manager::diagnostics::Logger::instance();
    assert(logger.initialize(log_path));
    logger.log(stage_manager::diagnostics::LogLevel::Info,
               "core_test",
               {{"batch", "test"}, {"message", "quoted \"value\""}});
    logger.shutdown();

    std::ifstream log_input(log_path);
    const std::string log_content((std::istreambuf_iterator<char>(log_input)),
                                  std::istreambuf_iterator<char>());
    assert(log_content.find("level=info") != std::string::npos);
    assert(log_content.find("batch=\"test\"") != std::string::npos);
    assert(log_content.find("quoted \\\"value\\\"") != std::string::npos);

    std::filesystem::remove_all(root, error);
    return 0;
}
