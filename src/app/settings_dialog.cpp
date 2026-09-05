#include "app/settings_dialog.h"

#ifdef _WIN32

#include "app/resource.h"
#include "geometry/dpi.h"

#include <algorithm>
#include <cerrno>
#include <cwchar>
#include <limits>
#include <string>

namespace stage_manager::app {
namespace {

struct SettingsDialogState {
    Settings draft;
    std::optional<Settings> result;
    SettingField selected = SettingField::DryRun;
    std::uint32_t dpi = 96;
};

std::wstring value_text(SettingField field, std::uint32_t value)
{
    if (field == SettingField::DryRun) {
        return value == 0 ? L"0 - \u5e94\u7528\u7a97\u53e3\u8c03\u6574"
                          : L"1 - \u4ec5\u9884\u89c8\uff08DryRun\uff09";
    }
    if (field == SettingField::PlaceActivatedWindow) {
        return value == 0 ? L"0 - \u5173\u95ed" : L"1 - \u5f00\u542f";
    }
    if (field == SettingField::AffordancePreset) {
        switch (static_cast<AffordancePreset>(value)) {
        case AffordancePreset::Compact: return L"0 - \u7d27\u51d1";
        case AffordancePreset::Balanced: return L"1 - \u5e73\u8861";
        case AffordancePreset::Prominent: return L"2 - \u9192\u76ee";
        case AffordancePreset::Custom: return L"3 - \u81ea\u5b9a\u4e49";
        }
    }
    return std::to_wstring(value);
}

std::optional<std::uint32_t> read_value(HWND dialog)
{
    wchar_t text[128]{};
    if (GetDlgItemTextW(dialog, IDC_SETTINGS_VALUE, text, 128) == 0) {
        return std::nullopt;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const auto parsed = std::wcstoul(text, &end, 10);
    if (end == text || errno == ERANGE ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
}

void set_control_font(HWND dialog, int control_id)
{
    const auto font = SendMessageW(dialog, WM_GETFONT, 0, 0);
    SendDlgItemMessageW(dialog, control_id, WM_SETFONT, font, TRUE);
}

void populate_value_choices(HWND dialog, const SettingsDialogState& state)
{
    const auto combo = GetDlgItem(dialog, IDC_SETTINGS_VALUE);
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    const auto current = current_setting_value(state.draft, state.selected);
    int selected_index = -1;
    for (const auto choice : setting_choices(state.selected)) {
        const auto text = value_text(state.selected, choice);
        const auto index = static_cast<int>(SendMessageW(
            combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(text.c_str())));
        if (index >= 0) {
            SendMessageW(combo, CB_SETITEMDATA, index, choice);
            if (choice == current) {
                selected_index = index;
            }
        }
    }
    if (selected_index >= 0) {
        SendMessageW(combo, CB_SETCURSEL, selected_index, 0);
    } else {
        const auto text = value_text(state.selected, current);
        SetWindowTextW(combo, text.c_str());
    }
}

std::wstring range_and_scale_text(const SettingsDialogState& state)
{
    const auto help = setting_help(state.selected);
    std::wstring text = L"\u5355\u4f4d\uff1a";
    text += help.unit;
    if (const auto range = custom_setting_range(state.selected)) {
        text += L"    \u53ef\u8f93\u5165\u8303\u56f4\uff1a" +
            std::to_wstring(range->minimum) + L" - " + std::to_wstring(range->maximum);
    } else {
        text += L"    \u8bf7\u4ece\u5019\u9009\u503c\u4e2d\u9009\u62e9";
    }
    if (help.dipValue) {
        const auto value = current_setting_value(state.draft, state.selected);
        const auto pixels = geometry::scale_dip_ceil(value, state.dpi);
        text += L"\r\n\u5f53\u524d\u663e\u793a\u5668\uff1a" + std::to_wstring(state.dpi) +
            L" DPI\uff08" + std::to_wstring(state.dpi * 100u / 96u) +
            L"%\uff09\uff0c" + std::to_wstring(value) + L" DIP \u2248 " +
            std::to_wstring(pixels) + L" \u50cf\u7d20";
    }
    return text;
}

void refresh_selected_field(HWND dialog, SettingsDialogState& state)
{
    const auto help = setting_help(state.selected);
    SetDlgItemTextW(dialog, IDC_SETTINGS_TITLE, help.title);
    SetDlgItemTextW(dialog, IDC_SETTINGS_DESCRIPTION, help.description);
    populate_value_choices(dialog, state);
    const auto details = range_and_scale_text(state);
    SetDlgItemTextW(dialog, IDC_SETTINGS_UNIT, details.c_str());
    InvalidateRect(GetDlgItem(dialog, IDC_SETTINGS_PREVIEW), nullptr, TRUE);
}

bool commit_current_field(HWND dialog, SettingsDialogState& state, bool show_error)
{
    const auto value = read_value(dialog);
    auto candidate = state.draft;
    if (!value || !apply_setting_input(candidate, {state.selected, *value})) {
        if (show_error) {
            MessageBoxW(dialog,
                        L"\u8bf7\u9009\u62e9\u9884\u8bbe\u503c\uff0c\u6216\u8f93\u5165\u5141\u8bb8\u8303\u56f4\u5185\u7684\u6574\u6570\u3002",
                        L"\u53c2\u6570\u65e0\u6548",
                        MB_OK | MB_ICONWARNING);
        }
        return false;
    }
    state.draft = candidate;
    const auto details = range_and_scale_text(state);
    SetDlgItemTextW(dialog, IDC_SETTINGS_UNIT, details.c_str());
    return true;
}

Settings preview_settings(HWND dialog, const SettingsDialogState& state)
{
    auto preview = state.draft;
    if (const auto value = read_value(dialog)) {
        apply_setting_input(preview, {state.selected, *value});
    }
    return preview;
}

void draw_preview(HWND dialog, const DRAWITEMSTRUCT& item, const SettingsDialogState& state)
{
    const auto settings = preview_settings(dialog, state);
    RECT bounds = item.rcItem;
    FillRect(item.hDC, &bounds, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));

    RECT work = bounds;
    work.left += 18;
    work.right -= 18;
    work.top += 22;
    work.bottom -= 34;
    const auto background = CreateSolidBrush(RGB(238, 242, 247));
    FillRect(item.hDC, &work, background);
    DeleteObject(background);
    FrameRect(item.hDC, &work, reinterpret_cast<HBRUSH>(GetStockObject(GRAY_BRUSH)));

    const auto width = work.right - work.left;
    const auto height = work.bottom - work.top;
    RECT inactive{
        work.left + width / 9,
        work.top + height / 8,
        work.right - width / 9,
        work.bottom - height / 10,
    };
    RECT active{
        work.left + width * 3 / 10,
        work.top + height * 7 / 20,
        work.right - width / 8,
        work.bottom,
    };

    const auto inactive_brush = CreateSolidBrush(RGB(255, 242, 218));
    FillRect(item.hDC, &inactive, inactive_brush);
    DeleteObject(inactive_brush);
    FrameRect(item.hDC, &inactive, reinterpret_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    const auto active_brush = CreateSolidBrush(RGB(65, 105, 225));
    FillRect(item.hDC, &active, active_brush);
    DeleteObject(active_brush);

    const auto metrics = make_dip_preview_metrics(settings, state.dpi);
    const auto top_length = std::clamp<int>(
        static_cast<int>(metrics.topLengthPixels), 8, inactive.right - inactive.left);
    const auto top_depth = std::clamp<int>(static_cast<int>(metrics.topDepthPixels), 3, 36);
    const auto left_length = std::clamp<int>(
        static_cast<int>(metrics.leftLengthPixels), 8, inactive.bottom - inactive.top);
    const auto left_depth = std::clamp<int>(static_cast<int>(metrics.leftDepthPixels), 3, 36);
    const auto right_length = std::clamp<int>(
        static_cast<int>(metrics.rightLengthPixels), 8, inactive.bottom - inactive.top);
    const auto right_depth = std::clamp<int>(static_cast<int>(metrics.rightDepthPixels), 3, 36);
    const auto bottom_length = std::clamp<int>(
        static_cast<int>(metrics.bottomLengthPixels), 8, inactive.right - inactive.left);
    const auto bottom_depth = std::clamp<int>(
        static_cast<int>(metrics.bottomDepthPixels), 3, 36);
    const auto exposed_brush = CreateSolidBrush(RGB(41, 176, 106));
    RECT top_edge{inactive.left, inactive.top, inactive.left + top_length,
                  inactive.top + top_depth};
    RECT left_edge{inactive.left, inactive.top, inactive.left + left_depth,
                   inactive.top + left_length};
    RECT right_edge{inactive.right - right_depth, inactive.top,
                    inactive.right, inactive.top + right_length};
    RECT bottom_edge{inactive.right - bottom_length, inactive.bottom - bottom_depth,
                     inactive.right, inactive.bottom};
    FillRect(item.hDC, &top_edge, exposed_brush);
    FillRect(item.hDC, &left_edge, exposed_brush);
    FillRect(item.hDC, &right_edge, exposed_brush);
    FillRect(item.hDC, &bottom_edge, exposed_brush);
    DeleteObject(exposed_brush);

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(255, 255, 255));
    DrawTextW(item.hDC, L"\u5f53\u524d\u6d3b\u52a8\u7a97\u53e3", -1, &active,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(item.hDC, RGB(45, 45, 45));
    RECT caption{bounds.left + 12, bounds.bottom - 27, bounds.right - 12, bounds.bottom - 6};
    const auto text = L"\u7eff\u8272 = \u56db\u8fb9\u72ec\u7acb\u53ef\u8fa8\u8bc6\u533a\uff1b\u9876\u90e8\u6309\u6807\u9898\u680f\u9ad8\u5ea6\uff0c\u53f3/\u4e0b\u9700\u8981\u66f4\u5927\u70b9\u51fb\u533a";
    DrawTextW(item.hDC, text, -1, &caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

INT_PTR CALLBACK settings_dialog_proc(HWND dialog,
                                      UINT message,
                                      WPARAM w_param,
                                      LPARAM l_param)
{
    auto* state = reinterpret_cast<SettingsDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<SettingsDialogState*>(l_param);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        state->dpi = GetDpiForWindow(dialog);
        const auto fields = setting_fields();
        for (const auto field : fields) {
            const auto help = setting_help(field);
            const auto index = SendDlgItemMessageW(
                dialog, IDC_SETTINGS_FIELDS, LB_ADDSTRING, 0,
                reinterpret_cast<LPARAM>(help.title));
            if (index >= 0) {
                SendDlgItemMessageW(dialog, IDC_SETTINGS_FIELDS, LB_SETITEMDATA,
                                    index, static_cast<LPARAM>(field));
            }
        }
        SendDlgItemMessageW(dialog, IDC_SETTINGS_FIELDS, LB_SETCURSEL, 0, 0);
        for (const int id : {IDC_SETTINGS_FIELDS, IDC_SETTINGS_TITLE,
                             IDC_SETTINGS_DESCRIPTION, IDC_SETTINGS_VALUE,
                             IDC_SETTINGS_UNIT, IDC_SETTINGS_PREVIEW,
                             IDC_SETTINGS_DEFAULTS}) {
            set_control_font(dialog, id);
        }
        refresh_selected_field(dialog, *state);
        return TRUE;
    }
    if (state == nullptr) {
        return FALSE;
    }
    if (message == WM_DRAWITEM && w_param == IDC_SETTINGS_PREVIEW) {
        draw_preview(dialog, *reinterpret_cast<const DRAWITEMSTRUCT*>(l_param), *state);
        return TRUE;
    }
    if (message == WM_DPICHANGED) {
        state->dpi = HIWORD(w_param);
        refresh_selected_field(dialog, *state);
        return TRUE;
    }
    if (message != WM_COMMAND) {
        return FALSE;
    }

    const auto control = LOWORD(w_param);
    const auto notification = HIWORD(w_param);
    if (control == IDC_SETTINGS_FIELDS && notification == LBN_SELCHANGE) {
        const auto selected = SendDlgItemMessageW(
            dialog, IDC_SETTINGS_FIELDS, LB_GETCURSEL, 0, 0);
        const auto previous = static_cast<LRESULT>(state->selected);
        if (selected != LB_ERR && selected != previous) {
            if (!commit_current_field(dialog, *state, true)) {
                SendDlgItemMessageW(dialog, IDC_SETTINGS_FIELDS, LB_SETCURSEL,
                                    previous, 0);
                return TRUE;
            }
            state->selected = static_cast<SettingField>(selected);
            refresh_selected_field(dialog, *state);
        }
        return TRUE;
    }
    if (control == IDC_SETTINGS_VALUE &&
        (notification == CBN_EDITCHANGE || notification == CBN_SELCHANGE)) {
        auto preview = state->draft;
        if (const auto value = read_value(dialog);
            value && apply_setting_input(preview, {state->selected, *value})) {
            auto preview_state = *state;
            preview_state.draft = preview;
            const auto details = range_and_scale_text(preview_state);
            SetDlgItemTextW(dialog, IDC_SETTINGS_UNIT, details.c_str());
        }
        InvalidateRect(GetDlgItem(dialog, IDC_SETTINGS_PREVIEW), nullptr, TRUE);
        return TRUE;
    }
    switch (control) {
    case IDC_SETTINGS_DEFAULTS: {
        const bool enabled = state->draft.enabled;
        state->draft = Settings{};
        state->draft.enabled = enabled;
        refresh_selected_field(dialog, *state);
        return TRUE;
    }
    case IDOK:
        if (commit_current_field(dialog, *state, true)) {
            state->result = state->draft;
            EndDialog(dialog, IDOK);
        }
        return TRUE;
    case IDCANCEL:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

std::uint64_t dip_pixels(std::uint32_t dip, std::uint32_t dpi) noexcept
{
    return static_cast<std::uint64_t>(geometry::scale_dip_ceil(dip, dpi));
}

} // namespace

SettingHelp setting_help(SettingField field) noexcept
{
    switch (field) {
    case SettingField::DryRun:
        return {L"\u8fd0\u884c\u6a21\u5f0f", L"\u4ec5\u9884\u89c8\u4f1a\u8ba1\u7b97\u5e03\u5c40\u5e76\u5199\u65e5\u5fd7\uff0c\u4f46\u4e0d\u79fb\u52a8\u771f\u5b9e\u7a97\u53e3\u3002", L"\u5f00\u5173", false};
    case SettingField::PlaceActivatedWindow:
        return {L"\u65b0\u6fc0\u6d3b\u7a97\u53e3\u9760\u4e0b\u5c45\u4e2d", L"\u4ec5\u5728\u7a97\u53e3\u4ece\u540e\u53f0\u53d8\u4e3a\u6d3b\u52a8\u65f6\uff0c\u6c34\u5e73\u5c45\u4e2d\u5e76\u5e95\u90e8\u5bf9\u9f50\uff1b\u624b\u52a8\u62d6\u52a8\u540e\u4e0d\u62a2\u56de\u3002", L"\u5f00\u5173", false};
    case SettingField::AffordancePreset:
        return {L"\u53ef\u8fa8\u8bc6\u5ea6\u9884\u8bbe", L"\u7d27\u51d1\u51cf\u5c11\u79fb\u52a8\uff0c\u5e73\u8861\u9002\u5408\u5927\u591a\u6570\u663e\u793a\u5668\uff0c\u9192\u76ee\u4fdd\u7559\u66f4\u5927\u70b9\u51fb\u533a\u3002\u4fee\u6539\u4efb\u4e00\u5206\u8fb9\u53c2\u6570\u540e\u663e\u793a\u4e3a\u81ea\u5b9a\u4e49\u3002", L"\u9884\u8bbe", false};
    case SettingField::TopMinimumLengthDip: return {L"\u9876\u90e8\u6700\u5c0f\u957f\u5ea6", L"\u6807\u9898\u680f\u53ef\u89c1\u6bb5\u7684\u4e0b\u9650\u3002", L"DIP", true};
    case SettingField::TopMaximumLengthDip: return {L"\u9876\u90e8\u6700\u5927\u957f\u5ea6", L"\u6807\u9898\u680f\u53ef\u89c1\u6bb5\u7684\u4e0a\u9650\u3002", L"DIP", true};
    case SettingField::TopDepthDip: return {L"\u9876\u90e8\u56de\u9000\u9ad8\u5ea6", L"\u65e0\u6cd5\u8bfb\u53d6\u6807\u9898\u680f\u65f6\u4f7f\u7528\u7684\u9876\u90e8\u9ad8\u5ea6\uff1b\u6807\u51c6\u7a97\u53e3\u4f18\u5148\u4f7f\u7528\u5b9e\u9645\u6807\u9898\u680f\u3002", L"DIP", true};
    case SettingField::TopLengthPercent: return {L"\u9876\u90e8\u52a8\u6001\u6bd4\u4f8b", L"\u9876\u90e8\u957f\u5ea6\u6309\u7a97\u53e3\u5bbd\u5ea6\u7684\u6bd4\u4f8b\u8ba1\u7b97\uff0c\u518d\u9650\u5236\u5728\u4e0a\u4e0b\u9650\u4e4b\u95f4\u3002", L"%", false};
    case SettingField::LeftMinimumLengthDip: return {L"\u5de6\u4fa7\u6700\u5c0f\u957f\u5ea6", L"\u5de6\u8fb9\u53ef\u89c1\u6bb5\u7684\u4e0b\u9650\u3002", L"DIP", true};
    case SettingField::LeftMaximumLengthDip: return {L"\u5de6\u4fa7\u6700\u5927\u957f\u5ea6", L"\u5de6\u8fb9\u53ef\u89c1\u6bb5\u7684\u4e0a\u9650\u3002", L"DIP", true};
    case SettingField::LeftDepthDip: return {L"\u5de6\u4fa7\u6df1\u5ea6", L"\u5de6\u8fb9\u5411\u7a97\u53e3\u5185\u4fdd\u7559\u7684\u53ef\u70b9\u51fb\u5bbd\u5ea6\u3002", L"DIP", true};
    case SettingField::LeftLengthPercent: return {L"\u5de6\u4fa7\u52a8\u6001\u6bd4\u4f8b", L"\u5de6\u8fb9\u957f\u5ea6\u5360\u7a97\u53e3\u9ad8\u5ea6\u7684\u6bd4\u4f8b\u3002", L"%", false};
    case SettingField::RightMinimumLengthDip: return {L"\u53f3\u4fa7\u6700\u5c0f\u957f\u5ea6", L"\u53f3\u8fb9\u53ef\u89c1\u6bb5\u7684\u4e0b\u9650\uff0c\u9ed8\u8ba4\u5927\u4e8e\u5de6\u8fb9\u3002", L"DIP", true};
    case SettingField::RightMaximumLengthDip: return {L"\u53f3\u4fa7\u6700\u5927\u957f\u5ea6", L"\u53f3\u8fb9\u53ef\u89c1\u6bb5\u7684\u4e0a\u9650\u3002", L"DIP", true};
    case SettingField::RightDepthDip: return {L"\u53f3\u4fa7\u6df1\u5ea6", L"\u53f3\u8fb9\u8f83\u96be\u8fa8\u8bc6\uff0c\u9ed8\u8ba4\u4fdd\u7559\u66f4\u5927\u5bbd\u5ea6\u3002", L"DIP", true};
    case SettingField::RightLengthPercent: return {L"\u53f3\u4fa7\u52a8\u6001\u6bd4\u4f8b", L"\u53f3\u8fb9\u957f\u5ea6\u5360\u7a97\u53e3\u9ad8\u5ea6\u7684\u6bd4\u4f8b\u3002", L"%", false};
    case SettingField::BottomMinimumLengthDip: return {L"\u5e95\u90e8\u6700\u5c0f\u957f\u5ea6", L"\u5e95\u8fb9\u53ef\u89c1\u6bb5\u7684\u4e0b\u9650\uff0c\u9ed8\u8ba4\u4e3a\u56db\u8fb9\u6700\u5927\u3002", L"DIP", true};
    case SettingField::BottomMaximumLengthDip: return {L"\u5e95\u90e8\u6700\u5927\u957f\u5ea6", L"\u5e95\u8fb9\u53ef\u89c1\u6bb5\u7684\u4e0a\u9650\u3002", L"DIP", true};
    case SettingField::BottomDepthDip: return {L"\u5e95\u90e8\u6df1\u5ea6", L"\u5e95\u8fb9\u8f83\u96be\u8fa8\u8bc6\uff0c\u9700\u8981\u8f83\u5927\u53ef\u70b9\u51fb\u9ad8\u5ea6\u3002", L"DIP", true};
    case SettingField::BottomLengthPercent: return {L"\u5e95\u90e8\u52a8\u6001\u6bd4\u4f8b", L"\u5e95\u8fb9\u957f\u5ea6\u5360\u7a97\u53e3\u5bbd\u5ea6\u7684\u6bd4\u4f8b\u3002", L"%", false};
    case SettingField::MinimumOnscreenWidthDip:
        return {L"\u6700\u5c0f\u5c4f\u4e0a\u5bbd\u5ea6", L"\u79fb\u52a8\u540e\u81f3\u5c11\u4fdd\u7559\u5728\u5f53\u524d\u5de5\u4f5c\u533a\u5185\u7684\u7a97\u53e3\u5bbd\u5ea6\u3002", L"DIP", true};
    case SettingField::MinimumOnscreenHeightDip:
        return {L"\u6700\u5c0f\u5c4f\u4e0a\u9ad8\u5ea6", L"\u79fb\u52a8\u540e\u81f3\u5c11\u4fdd\u7559\u5728\u5f53\u524d\u5de5\u4f5c\u533a\u5185\u7684\u7a97\u53e3\u9ad8\u5ea6\u3002", L"DIP", true};
    case SettingField::EventCoalesceWindowMs:
        return {L"\u4e8b\u4ef6\u5408\u5e76\u7a97\u53e3", L"\u5c06\u77ed\u65f6\u95f4\u5185\u8fde\u7eed\u7684\u7a97\u53e3\u4e8b\u4ef6\u5408\u5e76\u5904\u7406\uff0c\u503c\u8d8a\u5927\u6296\u52a8\u8d8a\u5c11\u4f46\u54cd\u5e94\u7a0d\u6162\u3002", L"ms", false};
    case SettingField::ReconcileIntervalMs:
        return {L"\u72b6\u6001\u5237\u65b0\u95f4\u9694", L"\u5373\u4f7f\u6ca1\u6709\u6536\u5230\u4e8b\u4ef6\uff0c\u4e5f\u5b9a\u671f\u91cd\u65b0\u68c0\u67e5\u7a97\u53e3\u5e03\u5c40\u7684\u65f6\u95f4\u3002", L"ms", false};
    case SettingField::MaximumMovesPerBatch:
        return {L"\u6bcf\u6279\u6700\u5927\u79fb\u52a8\u6570", L"\u4e00\u6b21\u5e03\u5c40\u4fee\u590d\u5141\u8bb8\u79fb\u52a8\u7684\u7a97\u53e3\u6b21\u6570\uff0c\u7528\u4e8e\u9632\u6b62\u65e0\u9650\u63a8\u6324\u3002", L"\u6b21", false};
    case SettingField::MaximumSolverStates:
        return {L"\u6700\u5927\u6c42\u89e3\u72b6\u6001\u6570", L"\u5168\u5c40\u641c\u7d22\u6700\u591a\u68c0\u67e5\u7684\u5019\u9009\u5e03\u5c40\u6570\uff0c\u503c\u8d8a\u5927\u627e\u5230\u590d\u6742\u89e3\u7684\u6982\u7387\u8d8a\u9ad8\u3002", L"\u4e2a\u72b6\u6001", false};
    case SettingField::MaximumSolveTimeMs:
        return {L"\u6700\u5927\u6c42\u89e3\u65f6\u95f4", L"\u5355\u6b21\u4f4d\u7f6e\u641c\u7d22\u7684\u65f6\u95f4\u9884\u7b97\uff1b\u8d85\u65f6\u4f1a\u8fdb\u5165\u6709\u754c\u7684\u9010\u7a97\u4fee\u590d\u3002", L"ms", false};
    case SettingField::MaximumManagedWindows:
        return {L"\u6700\u5927\u7ba1\u7406\u7a97\u53e3\u6570", L"\u540c\u4e00\u663e\u793a\u5668\u4e0a\u4e00\u6b21\u7eb3\u5165\u5e03\u5c40\u8ba1\u7b97\u7684\u7a97\u53e3\u6570\u4e0a\u9650\u3002", L"\u4e2a\u7a97\u53e3", false};
    case SettingField::MaximumConsecutiveFailures:
        return {L"\u8fde\u7eed\u5931\u8d25\u9608\u503c", L"\u8fde\u7eed\u53d1\u751f API \u6216\u9a8c\u8bc1\u5931\u8d25\u8fbe\u5230\u6b64\u6b21\u6570\u540e\uff0c\u81ea\u52a8\u6682\u505c\u4ee5\u4fdd\u62a4\u7a97\u53e3\u3002", L"\u6b21", false};
    case SettingField::Count:
        return {};
    }
    return {};
}

DipPreviewMetrics make_dip_preview_metrics(
    const Settings& settings, std::uint32_t dpi) noexcept
{
    return {
        dpi,
        dip_pixels(settings.topMinimumLengthDip, dpi),
        dip_pixels(settings.topDepthDip, dpi),
        dip_pixels(settings.leftMinimumLengthDip, dpi),
        dip_pixels(settings.leftDepthDip, dpi),
        dip_pixels(settings.rightMinimumLengthDip, dpi),
        dip_pixels(settings.rightDepthDip, dpi),
        dip_pixels(settings.bottomMinimumLengthDip, dpi),
        dip_pixels(settings.bottomDepthDip, dpi),
        dip_pixels(settings.minOnscreenWidthDip, dpi),
        dip_pixels(settings.minOnscreenHeightDip, dpi),
    };
}

std::optional<Settings> prompt_settings_dialog(HWND owner, const Settings& current)
{
    SettingsDialogState state;
    state.draft = current;
    const auto result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(IDD_SETTINGS),
                                        owner,
                                        settings_dialog_proc,
                                        reinterpret_cast<LPARAM>(&state));
    return result == IDOK ? state.result : std::nullopt;
}

} // namespace stage_manager::app

#endif
