#include "app/settings.h"
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

    stage_manager::app::Settings expected;
    expected.enabled = false;
    expected.dryRun = false;
    expected.minExposedEdgeDip = 56;
    expected.maxSolverStates = 99;
    expected.maxManagedWindows = 12;

    const auto settings_path = root / "settings.ini";
    assert(stage_manager::app::save_settings(expected, settings_path));
    const auto actual = stage_manager::app::load_settings(settings_path);
    assert(actual.enabled == expected.enabled);
    assert(actual.dryRun == expected.dryRun);
    assert(actual.minExposedEdgeDip == expected.minExposedEdgeDip);
    assert(actual.maxSolverStates == expected.maxSolverStates);
    assert(actual.maxManagedWindows == expected.maxManagedWindows);

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
