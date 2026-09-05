#include "app/settings_menu.h"

#include <algorithm>
#include <array>

namespace stage_manager::app {
namespace {

constexpr std::array kFields = {
    SettingField::DryRun,
    SettingField::CenterActivatedWindow,
    SettingField::MinimumExposedEdgeDip,
    SettingField::MinimumExposedDepthDip,
    SettingField::PreferredExposedEdges,
    SettingField::MinimumExposedEdges,
    SettingField::RepairTargetEdgeDip,
    SettingField::MinimumOnscreenWidthDip,
    SettingField::MinimumOnscreenHeightDip,
    SettingField::EventCoalesceWindowMs,
    SettingField::ReconcileIntervalMs,
    SettingField::MaximumMovesPerBatch,
    SettingField::MaximumSolverStates,
    SettingField::MaximumSolveTimeMs,
    SettingField::MaximumManagedWindows,
    SettingField::MaximumConsecutiveFailures,
};

constexpr std::array<std::uint32_t, 2> kBooleanChoices = {0, 1};
constexpr std::array<std::uint32_t, 5> kEdgeLengthChoices = {32, 48, 64, 80, 96};
constexpr std::array<std::uint32_t, 4> kEdgeDepthChoices = {16, 24, 32, 48};
constexpr std::array<std::uint32_t, 4> kEdgeCountChoices = {1, 2, 3, 4};
constexpr std::array<std::uint32_t, 5> kRepairLengthChoices = {48, 64, 80, 96, 128};
constexpr std::array<std::uint32_t, 4> kOnscreenChoices = {64, 100, 160, 240};
constexpr std::array<std::uint32_t, 5> kCoalesceChoices = {8, 12, 16, 24, 32};
constexpr std::array<std::uint32_t, 4> kReconcileChoices = {100, 250, 500, 1000};
constexpr std::array<std::uint32_t, 4> kMoveChoices = {8, 16, 32, 64};
constexpr std::array<std::uint32_t, 4> kStateChoices = {128, 256, 512, 1024};
constexpr std::array<std::uint32_t, 5> kSolveTimeChoices = {8, 16, 32, 64, 100};
constexpr std::array<std::uint32_t, 5> kWindowCountChoices = {2, 5, 10, 15, 20};
constexpr std::array<std::uint32_t, 4> kFailureChoices = {1, 3, 5, 10};

bool contains_choice(SettingField field, std::uint32_t value) noexcept
{
    const auto choices = setting_choices(field);
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

} // namespace

std::span<const SettingField> setting_fields() noexcept
{
    return kFields;
}

std::span<const std::uint32_t> setting_choices(SettingField field) noexcept
{
    switch (field) {
    case SettingField::DryRun:
    case SettingField::CenterActivatedWindow:
        return kBooleanChoices;
    case SettingField::MinimumExposedEdgeDip:
        return kEdgeLengthChoices;
    case SettingField::MinimumExposedDepthDip:
        return kEdgeDepthChoices;
    case SettingField::PreferredExposedEdges:
    case SettingField::MinimumExposedEdges:
        return kEdgeCountChoices;
    case SettingField::RepairTargetEdgeDip:
        return kRepairLengthChoices;
    case SettingField::MinimumOnscreenWidthDip:
    case SettingField::MinimumOnscreenHeightDip:
        return kOnscreenChoices;
    case SettingField::EventCoalesceWindowMs:
        return kCoalesceChoices;
    case SettingField::ReconcileIntervalMs:
        return kReconcileChoices;
    case SettingField::MaximumMovesPerBatch:
        return kMoveChoices;
    case SettingField::MaximumSolverStates:
        return kStateChoices;
    case SettingField::MaximumSolveTimeMs:
        return kSolveTimeChoices;
    case SettingField::MaximumManagedWindows:
        return kWindowCountChoices;
    case SettingField::MaximumConsecutiveFailures:
        return kFailureChoices;
    case SettingField::Count:
        return {};
    }
    return {};
}

std::string_view setting_field_name(SettingField field) noexcept
{
    switch (field) {
    case SettingField::DryRun:
        return "dry_run";
    case SettingField::CenterActivatedWindow:
        return "center_activated_window";
    case SettingField::MinimumExposedEdgeDip:
        return "min_exposed_edge_dip";
    case SettingField::MinimumExposedDepthDip:
        return "min_exposed_depth_dip";
    case SettingField::PreferredExposedEdges:
        return "preferred_exposed_edges";
    case SettingField::MinimumExposedEdges:
        return "minimum_exposed_edges";
    case SettingField::RepairTargetEdgeDip:
        return "repair_target_edge_dip";
    case SettingField::MinimumOnscreenWidthDip:
        return "min_onscreen_width_dip";
    case SettingField::MinimumOnscreenHeightDip:
        return "min_onscreen_height_dip";
    case SettingField::EventCoalesceWindowMs:
        return "event_coalesce_window_ms";
    case SettingField::ReconcileIntervalMs:
        return "reconcile_interval_ms";
    case SettingField::MaximumMovesPerBatch:
        return "max_moves_per_batch";
    case SettingField::MaximumSolverStates:
        return "max_solver_states";
    case SettingField::MaximumSolveTimeMs:
        return "max_solve_time_ms";
    case SettingField::MaximumManagedWindows:
        return "max_managed_windows";
    case SettingField::MaximumConsecutiveFailures:
        return "max_consecutive_failures";
    case SettingField::Count:
        return "unknown";
    }
    return "unknown";
}

std::uint32_t current_setting_value(const Settings& settings, SettingField field) noexcept
{
    switch (field) {
    case SettingField::DryRun:
        return settings.dryRun ? 1u : 0u;
    case SettingField::CenterActivatedWindow:
        return settings.centerActivatedWindow ? 1u : 0u;
    case SettingField::MinimumExposedEdgeDip:
        return settings.minExposedEdgeDip;
    case SettingField::MinimumExposedDepthDip:
        return settings.minExposedDepthDip;
    case SettingField::PreferredExposedEdges:
        return settings.preferredExposedEdges;
    case SettingField::MinimumExposedEdges:
        return settings.minimumExposedEdges;
    case SettingField::RepairTargetEdgeDip:
        return settings.repairTargetEdgeDip;
    case SettingField::MinimumOnscreenWidthDip:
        return settings.minOnscreenWidthDip;
    case SettingField::MinimumOnscreenHeightDip:
        return settings.minOnscreenHeightDip;
    case SettingField::EventCoalesceWindowMs:
        return settings.eventCoalesceWindowMs;
    case SettingField::ReconcileIntervalMs:
        return settings.reconcileIntervalMs;
    case SettingField::MaximumMovesPerBatch:
        return settings.maxMovesPerBatch;
    case SettingField::MaximumSolverStates:
        return settings.maxSolverStates;
    case SettingField::MaximumSolveTimeMs:
        return settings.maxSolveTimeMs;
    case SettingField::MaximumManagedWindows:
        return settings.maxManagedWindows;
    case SettingField::MaximumConsecutiveFailures:
        return settings.maxConsecutiveFailures;
    case SettingField::Count:
        return 0;
    }
    return 0;
}

std::uint32_t setting_command_id(SettingField field, std::size_t choice_index) noexcept
{
    return kSettingCommandBase +
        static_cast<std::uint32_t>(field) * kSettingCommandStride +
        static_cast<std::uint32_t>(choice_index);
}

std::optional<SettingSelection> decode_setting_command(std::uint32_t command) noexcept
{
    if (command < kSettingCommandBase) {
        return std::nullopt;
    }
    const auto offset = command - kSettingCommandBase;
    const auto field_value = offset / kSettingCommandStride;
    const auto choice_index = offset % kSettingCommandStride;
    if (field_value >= static_cast<std::uint32_t>(SettingField::Count)) {
        return std::nullopt;
    }
    const auto field = static_cast<SettingField>(field_value);
    const auto choices = setting_choices(field);
    if (choice_index >= choices.size()) {
        return std::nullopt;
    }
    return SettingSelection{field, choices[choice_index]};
}

bool apply_setting_selection(Settings& settings, const SettingSelection& selection) noexcept
{
    if (!contains_choice(selection.field, selection.value)) {
        return false;
    }
    switch (selection.field) {
    case SettingField::DryRun:
        settings.dryRun = selection.value != 0;
        break;
    case SettingField::CenterActivatedWindow:
        settings.centerActivatedWindow = selection.value != 0;
        break;
    case SettingField::MinimumExposedEdgeDip:
        settings.minExposedEdgeDip = selection.value;
        settings.repairTargetEdgeDip = std::max(
            settings.repairTargetEdgeDip, settings.minExposedEdgeDip);
        break;
    case SettingField::MinimumExposedDepthDip:
        settings.minExposedDepthDip = selection.value;
        break;
    case SettingField::PreferredExposedEdges:
        settings.preferredExposedEdges = selection.value;
        settings.minimumExposedEdges = std::min(
            settings.minimumExposedEdges, settings.preferredExposedEdges);
        break;
    case SettingField::MinimumExposedEdges:
        settings.minimumExposedEdges = selection.value;
        settings.preferredExposedEdges = std::max(
            settings.preferredExposedEdges, settings.minimumExposedEdges);
        break;
    case SettingField::RepairTargetEdgeDip:
        settings.repairTargetEdgeDip = selection.value;
        settings.minExposedEdgeDip = std::min(
            settings.minExposedEdgeDip, settings.repairTargetEdgeDip);
        break;
    case SettingField::MinimumOnscreenWidthDip:
        settings.minOnscreenWidthDip = selection.value;
        break;
    case SettingField::MinimumOnscreenHeightDip:
        settings.minOnscreenHeightDip = selection.value;
        break;
    case SettingField::EventCoalesceWindowMs:
        settings.eventCoalesceWindowMs = selection.value;
        break;
    case SettingField::ReconcileIntervalMs:
        settings.reconcileIntervalMs = selection.value;
        break;
    case SettingField::MaximumMovesPerBatch:
        settings.maxMovesPerBatch = selection.value;
        break;
    case SettingField::MaximumSolverStates:
        settings.maxSolverStates = selection.value;
        break;
    case SettingField::MaximumSolveTimeMs:
        settings.maxSolveTimeMs = selection.value;
        break;
    case SettingField::MaximumManagedWindows:
        settings.maxManagedWindows = selection.value;
        break;
    case SettingField::MaximumConsecutiveFailures:
        settings.maxConsecutiveFailures = selection.value;
        break;
    case SettingField::Count:
        return false;
    }
    return true;
}

} // namespace stage_manager::app
