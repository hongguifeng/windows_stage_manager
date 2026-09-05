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
    } else if (key == "place_activated_window") {
        if (const auto parsed = parse_bool(value)) {
            settings.placeActivatedWindow = *parsed;
        }
    } else if (key == "activation_horizontal_alignment") {
        if (const auto parsed = parse_uint(value);
            parsed && *parsed <= static_cast<std::uint32_t>(
                ActivationHorizontalAlignment::Right)) {
            settings.activationHorizontalAlignment =
                static_cast<ActivationHorizontalAlignment>(*parsed);
        }
    } else if (key == "activation_vertical_alignment") {
        if (const auto parsed = parse_uint(value);
            parsed && *parsed <= static_cast<std::uint32_t>(
                ActivationVerticalAlignment::Bottom)) {
            settings.activationVerticalAlignment =
                static_cast<ActivationVerticalAlignment>(*parsed);
        }
    } else if (key == "affordance_preset") {
        if (const auto parsed = parse_uint(value)) {
            if (*parsed <= static_cast<std::uint32_t>(AffordancePreset::Custom)) {
                apply_affordance_preset(settings, static_cast<AffordancePreset>(*parsed));
            }
        }
    } else if (key == "top_minimum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.topMinimumLengthDip = *parsed;
        }
    } else if (key == "top_maximum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.topMaximumLengthDip = *parsed;
        }
    } else if (key == "top_depth_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.topDepthDip = *parsed;
        }
    } else if (key == "top_length_percent") {
        if (const auto parsed = parse_uint(value)) {
            settings.topLengthPercent = *parsed;
        }
    } else if (key == "left_minimum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.leftMinimumLengthDip = *parsed;
        }
    } else if (key == "left_maximum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.leftMaximumLengthDip = *parsed;
        }
    } else if (key == "left_depth_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.leftDepthDip = *parsed;
        }
    } else if (key == "left_length_percent") {
        if (const auto parsed = parse_uint(value)) {
            settings.leftLengthPercent = *parsed;
        }
    } else if (key == "right_minimum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.rightMinimumLengthDip = *parsed;
        }
    } else if (key == "right_maximum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.rightMaximumLengthDip = *parsed;
        }
    } else if (key == "right_depth_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.rightDepthDip = *parsed;
        }
    } else if (key == "right_length_percent") {
        if (const auto parsed = parse_uint(value)) {
            settings.rightLengthPercent = *parsed;
        }
    } else if (key == "bottom_minimum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.bottomMinimumLengthDip = *parsed;
        }
    } else if (key == "bottom_maximum_length_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.bottomMaximumLengthDip = *parsed;
        }
    } else if (key == "bottom_depth_dip") {
        if (const auto parsed = parse_uint(value)) {
            settings.bottomDepthDip = *parsed;
        }
    } else if (key == "bottom_length_percent") {
        if (const auto parsed = parse_uint(value)) {
            settings.bottomLengthPercent = *parsed;
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

void apply_affordance_preset(Settings& settings, AffordancePreset preset) noexcept
{
    settings.affordancePreset = preset;
    switch (preset) {
    case AffordancePreset::Compact:
        settings.topMinimumLengthDip = 96;
        settings.topMaximumLengthDip = 180;
        settings.topDepthDip = 28;
        settings.topLengthPercent = 20;
        settings.leftMinimumLengthDip = 96;
        settings.leftMaximumLengthDip = 180;
        settings.leftDepthDip = 32;
        settings.leftLengthPercent = 20;
        settings.rightMinimumLengthDip = 128;
        settings.rightMaximumLengthDip = 240;
        settings.rightDepthDip = 48;
        settings.rightLengthPercent = 25;
        settings.bottomMinimumLengthDip = 144;
        settings.bottomMaximumLengthDip = 280;
        settings.bottomDepthDip = 48;
        settings.bottomLengthPercent = 30;
        break;
    case AffordancePreset::Balanced:
        settings.topMinimumLengthDip = 120;
        settings.topMaximumLengthDip = 240;
        settings.topDepthDip = 32;
        settings.topLengthPercent = 25;
        settings.leftMinimumLengthDip = 120;
        settings.leftMaximumLengthDip = 240;
        settings.leftDepthDip = 40;
        settings.leftLengthPercent = 25;
        settings.rightMinimumLengthDip = 160;
        settings.rightMaximumLengthDip = 300;
        settings.rightDepthDip = 64;
        settings.rightLengthPercent = 30;
        settings.bottomMinimumLengthDip = 180;
        settings.bottomMaximumLengthDip = 360;
        settings.bottomDepthDip = 64;
        settings.bottomLengthPercent = 35;
        break;
    case AffordancePreset::Prominent:
        settings.topMinimumLengthDip = 160;
        settings.topMaximumLengthDip = 300;
        settings.topDepthDip = 40;
        settings.topLengthPercent = 30;
        settings.leftMinimumLengthDip = 160;
        settings.leftMaximumLengthDip = 300;
        settings.leftDepthDip = 56;
        settings.leftLengthPercent = 30;
        settings.rightMinimumLengthDip = 220;
        settings.rightMaximumLengthDip = 380;
        settings.rightDepthDip = 80;
        settings.rightLengthPercent = 35;
        settings.bottomMinimumLengthDip = 240;
        settings.bottomMaximumLengthDip = 420;
        settings.bottomDepthDip = 88;
        settings.bottomLengthPercent = 40;
        break;
    case AffordancePreset::Custom:
        break;
    }
}

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
           << "place_activated_window="
           << (settings.placeActivatedWindow ? "true" : "false") << '\n'
           << "activation_horizontal_alignment=" << static_cast<std::uint32_t>(
                  settings.activationHorizontalAlignment) << '\n'
           << "activation_vertical_alignment=" << static_cast<std::uint32_t>(
                  settings.activationVerticalAlignment) << '\n'
           << "affordance_preset=" << static_cast<std::uint32_t>(
                  settings.affordancePreset) << '\n'
           << "top_minimum_length_dip=" << settings.topMinimumLengthDip << '\n'
           << "top_maximum_length_dip=" << settings.topMaximumLengthDip << '\n'
           << "top_depth_dip=" << settings.topDepthDip << '\n'
           << "top_length_percent=" << settings.topLengthPercent << '\n'
           << "left_minimum_length_dip=" << settings.leftMinimumLengthDip << '\n'
           << "left_maximum_length_dip=" << settings.leftMaximumLengthDip << '\n'
           << "left_depth_dip=" << settings.leftDepthDip << '\n'
           << "left_length_percent=" << settings.leftLengthPercent << '\n'
           << "right_minimum_length_dip=" << settings.rightMinimumLengthDip << '\n'
           << "right_maximum_length_dip=" << settings.rightMaximumLengthDip << '\n'
           << "right_depth_dip=" << settings.rightDepthDip << '\n'
           << "right_length_percent=" << settings.rightLengthPercent << '\n'
           << "bottom_minimum_length_dip=" << settings.bottomMinimumLengthDip << '\n'
           << "bottom_maximum_length_dip=" << settings.bottomMaximumLengthDip << '\n'
           << "bottom_depth_dip=" << settings.bottomDepthDip << '\n'
           << "bottom_length_percent=" << settings.bottomLengthPercent << '\n'
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
