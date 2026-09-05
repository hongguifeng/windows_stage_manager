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
    CHECK(dip_fields == 5);
    CHECK(stage_manager::app::setting_help(SettingField::MinimumExposedEdgeDip).dipValue);
    CHECK(!stage_manager::app::setting_help(SettingField::MaximumSolveTimeMs).dipValue);

    stage_manager::app::Settings settings;
    settings.minExposedEdgeDip = 96;
    settings.minExposedDepthDip = 24;
    settings.repairTargetEdgeDip = 128;
    settings.minOnscreenWidthDip = 100;
    settings.minOnscreenHeightDip = 80;
    const auto preview = stage_manager::app::make_dip_preview_metrics(settings, 144);
    CHECK(preview.dpi == 144);
    CHECK(preview.exposedEdgePixels == 144);
    CHECK(preview.exposedDepthPixels == 36);
    CHECK(preview.repairTargetPixels == 192);
    CHECK(preview.minimumOnscreenWidthPixels == 150);
    CHECK(preview.minimumOnscreenHeightPixels == 120);

    const auto resource = FindResourceW(
        GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_SETTINGS), RT_DIALOG);
    CHECK(resource != nullptr);
    CHECK(SizeofResource(GetModuleHandleW(nullptr), resource) > 0);
    return 0;
}
