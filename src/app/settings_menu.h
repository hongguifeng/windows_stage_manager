#pragma once

#include "app/settings.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace stage_manager::app {

enum class SettingField : std::uint8_t {
    DryRun,
    PlaceActivatedWindow,
    AffordancePreset,
    TopMinimumLengthDip,
    TopMaximumLengthDip,
    TopDepthDip,
    TopLengthPercent,
    LeftMinimumLengthDip,
    LeftMaximumLengthDip,
    LeftDepthDip,
    LeftLengthPercent,
    RightMinimumLengthDip,
    RightMaximumLengthDip,
    RightDepthDip,
    RightLengthPercent,
    BottomMinimumLengthDip,
    BottomMaximumLengthDip,
    BottomDepthDip,
    BottomLengthPercent,
    MinimumOnscreenWidthDip,
    MinimumOnscreenHeightDip,
    EventCoalesceWindowMs,
    ReconcileIntervalMs,
    MaximumMovesPerBatch,
    MaximumSolverStates,
    MaximumSolveTimeMs,
    MaximumManagedWindows,
    MaximumConsecutiveFailures,
    ActivationHorizontalAlignment,
    ActivationVerticalAlignment,
    Count,
};

struct SettingSelection {
    SettingField field = SettingField::DryRun;
    std::uint32_t value = 0;

    constexpr bool operator==(const SettingSelection&) const noexcept = default;
};

struct SettingValueRange {
    std::uint32_t minimum = 0;
    std::uint32_t maximum = 0;

    constexpr bool operator==(const SettingValueRange&) const noexcept = default;
};

inline constexpr std::uint32_t kSettingCommandBase = 2000;
inline constexpr std::uint32_t kSettingCommandStride = 16;

std::span<const SettingField> setting_fields() noexcept;
std::span<const std::uint32_t> setting_choices(SettingField field) noexcept;
std::string_view setting_field_name(SettingField field) noexcept;
std::uint32_t current_setting_value(const Settings& settings, SettingField field) noexcept;
std::uint32_t setting_command_id(SettingField field, std::size_t choice_index) noexcept;
std::optional<SettingSelection> decode_setting_command(std::uint32_t command) noexcept;
std::optional<SettingValueRange> custom_setting_range(SettingField field) noexcept;
std::uint32_t custom_setting_command_id(SettingField field) noexcept;
std::optional<SettingField> decode_custom_setting_command(std::uint32_t command) noexcept;

// Applies a menu choice and keeps dependent settings internally consistent.
// Returns false for values that are not exposed by the menu model.
bool apply_setting_selection(Settings& settings, const SettingSelection& selection) noexcept;
bool apply_custom_setting_selection(
    Settings& settings, const SettingSelection& selection) noexcept;
bool apply_setting_input(Settings& settings, const SettingSelection& selection) noexcept;

} // namespace stage_manager::app
