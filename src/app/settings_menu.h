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
    MinimumExposedEdgeDip,
    MinimumExposedDepthDip,
    PreferredExposedEdges,
    MinimumExposedEdges,
    RepairTargetEdgeDip,
    MinimumOnscreenWidthDip,
    MinimumOnscreenHeightDip,
    EventCoalesceWindowMs,
    ReconcileIntervalMs,
    MaximumMovesPerBatch,
    MaximumSolverStates,
    MaximumSolveTimeMs,
    MaximumManagedWindows,
    MaximumConsecutiveFailures,
    Count,
};

struct SettingSelection {
    SettingField field = SettingField::DryRun;
    std::uint32_t value = 0;

    constexpr bool operator==(const SettingSelection&) const noexcept = default;
};

inline constexpr std::uint32_t kSettingCommandBase = 2000;
inline constexpr std::uint32_t kSettingCommandStride = 16;

std::span<const SettingField> setting_fields() noexcept;
std::span<const std::uint32_t> setting_choices(SettingField field) noexcept;
std::string_view setting_field_name(SettingField field) noexcept;
std::uint32_t current_setting_value(const Settings& settings, SettingField field) noexcept;
std::uint32_t setting_command_id(SettingField field, std::size_t choice_index) noexcept;
std::optional<SettingSelection> decode_setting_command(std::uint32_t command) noexcept;

// Applies a menu choice and keeps dependent settings internally consistent.
// Returns false for values that are not exposed by the menu model.
bool apply_setting_selection(Settings& settings, const SettingSelection& selection) noexcept;

} // namespace stage_manager::app
