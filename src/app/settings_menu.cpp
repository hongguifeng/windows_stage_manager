#include "app/settings_menu.h"

#include <algorithm>
#include <array>

namespace stage_manager::app {
namespace {

constexpr std::array kFields = {
    SettingField::DryRun,
    SettingField::PlaceActivatedWindow,
    SettingField::ActivationHorizontalAlignment,
    SettingField::ActivationVerticalAlignment,
    SettingField::AffordancePreset,
    SettingField::TopMinimumLengthDip,
    SettingField::TopMaximumLengthDip,
    SettingField::TopDepthDip,
    SettingField::TopLengthPercent,
    SettingField::LeftMinimumLengthDip,
    SettingField::LeftMaximumLengthDip,
    SettingField::LeftDepthDip,
    SettingField::LeftLengthPercent,
    SettingField::RightMinimumLengthDip,
    SettingField::RightMaximumLengthDip,
    SettingField::RightDepthDip,
    SettingField::RightLengthPercent,
    SettingField::BottomMinimumLengthDip,
    SettingField::BottomMaximumLengthDip,
    SettingField::BottomDepthDip,
    SettingField::BottomLengthPercent,
    SettingField::MinimumOnscreenWidthDip,
    SettingField::MinimumOnscreenHeightDip,
    SettingField::EventCoalesceWindowMs,
    SettingField::ReconcileIntervalMs,
    SettingField::MaximumMovesPerBatch,
    SettingField::MaximumSolverStates,
    SettingField::MaximumSolveTimeMs,
    SettingField::MaximumManagedWindows,
    SettingField::MaximumConsecutiveFailures,
    SettingField::UiLanguage,
};

constexpr std::array<std::uint32_t, 2> kBooleanChoices = {0, 1};
constexpr std::array<std::uint32_t, 3> kAlignmentChoices = {0, 1, 2};
constexpr std::array<std::uint32_t, 3> kPresetChoices = {0, 1, 2};
constexpr std::array<std::uint32_t, 6> kLengthChoices = {96, 120, 160, 180, 240, 300};
constexpr std::array<std::uint32_t, 6> kDepthChoices = {28, 32, 40, 48, 64, 80};
constexpr std::array<std::uint32_t, 5> kPercentChoices = {20, 25, 30, 35, 40};
constexpr std::array<std::uint32_t, 4> kOnscreenChoices = {64, 100, 160, 240};
constexpr std::array<std::uint32_t, 5> kCoalesceChoices = {8, 12, 16, 24, 32};
constexpr std::array<std::uint32_t, 4> kReconcileChoices = {100, 250, 500, 1000};
constexpr std::array<std::uint32_t, 4> kMoveChoices = {8, 16, 32, 64};
constexpr std::array<std::uint32_t, 4> kStateChoices = {128, 256, 512, 1024};
constexpr std::array<std::uint32_t, 5> kSolveTimeChoices = {8, 16, 32, 64, 100};
constexpr std::array<std::uint32_t, 5> kWindowCountChoices = {2, 5, 10, 15, 20};
constexpr std::array<std::uint32_t, 4> kFailureChoices = {1, 3, 5, 10};

bool is_minimum_length(SettingField field) noexcept
{
    return field == SettingField::TopMinimumLengthDip ||
        field == SettingField::LeftMinimumLengthDip ||
        field == SettingField::RightMinimumLengthDip ||
        field == SettingField::BottomMinimumLengthDip;
}

bool is_maximum_length(SettingField field) noexcept
{
    return field == SettingField::TopMaximumLengthDip ||
        field == SettingField::LeftMaximumLengthDip ||
        field == SettingField::RightMaximumLengthDip ||
        field == SettingField::BottomMaximumLengthDip;
}

bool is_depth(SettingField field) noexcept
{
    return field == SettingField::TopDepthDip || field == SettingField::LeftDepthDip ||
        field == SettingField::RightDepthDip || field == SettingField::BottomDepthDip;
}

bool is_percent(SettingField field) noexcept
{
    return field == SettingField::TopLengthPercent ||
        field == SettingField::LeftLengthPercent ||
        field == SettingField::RightLengthPercent ||
        field == SettingField::BottomLengthPercent;
}

bool contains_choice(SettingField field, std::uint32_t value) noexcept
{
    const auto choices = setting_choices(field);
    return std::find(choices.begin(), choices.end(), value) != choices.end();
}

void mark_custom(Settings& settings, SettingField field) noexcept
{
    if (is_minimum_length(field) || is_maximum_length(field) || is_depth(field) ||
        is_percent(field)) {
        settings.affordancePreset = AffordancePreset::Custom;
    }
}

bool apply_setting_value(Settings& settings, const SettingSelection& selection) noexcept
{
    switch (selection.field) {
    case SettingField::DryRun: settings.dryRun = selection.value != 0; break;
    case SettingField::PlaceActivatedWindow:
        settings.placeActivatedWindow = selection.value != 0;
        break;
    case SettingField::ActivationHorizontalAlignment:
        settings.activationHorizontalAlignment =
            static_cast<ActivationHorizontalAlignment>(selection.value);
        break;
    case SettingField::ActivationVerticalAlignment:
        settings.activationVerticalAlignment =
            static_cast<ActivationVerticalAlignment>(selection.value);
        break;
    case SettingField::AffordancePreset:
        if (selection.value > static_cast<std::uint32_t>(AffordancePreset::Prominent)) {
            return false;
        }
        apply_affordance_preset(settings, static_cast<AffordancePreset>(selection.value));
        break;
    case SettingField::UiLanguage:
        settings.uiLanguage = static_cast<UiLanguage>(selection.value);
        break;
    case SettingField::TopMinimumLengthDip:
        settings.topMinimumLengthDip = selection.value;
        settings.topMaximumLengthDip = std::max(settings.topMaximumLengthDip, selection.value);
        break;
    case SettingField::TopMaximumLengthDip:
        settings.topMaximumLengthDip = selection.value;
        settings.topMinimumLengthDip = std::min(settings.topMinimumLengthDip, selection.value);
        break;
    case SettingField::TopDepthDip: settings.topDepthDip = selection.value; break;
    case SettingField::TopLengthPercent: settings.topLengthPercent = selection.value; break;
    case SettingField::LeftMinimumLengthDip:
        settings.leftMinimumLengthDip = selection.value;
        settings.leftMaximumLengthDip = std::max(settings.leftMaximumLengthDip, selection.value);
        break;
    case SettingField::LeftMaximumLengthDip:
        settings.leftMaximumLengthDip = selection.value;
        settings.leftMinimumLengthDip = std::min(settings.leftMinimumLengthDip, selection.value);
        break;
    case SettingField::LeftDepthDip: settings.leftDepthDip = selection.value; break;
    case SettingField::LeftLengthPercent: settings.leftLengthPercent = selection.value; break;
    case SettingField::RightMinimumLengthDip:
        settings.rightMinimumLengthDip = selection.value;
        settings.rightMaximumLengthDip = std::max(settings.rightMaximumLengthDip, selection.value);
        break;
    case SettingField::RightMaximumLengthDip:
        settings.rightMaximumLengthDip = selection.value;
        settings.rightMinimumLengthDip = std::min(settings.rightMinimumLengthDip, selection.value);
        break;
    case SettingField::RightDepthDip: settings.rightDepthDip = selection.value; break;
    case SettingField::RightLengthPercent: settings.rightLengthPercent = selection.value; break;
    case SettingField::BottomMinimumLengthDip:
        settings.bottomMinimumLengthDip = selection.value;
        settings.bottomMaximumLengthDip = std::max(settings.bottomMaximumLengthDip, selection.value);
        break;
    case SettingField::BottomMaximumLengthDip:
        settings.bottomMaximumLengthDip = selection.value;
        settings.bottomMinimumLengthDip = std::min(settings.bottomMinimumLengthDip, selection.value);
        break;
    case SettingField::BottomDepthDip: settings.bottomDepthDip = selection.value; break;
    case SettingField::BottomLengthPercent: settings.bottomLengthPercent = selection.value; break;
    case SettingField::MinimumOnscreenWidthDip: settings.minOnscreenWidthDip = selection.value; break;
    case SettingField::MinimumOnscreenHeightDip: settings.minOnscreenHeightDip = selection.value; break;
    case SettingField::EventCoalesceWindowMs: settings.eventCoalesceWindowMs = selection.value; break;
    case SettingField::ReconcileIntervalMs: settings.reconcileIntervalMs = selection.value; break;
    case SettingField::MaximumMovesPerBatch: settings.maxMovesPerBatch = selection.value; break;
    case SettingField::MaximumSolverStates: settings.maxSolverStates = selection.value; break;
    case SettingField::MaximumSolveTimeMs: settings.maxSolveTimeMs = selection.value; break;
    case SettingField::MaximumManagedWindows: settings.maxManagedWindows = selection.value; break;
    case SettingField::MaximumConsecutiveFailures:
        settings.maxConsecutiveFailures = selection.value;
        break;
    case SettingField::Count: return false;
    }
    mark_custom(settings, selection.field);
    return true;
}

} // namespace

std::span<const SettingField> setting_fields() noexcept { return kFields; }

std::span<const std::uint32_t> setting_choices(SettingField field) noexcept
{
    if (field == SettingField::DryRun || field == SettingField::PlaceActivatedWindow ||
        field == SettingField::UiLanguage) {
        return kBooleanChoices;
    }
    if (field == SettingField::ActivationHorizontalAlignment ||
        field == SettingField::ActivationVerticalAlignment) {
        return kAlignmentChoices;
    }
    if (field == SettingField::AffordancePreset) return kPresetChoices;
    if (is_minimum_length(field) || is_maximum_length(field)) return kLengthChoices;
    if (is_depth(field)) return kDepthChoices;
    if (is_percent(field)) return kPercentChoices;
    switch (field) {
    case SettingField::MinimumOnscreenWidthDip:
    case SettingField::MinimumOnscreenHeightDip: return kOnscreenChoices;
    case SettingField::EventCoalesceWindowMs: return kCoalesceChoices;
    case SettingField::ReconcileIntervalMs: return kReconcileChoices;
    case SettingField::MaximumMovesPerBatch: return kMoveChoices;
    case SettingField::MaximumSolverStates: return kStateChoices;
    case SettingField::MaximumSolveTimeMs: return kSolveTimeChoices;
    case SettingField::MaximumManagedWindows: return kWindowCountChoices;
    case SettingField::MaximumConsecutiveFailures: return kFailureChoices;
    default: return {};
    }
}

std::string_view setting_field_name(SettingField field) noexcept
{
    constexpr std::array<std::string_view, static_cast<std::size_t>(SettingField::Count)> names = {
        "dry_run", "place_activated_window", "affordance_preset",
        "top_minimum_length_dip", "top_maximum_length_dip", "top_depth_dip",
        "top_length_percent", "left_minimum_length_dip", "left_maximum_length_dip",
        "left_depth_dip", "left_length_percent", "right_minimum_length_dip",
        "right_maximum_length_dip", "right_depth_dip", "right_length_percent",
        "bottom_minimum_length_dip", "bottom_maximum_length_dip", "bottom_depth_dip",
        "bottom_length_percent", "min_onscreen_width_dip", "min_onscreen_height_dip",
        "event_coalesce_window_ms", "reconcile_interval_ms", "max_moves_per_batch",
        "max_solver_states", "max_solve_time_ms", "max_managed_windows",
        "max_consecutive_failures", "activation_horizontal_alignment",
        "activation_vertical_alignment", "ui_language"};
    const auto index = static_cast<std::size_t>(field);
    return index < names.size() ? names[index] : "unknown";
}

std::uint32_t current_setting_value(const Settings& settings, SettingField field) noexcept
{
    switch (field) {
    case SettingField::DryRun: return settings.dryRun ? 1u : 0u;
    case SettingField::PlaceActivatedWindow: return settings.placeActivatedWindow ? 1u : 0u;
    case SettingField::ActivationHorizontalAlignment:
        return static_cast<std::uint32_t>(settings.activationHorizontalAlignment);
    case SettingField::ActivationVerticalAlignment:
        return static_cast<std::uint32_t>(settings.activationVerticalAlignment);
    case SettingField::AffordancePreset: return static_cast<std::uint32_t>(settings.affordancePreset);
    case SettingField::UiLanguage: return static_cast<std::uint32_t>(settings.uiLanguage);
    case SettingField::TopMinimumLengthDip: return settings.topMinimumLengthDip;
    case SettingField::TopMaximumLengthDip: return settings.topMaximumLengthDip;
    case SettingField::TopDepthDip: return settings.topDepthDip;
    case SettingField::TopLengthPercent: return settings.topLengthPercent;
    case SettingField::LeftMinimumLengthDip: return settings.leftMinimumLengthDip;
    case SettingField::LeftMaximumLengthDip: return settings.leftMaximumLengthDip;
    case SettingField::LeftDepthDip: return settings.leftDepthDip;
    case SettingField::LeftLengthPercent: return settings.leftLengthPercent;
    case SettingField::RightMinimumLengthDip: return settings.rightMinimumLengthDip;
    case SettingField::RightMaximumLengthDip: return settings.rightMaximumLengthDip;
    case SettingField::RightDepthDip: return settings.rightDepthDip;
    case SettingField::RightLengthPercent: return settings.rightLengthPercent;
    case SettingField::BottomMinimumLengthDip: return settings.bottomMinimumLengthDip;
    case SettingField::BottomMaximumLengthDip: return settings.bottomMaximumLengthDip;
    case SettingField::BottomDepthDip: return settings.bottomDepthDip;
    case SettingField::BottomLengthPercent: return settings.bottomLengthPercent;
    case SettingField::MinimumOnscreenWidthDip: return settings.minOnscreenWidthDip;
    case SettingField::MinimumOnscreenHeightDip: return settings.minOnscreenHeightDip;
    case SettingField::EventCoalesceWindowMs: return settings.eventCoalesceWindowMs;
    case SettingField::ReconcileIntervalMs: return settings.reconcileIntervalMs;
    case SettingField::MaximumMovesPerBatch: return settings.maxMovesPerBatch;
    case SettingField::MaximumSolverStates: return settings.maxSolverStates;
    case SettingField::MaximumSolveTimeMs: return settings.maxSolveTimeMs;
    case SettingField::MaximumManagedWindows: return settings.maxManagedWindows;
    case SettingField::MaximumConsecutiveFailures: return settings.maxConsecutiveFailures;
    case SettingField::Count: return 0;
    }
    return 0;
}

std::uint32_t setting_command_id(SettingField field, std::size_t choice_index) noexcept
{
    return kSettingCommandBase + static_cast<std::uint32_t>(field) * kSettingCommandStride +
        static_cast<std::uint32_t>(choice_index);
}

std::optional<SettingSelection> decode_setting_command(std::uint32_t command) noexcept
{
    if (command < kSettingCommandBase) return std::nullopt;
    const auto offset = command - kSettingCommandBase;
    const auto field_value = offset / kSettingCommandStride;
    const auto choice_index = offset % kSettingCommandStride;
    if (field_value >= static_cast<std::uint32_t>(SettingField::Count)) return std::nullopt;
    const auto field = static_cast<SettingField>(field_value);
    const auto choices = setting_choices(field);
    return choice_index < choices.size()
        ? std::optional{SettingSelection{field, choices[choice_index]}}
        : std::nullopt;
}

std::optional<SettingValueRange> custom_setting_range(SettingField field) noexcept
{
    if (is_minimum_length(field) || is_maximum_length(field) || is_depth(field) ||
        field == SettingField::MinimumOnscreenWidthDip ||
        field == SettingField::MinimumOnscreenHeightDip) return SettingValueRange{1, 8192};
    if (is_percent(field)) return SettingValueRange{1, 100};
    switch (field) {
    case SettingField::EventCoalesceWindowMs: return SettingValueRange{1, 1000};
    case SettingField::ReconcileIntervalMs: return SettingValueRange{10, 60000};
    case SettingField::MaximumMovesPerBatch: return SettingValueRange{1, 256};
    case SettingField::MaximumSolverStates: return SettingValueRange{1, 100000};
    case SettingField::MaximumSolveTimeMs: return SettingValueRange{1, 10000};
    case SettingField::MaximumManagedWindows: return SettingValueRange{2, 20};
    case SettingField::MaximumConsecutiveFailures: return SettingValueRange{1, 100};
    default: return std::nullopt;
    }
}

std::uint32_t custom_setting_command_id(SettingField field) noexcept
{
    return setting_command_id(field, kSettingCommandStride - 1);
}

std::optional<SettingField> decode_custom_setting_command(std::uint32_t command) noexcept
{
    if (command < kSettingCommandBase) return std::nullopt;
    const auto offset = command - kSettingCommandBase;
    if (offset % kSettingCommandStride != kSettingCommandStride - 1) return std::nullopt;
    const auto field_value = offset / kSettingCommandStride;
    if (field_value >= static_cast<std::uint32_t>(SettingField::Count)) return std::nullopt;
    const auto field = static_cast<SettingField>(field_value);
    return custom_setting_range(field) ? std::optional{field} : std::nullopt;
}

bool apply_setting_selection(Settings& settings, const SettingSelection& selection) noexcept
{
    return contains_choice(selection.field, selection.value) &&
        apply_setting_value(settings, selection);
}

bool apply_custom_setting_selection(Settings& settings,
                                    const SettingSelection& selection) noexcept
{
    const auto range = custom_setting_range(selection.field);
    return range && selection.value >= range->minimum && selection.value <= range->maximum &&
        apply_setting_value(settings, selection);
}

bool apply_setting_input(Settings& settings, const SettingSelection& selection) noexcept
{
    if (contains_choice(selection.field, selection.value)) {
        return apply_setting_value(settings, selection);
    }
    return apply_custom_setting_selection(settings, selection);
}

} // namespace stage_manager::app
