#include "app/resource.h"
#include "app/settings_dialog.h"

#include <windows.h>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main()
{
    using stage_manager::app::SettingField;
    std::size_t dip_fields = 0;
    for (const auto field : stage_manager::app::setting_fields()) {
        const auto help = stage_manager::app::setting_help(field);
        CHECK(help.title != nullptr && help.title[0] != L'\0');
        CHECK(help.description != nullptr && help.description[0] != L'\0');
        CHECK(help.unit != nullptr && help.unit[0] != L'\0');
        if (help.dipValue) {
            ++dip_fields;
        }
    }
    CHECK(dip_fields == 14);
    CHECK(stage_manager::app::setting_help(SettingField::TopDepthDip).dipValue);
    CHECK(!stage_manager::app::setting_help(SettingField::MaximumSolveTimeMs).dipValue);
    CHECK(stage_manager::app::setting_choices(
        SettingField::ActivationHorizontalAlignment).size() == 3);
    CHECK(stage_manager::app::setting_choices(
        SettingField::ActivationVerticalAlignment).size() == 3);
    CHECK(stage_manager::app::setting_help(
        SettingField::ActivationHorizontalAlignment).title[0] != L'\0');
    CHECK(stage_manager::app::setting_help(
        SettingField::ActivationVerticalAlignment).title[0] != L'\0');

    stage_manager::app::Settings settings;
    settings.topMinimumLengthDip = 96;
    settings.topDepthDip = 24;
    settings.leftMinimumLengthDip = 120;
    settings.leftDepthDip = 32;
    settings.rightMinimumLengthDip = 160;
    settings.rightDepthDip = 64;
    settings.bottomMinimumLengthDip = 180;
    settings.bottomDepthDip = 72;
    settings.minOnscreenWidthDip = 100;
    settings.minOnscreenHeightDip = 80;
    const auto preview = stage_manager::app::make_dip_preview_metrics(settings, 144);
    CHECK(preview.dpi == 144);
    CHECK(preview.topLengthPixels == 144);
    CHECK(preview.topDepthPixels == 36);
    CHECK(preview.leftLengthPixels == 180);
    CHECK(preview.leftDepthPixels == 48);
    CHECK(preview.rightLengthPixels == 240);
    CHECK(preview.rightDepthPixels == 96);
    CHECK(preview.bottomLengthPixels == 270);
    CHECK(preview.bottomDepthPixels == 108);
    CHECK(preview.minimumOnscreenWidthPixels == 150);
    CHECK(preview.minimumOnscreenHeightPixels == 120);

    const auto resource = FindResourceW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), RT_DIALOG);
    CHECK(resource != nullptr);
    CHECK(SizeofResource(GetModuleHandleW(nullptr), resource) > 0);
    return 0;
}
