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
    if (field == SettingField::CenterActivatedWindow) {
        return value == 0 ? L"0 - \u5173\u95ed" : L"1 - \u5f00\u542f";
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
        work.top + height * 3 / 10,
        work.right - width / 8,
        work.bottom - height / 8,
    };

    const auto inactive_brush = CreateSolidBrush(RGB(255, 242, 218));
    FillRect(item.hDC, &inactive, inactive_brush);
    DeleteObject(inactive_brush);
    FrameRect(item.hDC, &inactive, reinterpret_cast<HBRUSH>(GetStockObject(DKGRAY_BRUSH)));

    const auto active_brush = CreateSolidBrush(RGB(65, 105, 225));
    FillRect(item.hDC, &active, active_brush);
    DeleteObject(active_brush);

    const auto metrics = make_dip_preview_metrics(settings, state.dpi);
    const auto edge_length = std::clamp<int>(
        static_cast<int>(metrics.exposedEdgePixels), 8, inactive.right - inactive.left);
    const auto edge_depth = std::clamp<int>(
        static_cast<int>(metrics.exposedDepthPixels), 3, 36);
    const auto exposed_brush = CreateSolidBrush(RGB(41, 176, 106));
    RECT top_edge{inactive.left, inactive.top, inactive.left + edge_length,
                  inactive.top + edge_depth};
    RECT left_edge{inactive.left, inactive.top, inactive.left + edge_depth,
                   inactive.top + std::min<int>(
                       edge_length, inactive.bottom - inactive.top)};
    FillRect(item.hDC, &top_edge, exposed_brush);
    FillRect(item.hDC, &left_edge, exposed_brush);
    DeleteObject(exposed_brush);

    SetBkMode(item.hDC, TRANSPARENT);
    SetTextColor(item.hDC, RGB(255, 255, 255));
    DrawTextW(item.hDC, L"\u5f53\u524d\u6d3b\u52a8\u7a97\u53e3", -1, &active,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SetTextColor(item.hDC, RGB(45, 45, 45));
    RECT caption{bounds.left + 12, bounds.bottom - 27, bounds.right - 12, bounds.bottom - 6};
    const auto text = L"\u7eff\u8272 = \u9700\u4fdd\u7559\u7684\u53ef\u4ea4\u4e92\u8fb9\u7f18    " +
        std::to_wstring(settings.minExposedEdgeDip) + L"\u00d7" +
        std::to_wstring(settings.minExposedDepthDip) + L" DIP \u2248 " +
        std::to_wstring(metrics.exposedEdgePixels) + L"\u00d7" +
        std::to_wstring(metrics.exposedDepthPixels) + L" px";
    DrawTextW(item.hDC, text.c_str(), -1, &caption, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
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
    case SettingField::CenterActivatedWindow:
        return {L"\u65b0\u6fc0\u6d3b\u7a97\u53e3\u5c45\u4e2d", L"\u7a97\u53e3\u4ece\u540e\u53f0\u53d8\u4e3a\u6d3b\u52a8\u65f6\u5c45\u4e2d\uff1b\u7528\u6237\u624b\u52a8\u62d6\u52a8\u5df2\u6d3b\u52a8\u7a97\u53e3\u65f6\u4e0d\u4f1a\u62a2\u56de\u3002", L"\u5f00\u5173", false};
    case SettingField::MinimumExposedEdgeDip:
        return {L"\u53ef\u89c1\u8fb9\u7f18\u957f\u5ea6", L"\u4e00\u6761\u7a97\u53e3\u8fb9\u7f18\u81f3\u5c11\u6709\u591a\u957f\u4e0d\u88ab\u906e\u6321\uff0c\u7528\u4e8e\u786e\u4fdd\u53ef\u4ee5\u76f4\u63a5\u70b9\u51fb\u3002", L"DIP", true};
    case SettingField::MinimumExposedDepthDip:
        return {L"\u53ef\u89c1\u8fb9\u7f18\u6df1\u5ea6", L"\u9732\u51fa\u8fb9\u7f18\u5411\u7a97\u53e3\u5185\u90e8\u7684\u6700\u5c0f\u539a\u5ea6\uff0c\u503c\u8d8a\u5927\u8d8a\u5bb9\u6613\u70b9\u4e2d\u3002", L"DIP", true};
    case SettingField::PreferredExposedEdges:
        return {L"\u9996\u9009\u53ef\u89c1\u8fb9\u7f18\u6570", L"\u6b63\u5e38\u60c5\u51b5\u4e0b\u5e0c\u671b\u6bcf\u4e2a\u975e\u6d3b\u52a8\u7a97\u53e3\u540c\u65f6\u9732\u51fa\u7684\u72ec\u7acb\u8fb9\u7f18\u6570\u3002", L"\u6761\u8fb9\u7f18", false};
    case SettingField::MinimumExposedEdges:
        return {L"\u6700\u4f4e\u53ef\u89c1\u8fb9\u7f18\u6570", L"\u9996\u9009\u76ee\u6807\u65e0\u89e3\u65f6\u53ef\u964d\u7ea7\u5230\u7684\u6700\u5c0f\u8fb9\u7f18\u6570\u3002", L"\u6761\u8fb9\u7f18", false};
    case SettingField::RepairTargetEdgeDip:
        return {L"\u4fee\u590d\u76ee\u6807\u957f\u5ea6", L"\u79fb\u52a8\u7a97\u53e3\u65f6\u4e3a\u53ef\u89c1\u8fb9\u7f18\u9884\u7559\u7684\u76ee\u6807\u957f\u5ea6\uff0c\u5e94\u4e0d\u5c0f\u4e8e\u53ef\u89c1\u8fb9\u7f18\u957f\u5ea6\u3002", L"DIP", true};
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
        dip_pixels(settings.minExposedEdgeDip, dpi),
        dip_pixels(settings.minExposedDepthDip, dpi),
        dip_pixels(settings.repairTargetEdgeDip, dpi),
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
