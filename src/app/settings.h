#pragma once

#include <cstdint>
#include <filesystem>

namespace stage_manager::app {

enum class AffordancePreset : std::uint32_t {
    Compact = 0,
    Balanced = 1,
    Prominent = 2,
    Custom = 3,
};

enum class ActivationHorizontalAlignment : std::uint32_t {
    Left = 0,
    Center = 1,
    Right = 2,
};

enum class ActivationVerticalAlignment : std::uint32_t {
    Top = 0,
    Center = 1,
    Bottom = 2,
};

struct Settings {
    bool enabled = true;
    bool dryRun = false;
    bool placeActivatedWindow = true;
    ActivationHorizontalAlignment activationHorizontalAlignment =
        ActivationHorizontalAlignment::Center;
    ActivationVerticalAlignment activationVerticalAlignment =
        ActivationVerticalAlignment::Bottom;
    AffordancePreset affordancePreset = AffordancePreset::Balanced;

    std::uint32_t topMinimumLengthDip = 120;
    std::uint32_t topMaximumLengthDip = 240;
    std::uint32_t topDepthDip = 32;
    std::uint32_t topLengthPercent = 25;
    std::uint32_t leftMinimumLengthDip = 120;
    std::uint32_t leftMaximumLengthDip = 240;
    std::uint32_t leftDepthDip = 40;
    std::uint32_t leftLengthPercent = 25;
    std::uint32_t rightMinimumLengthDip = 160;
    std::uint32_t rightMaximumLengthDip = 300;
    std::uint32_t rightDepthDip = 64;
    std::uint32_t rightLengthPercent = 30;
    std::uint32_t bottomMinimumLengthDip = 180;
    std::uint32_t bottomMaximumLengthDip = 360;
    std::uint32_t bottomDepthDip = 64;
    std::uint32_t bottomLengthPercent = 35;
    std::uint32_t minOnscreenWidthDip = 100;
    std::uint32_t minOnscreenHeightDip = 100;

    std::uint32_t eventCoalesceWindowMs = 12;
    std::uint32_t reconcileIntervalMs = 250;
    std::uint32_t maxMovesPerBatch = 32;
    std::uint32_t maxSolverStates = 512;
    std::uint32_t maxSolveTimeMs = 16;
    std::uint32_t maxManagedWindows = 20;
    std::uint32_t maxConsecutiveFailures = 3;

    constexpr bool operator==(const Settings&) const noexcept = default;
};

void apply_affordance_preset(Settings& settings, AffordancePreset preset) noexcept;

std::filesystem::path default_settings_path();
std::filesystem::path default_log_path();

Settings load_settings(const std::filesystem::path& path);
bool save_settings(const Settings& settings, const std::filesystem::path& path);

} // namespace stage_manager::app
