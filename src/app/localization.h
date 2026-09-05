#pragma once

#include "app/settings.h"
#include "app/settings_menu.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace stage_manager::app {

enum class UiText : std::uint8_t {
    CustomSettingTitle,
    SettingsTitle,
    Ok,
    Cancel,
    CurrentValue,
    PreviewHint,
    RestoreDefaults,
    SaveAndApply,
    UnitPrefix,
    InputRange,
    ChooseCandidate,
    CurrentMonitor,
    Pixels,
    InvalidParameterMessage,
    InvalidParameterTitle,
    ActiveWindow,
    PreviewCaption,
    EnterIntegerPrefix,
    EnterIntegerSuffix,
    InvalidValueMessage,
    InvalidValueTitle,
    CurrentCustomValue,
    Custom,
    PauseManagement,
    EnableManagement,
    OpenVisualSettings,
    QuickSettings,
    Exit,
    UnsatisfiableTitle,
    UnsatisfiableMessage,
    SearchLimitTitle,
    SearchLimitMessage,
    GeometryTooComplexTitle,
    GeometryTooComplexMessage,
    InvalidLayoutInputTitle,
    InvalidLayoutInputMessage,
    LayoutFailureTitle,
    LayoutFailureMessage,
    TooltipRunning,
    TooltipPaused,
    TooltipUnsatisfiable,
    TooltipApiError,
    TooltipRebuilding,
};

std::wstring_view ui_text(UiLanguage language, UiText text) noexcept;
std::wstring_view setting_field_title(SettingField field, UiLanguage language) noexcept;
std::wstring setting_choice_text(
    SettingField field,
    std::uint32_t value,
    UiLanguage language,
    bool include_numeric_prefix = false);

} // namespace stage_manager::app
