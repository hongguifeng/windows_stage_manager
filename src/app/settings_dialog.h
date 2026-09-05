#pragma once

#ifdef _WIN32

#include "app/settings.h"
#include "app/settings_menu.h"

#include <windows.h>

#include <cstdint>
#include <optional>

namespace stage_manager::app {

struct SettingHelp {
    const wchar_t* title = L"";
    const wchar_t* description = L"";
    const wchar_t* unit = L"";
    bool dipValue = false;
};

struct DipPreviewMetrics {
    std::uint32_t dpi = 96;
    std::uint64_t topLengthPixels = 0;
    std::uint64_t topDepthPixels = 0;
    std::uint64_t leftLengthPixels = 0;
    std::uint64_t leftDepthPixels = 0;
    std::uint64_t rightLengthPixels = 0;
    std::uint64_t rightDepthPixels = 0;
    std::uint64_t bottomLengthPixels = 0;
    std::uint64_t bottomDepthPixels = 0;
    std::uint64_t minimumOnscreenWidthPixels = 0;
    std::uint64_t minimumOnscreenHeightPixels = 0;
};

SettingHelp setting_help(
    SettingField field,
    UiLanguage language = UiLanguage::SimplifiedChinese) noexcept;
DipPreviewMetrics make_dip_preview_metrics(
    const Settings& settings, std::uint32_t dpi) noexcept;
std::optional<Settings> prompt_settings_dialog(HWND owner, const Settings& current);

} // namespace stage_manager::app

#endif
