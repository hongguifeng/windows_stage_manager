#include "app/settings.h"

#include <charconv>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

namespace stage_manager::app {
namespace {

std::string trim(std::string value)
{
    const auto is_space = [](unsigned char ch) { return std::isspace(ch) != 0; };

    while (!value.empty() && is_space(static_cast<unsigned char>(value.front()))) {
        value.erase(value.begin());
    }
    while (!value.empty() && is_space(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }
    return value;
}

std::optional<bool> parse_bool(std::string_view value)
{
    if (value == "true" || value == "1") {
        return true;
    }
    if (value == "false" || value == "0") {
        return false;
    }
    return std::nullopt;
}

std::optional<std::uint32_t> parse_uint(std::string_view value)
{
    std::uint32_t parsed = 0;
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return parsed;
}

void load_key(Settings& settings, std::string_view key, std::string_view value)
{
    if (key == "enabled") {
        if (const auto parsed = parse_bool(value)) {
            settings.enabled = *parsed;
        }
    } else if (key == "dry_run") {
        if (const auto parsed = parse_bool(value)) {
            settings.dryRun = *parsed;
        }
    } else if (key == "min_exposed_edge_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.minExposedEdgeDip = *parsed;
        }
    } else if (key == "min_exposed_depth_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.minExposedDepthDip = *parsed;
        }
    } else if (key == "repair_target_edge_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.repairTargetEdgeDip = *parsed;
        }
    } else if (key == "min_onscreen_width_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.minOnscreenWidthDip = *parsed;
        }
    } else if (key == "min_onscreen_height_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.minOnscreenHeightDip = *parsed;
        }
    } else if (key == "event_coalesce_window_ms") {
        if (const auto parsed = parse_uint(value)) {
            settings.eventCoalesceWindowMs = *parsed;
        }
    } else if (key == "reconcile_interval_ms") {
        if (const auto parsed = parse_uint(value)) {
            settings.reconcileIntervalMs = *parsed;
        }
    } else if (key == "max_moves_per_batch") {
        if (const auto parsed = parse_uint(value)) {
            settings.maxMovesPerBatch = *parsed;
        }
    } else if (key == "max_solver_states") {
        if (const auto parsed = parse_uint(value)) {
            settings.maxSolverStates = *parsed;
        }
    } else if (key == "max_solve_time_ms") {
        if (const auto parsed = parse_uint(value)) {
            settings.maxSolveTimeMs = *parsed;
        }
    } else if (key == "max_managed_windows") {
        if (const auto parsed = parse_uint(value)) {
            settings.maxManagedWindows = *parsed;
        }
    } else if (key == "max_consecutive_failures") {
        if (const auto parsed = parse_uint(value)) {
            settings.maxConsecutiveFailures = *parsed;
        }
    }
}

std::filesystem::path local_app_data_path()
{
#ifdef _WIN32
    std::vector<wchar_t> buffer(512);
    while (true) {
        const DWORD length = GetEnvironmentVariableW(
            L"LOCALAPPDATA", buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            break;
        }
        if (length < buffer.size()) {
            return std::filesystem::path(buffer.data());
        }
        buffer.resize(static_cast<std::size_t>(length) + 1);
    }
#endif
    return std::filesystem::current_path() / "local-config";
}

} // namespace

std::filesystem::path default_settings_path()
{
    return local_app_data_path() / "WindowsStageManager" / "settings.ini";
}

std::filesystem::path default_log_path()
{
    return local_app_data_path() / "WindowsStageManager" / "logs" / "manager.log";
}

Settings load_settings(const std::filesystem::path& path)
{
    Settings settings;
    std::ifstream input(path);
    if (!input) {
        return settings;
    }

    std::string line;
    while (std::getline(input, line)) {
        line = trim(std::move(line));
        if (line.empty() || line.front() == '#' || line.front() == ';' || line.front() == '[') {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        const auto key = trim(line.substr(0, separator));
        const auto value = trim(line.substr(separator + 1));
        load_key(settings, key, value);
    }
    return settings;
}

bool save_settings(const Settings& settings, const std::filesystem::path& path)
{
    std::error_code error;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            return false;
        }
    }

    auto temporary_path = path;
    temporary_path += ".tmp";
    std::ofstream output(temporary_path, std::ios::trunc);
    if (!output) {
        return false;
    }

    output << "enabled=" << (settings.enabled ? "true" : "false") << '\n'
           << "dry_run=" << (settings.dryRun ? "true" : "false") << '\n'
           << "min_exposed_edge_dip=" << settings.minExposedEdgeDip << '\n'
           << "min_exposed_depth_dip=" << settings.minExposedDepthDip << '\n'
           << "repair_target_edge_dip=" << settings.repairTargetEdgeDip << '\n'
           << "min_onscreen_width_dip=" << settings.minOnscreenWidthDip << '\n'
           << "min_onscreen_height_dip=" << settings.minOnscreenHeightDip << '\n'
           << "event_coalesce_window_ms=" << settings.eventCoalesceWindowMs << '\n'
           << "reconcile_interval_ms=" << settings.reconcileIntervalMs << '\n'
           << "max_moves_per_batch=" << settings.maxMovesPerBatch << '\n'
           << "max_solver_states=" << settings.maxSolverStates << '\n'
           << "max_solve_time_ms=" << settings.maxSolveTimeMs << '\n'
           << "max_managed_windows=" << settings.maxManagedWindows << '\n'
           << "max_consecutive_failures=" << settings.maxConsecutiveFailures << '\n';
    output.flush();
    if (!output) {
        return false;
    }
    output.close();

    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary_path, path, error);
    if (error) {
        std::filesystem::remove(temporary_path, error);
        return false;
    }
    return true;
}

} // namespace stage_manager::app
