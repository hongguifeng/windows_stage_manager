#pragma once

#include <cstdint>
#include <filesystem>

namespace stage_manager::app {

struct Settings {
    bool enabled = true;
    bool dryRun = true;

    std::uint32_t minExposedEdgeDip = 48;
    std::uint32_t minExposedDepthDip = 24;
    std::uint32_t repairTargetEdgeDip = 64;
    std::uint32_t minOnscreenWidthDip = 100;
    std::uint32_t minOnscreenHeightDip = 100;

    std::uint32_t eventCoalesceWindowMs = 12;
    std::uint32_t reconcileIntervalMs = 250;
    std::uint32_t maxMovesPerBatch = 32;
    std::uint32_t maxSolverStates = 512;
    std::uint32_t maxSolveTimeMs = 16;
};

std::filesystem::path default_settings_path();
std::filesystem::path default_log_path();

Settings load_settings(const std::filesystem::path& path);
bool save_settings(const Settings& settings, const std::filesystem::path& path);

} // namespace stage_manager::app

