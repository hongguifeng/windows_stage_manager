#include "app/tray_controller.h"
#include "app/resource.h"

#ifdef _WIN32

#include <shellapi.h>

#include <algorithm>
#include <string>

namespace stage_manager::app {
namespace {

struct CustomSettingDialogState {
    SettingField field = SettingField::Count;
    std::uint32_t currentValue = 0;
    std::optional<std::uint32_t> result;
};

std::wstring choice_text(SettingField field, std::uint32_t value)
{
    if (field == SettingField::DryRun) {
        return value == 0 ? L"\u5e94\u7528\u7a97\u53e3\u8c03\u6574"
                          : L"\u4ec5\u9884\u89c8\uff08DryRun\uff09";
    }
    if (field == SettingField::PlaceActivatedWindow) {
        return value == 0 ? L"\u5173\u95ed" : L"\u5f00\u542f";
    }
    if (field == SettingField::ActivationHorizontalAlignment) {
        switch (static_cast<ActivationHorizontalAlignment>(value)) {
        case ActivationHorizontalAlignment::Left: return L"\u9760\u5de6";
        case ActivationHorizontalAlignment::Center: return L"\u6c34\u5e73\u5c45\u4e2d";
        case ActivationHorizontalAlignment::Right: return L"\u9760\u53f3";
        }
    }
    if (field == SettingField::ActivationVerticalAlignment) {
        switch (static_cast<ActivationVerticalAlignment>(value)) {
        case ActivationVerticalAlignment::Top: return L"\u9760\u4e0a";
        case ActivationVerticalAlignment::Center: return L"\u5782\u76f4\u5c45\u4e2d";
        case ActivationVerticalAlignment::Bottom: return L"\u9760\u4e0b";
        }
    }
    if (field == SettingField::AffordancePreset) {
        switch (static_cast<AffordancePreset>(value)) {
        case AffordancePreset::Compact: return L"\u7d27\u51d1";
        case AffordancePreset::Balanced: return L"\u5e73\u8861";
        case AffordancePreset::Prominent: return L"\u9192\u76ee";
        case AffordancePreset::Custom: return L"\u81ea\u5b9a\u4e49";
        }
    }
    auto text = std::to_wstring(value);
    switch (field) {
    case SettingField::TopMinimumLengthDip:
    case SettingField::TopMaximumLengthDip:
    case SettingField::TopDepthDip:
    case SettingField::LeftMinimumLengthDip:
    case SettingField::LeftMaximumLengthDip:
    case SettingField::LeftDepthDip:
    case SettingField::RightMinimumLengthDip:
    case SettingField::RightMaximumLengthDip:
    case SettingField::RightDepthDip:
    case SettingField::BottomMinimumLengthDip:
    case SettingField::BottomMaximumLengthDip:
    case SettingField::BottomDepthDip:
    case SettingField::MinimumOnscreenWidthDip:
    case SettingField::MinimumOnscreenHeightDip:
        return text + L" DIP";
    case SettingField::TopLengthPercent:
    case SettingField::LeftLengthPercent:
    case SettingField::RightLengthPercent:
    case SettingField::BottomLengthPercent:
        return text + L"%";
    case SettingField::EventCoalesceWindowMs:
    case SettingField::ReconcileIntervalMs:
    case SettingField::MaximumSolveTimeMs:
        return text + L" ms";
    case SettingField::MaximumMovesPerBatch:
        return text + L" \u6b21\u79fb\u52a8";
    case SettingField::MaximumSolverStates:
        return text + L" \u4e2a\u72b6\u6001";
    case SettingField::MaximumManagedWindows:
        return text + L" \u4e2a\u7a97\u53e3";
    case SettingField::MaximumConsecutiveFailures:
        return text + L" \u6b21\u5931\u8d25";
    case SettingField::DryRun:
    case SettingField::PlaceActivatedWindow:
    case SettingField::ActivationHorizontalAlignment:
    case SettingField::ActivationVerticalAlignment:
    case SettingField::AffordancePreset:
    case SettingField::Count:
        return text;
    }
    return text;
}

const wchar_t* field_title(SettingField field) noexcept
{
    switch (field) {
    case SettingField::DryRun:
        return L"\u8fd0\u884c\u6a21\u5f0f";
    case SettingField::PlaceActivatedWindow: return L"\u81ea\u52a8\u653e\u7f6e\u65b0\u6fc0\u6d3b\u7a97\u53e3";
    case SettingField::ActivationHorizontalAlignment: return L"\u65b0\u6fc0\u6d3b\u7a97\u53e3\u6c34\u5e73\u4f4d\u7f6e";
    case SettingField::ActivationVerticalAlignment: return L"\u65b0\u6fc0\u6d3b\u7a97\u53e3\u5782\u76f4\u4f4d\u7f6e";
    case SettingField::AffordancePreset: return L"\u53ef\u8fa8\u8bc6\u5ea6\u9884\u8bbe";
    case SettingField::TopMinimumLengthDip: return L"\u9876\u90e8\u6700\u5c0f\u957f\u5ea6";
    case SettingField::TopMaximumLengthDip: return L"\u9876\u90e8\u6700\u5927\u957f\u5ea6";
    case SettingField::TopDepthDip: return L"\u9876\u90e8\u56de\u9000\u9ad8\u5ea6";
    case SettingField::TopLengthPercent: return L"\u9876\u90e8\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::LeftMinimumLengthDip: return L"\u5de6\u4fa7\u6700\u5c0f\u957f\u5ea6";
    case SettingField::LeftMaximumLengthDip: return L"\u5de6\u4fa7\u6700\u5927\u957f\u5ea6";
    case SettingField::LeftDepthDip: return L"\u5de6\u4fa7\u6df1\u5ea6";
    case SettingField::LeftLengthPercent: return L"\u5de6\u4fa7\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::RightMinimumLengthDip: return L"\u53f3\u4fa7\u6700\u5c0f\u957f\u5ea6";
    case SettingField::RightMaximumLengthDip: return L"\u53f3\u4fa7\u6700\u5927\u957f\u5ea6";
    case SettingField::RightDepthDip: return L"\u53f3\u4fa7\u6df1\u5ea6";
    case SettingField::RightLengthPercent: return L"\u53f3\u4fa7\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::BottomMinimumLengthDip: return L"\u5e95\u90e8\u6700\u5c0f\u957f\u5ea6";
    case SettingField::BottomMaximumLengthDip: return L"\u5e95\u90e8\u6700\u5927\u957f\u5ea6";
    case SettingField::BottomDepthDip: return L"\u5e95\u90e8\u6df1\u5ea6";
    case SettingField::BottomLengthPercent: return L"\u5e95\u90e8\u52a8\u6001\u6bd4\u4f8b";
    case SettingField::MinimumOnscreenWidthDip:
        return L"\u6700\u5c0f\u5c4f\u4e0a\u5bbd\u5ea6";
    case SettingField::MinimumOnscreenHeightDip:
        return L"\u6700\u5c0f\u5c4f\u4e0a\u9ad8\u5ea6";
    case SettingField::EventCoalesceWindowMs:
        return L"\u4e8b\u4ef6\u5408\u5e76\u7a97\u53e3";
    case SettingField::ReconcileIntervalMs:
        return L"\u72b6\u6001\u5237\u65b0\u95f4\u9694";
    case SettingField::MaximumMovesPerBatch:
        return L"\u6bcf\u6279\u6700\u5927\u79fb\u52a8\u6570";
    case SettingField::MaximumSolverStates:
        return L"\u6700\u5927\u6c42\u89e3\u72b6\u6001\u6570";
    case SettingField::MaximumSolveTimeMs:
        return L"\u6700\u5927\u6c42\u89e3\u65f6\u95f4";
    case SettingField::MaximumManagedWindows:
        return L"\u6700\u5927\u7ba1\u7406\u7a97\u53e3\u6570";
    case SettingField::MaximumConsecutiveFailures:
        return L"\u8fde\u7eed\u5931\u8d25\u9608\u503c";
    case SettingField::Count:
        return L"\u672a\u77e5\u8bbe\u7f6e";
    }
    return L"\u672a\u77e5\u8bbe\u7f6e";
}

INT_PTR CALLBACK custom_setting_dialog_proc(
    HWND dialog, UINT message, WPARAM w_param, LPARAM l_param)
{
    auto* state = reinterpret_cast<CustomSettingDialogState*>(
        GetWindowLongPtrW(dialog, DWLP_USER));
    if (message == WM_INITDIALOG) {
        state = reinterpret_cast<CustomSettingDialogState*>(l_param);
        SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(state));
        const auto range = custom_setting_range(state->field);
        if (!range) {
            EndDialog(dialog, IDCANCEL);
            return TRUE;
        }
        const auto prompt = std::wstring(field_title(state->field)) +
            L"\uff1a\u8bf7\u8f93\u5165 " + std::to_wstring(range->minimum) + L" - " +
            std::to_wstring(range->maximum) + L" \u4e4b\u95f4\u7684\u6574\u6570";
        SetDlgItemTextW(dialog, IDC_CUSTOM_SETTING_PROMPT, prompt.c_str());
        SetDlgItemInt(dialog, IDC_CUSTOM_SETTING_VALUE, state->currentValue, FALSE);
        const auto edit = GetDlgItem(dialog, IDC_CUSTOM_SETTING_VALUE);
        SendMessageW(edit, EM_SETSEL, 0, -1);
        SetFocus(edit);
        return FALSE;
    }
    if (message != WM_COMMAND || state == nullptr) {
        return FALSE;
    }
    switch (LOWORD(w_param)) {
    case IDOK: {
        BOOL translated = FALSE;
        const auto value = GetDlgItemInt(
            dialog, IDC_CUSTOM_SETTING_VALUE, &translated, FALSE);
        const auto range = custom_setting_range(state->field);
        if (translated == FALSE || !range || value < range->minimum ||
            value > range->maximum) {
            MessageBoxW(dialog,
                        L"\u8bf7\u8f93\u5165\u63d0\u793a\u8303\u56f4\u5185\u7684\u6574\u6570\u3002",
                        L"\u6570\u503c\u65e0\u6548",
                        MB_OK | MB_ICONWARNING);
            return TRUE;
        }
        state->result = value;
        EndDialog(dialog, IDOK);
        return TRUE;
    }
    case IDCANCEL:
        EndDialog(dialog, IDCANCEL);
        return TRUE;
    default:
        return FALSE;
    }
}

std::wstring field_menu_text(const Settings& settings, SettingField field)
{
    return std::wstring(field_title(field)) + L" (" +
        choice_text(field, current_setting_value(settings, field)) + L")";
}

bool append_setting_menu(HMENU parent, const Settings& settings, SettingField field)
{
    const HMENU choices_menu = CreatePopupMenu();
    if (choices_menu == nullptr) {
        return false;
    }

    const auto current = current_setting_value(settings, field);
    const auto choices = setting_choices(field);
    const bool current_is_preset =
        std::find(choices.begin(), choices.end(), current) != choices.end();
    if (!current_is_preset) {
        const auto custom_text = L"\u5f53\u524d\u81ea\u5b9a\u4e49\u503c\uff1a" +
            choice_text(field, current);
        AppendMenuW(choices_menu, MF_STRING | MF_DISABLED | MF_CHECKED, 0, custom_text.c_str());
        AppendMenuW(choices_menu, MF_SEPARATOR, 0, nullptr);
    }

    for (std::size_t index = 0; index < choices.size(); ++index) {
        const auto value = choices[index];
        const UINT flags = MF_STRING | (value == current ? MF_CHECKED : MF_UNCHECKED);
        const auto text = choice_text(field, value);
        AppendMenuW(choices_menu,
                    flags,
                    setting_command_id(field, index),
                    text.c_str());
    }

    if (custom_setting_range(field)) {
        AppendMenuW(choices_menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(choices_menu,
                    MF_STRING,
                    custom_setting_command_id(field),
                    L"\u81ea\u5b9a\u4e49\u2026");
    }

    const auto title = field_menu_text(settings, field);
    if (!AppendMenuW(parent,
                     MF_POPUP | MF_STRING,
                     reinterpret_cast<UINT_PTR>(choices_menu),
                     title.c_str())) {
        DestroyMenu(choices_menu);
        return false;
    }
    return true;
}

} // namespace

UnsatisfiableNotification unsatisfiable_notification() noexcept
{
    return {
        L"\u7a97\u53e3\u5e03\u5c40\u6682\u65f6\u65e0\u89e3",
        L"\u5f53\u524d\u7a7a\u95f4\u65e0\u6cd5\u8fdb\u4e00\u6b65\u9732\u51fa\u540e\u53f0\u7a97\u53e3\u3002\u7a0b\u5e8f\u672a\u6539\u53d8\u7a97\u53e3\u5c42\u7ea7\uff0c\u5c06\u5728\u7a97\u53e3\u72b6\u6001\u53d8\u5316\u540e\u91cd\u8bd5\u3002",
        5'000,
        NIIF_WARNING | NIIF_NOSOUND,
    };
}

bool unsatisfiable_notification_due(
    std::optional<std::uint64_t> last_notification_ms,
    std::uint64_t now_ms) noexcept
{
    return !last_notification_ms ||
        (now_ms >= *last_notification_ms &&
         now_ms - *last_notification_ms >= kUnsatisfiableNotificationCooldownMs);
}

std::optional<std::uint32_t> prompt_custom_setting_value(
    HWND owner, SettingField field, std::uint32_t current_value)
{
    if (!custom_setting_range(field)) {
        return std::nullopt;
    }
    CustomSettingDialogState state{field, current_value, std::nullopt};
    const auto result = DialogBoxParamW(GetModuleHandleW(nullptr),
                                        MAKEINTRESOURCEW(IDD_CUSTOM_SETTING),
                                        owner,
                                        custom_setting_dialog_proc,
                                        reinterpret_cast<LPARAM>(&state));
    return result == IDOK ? state.result : std::nullopt;
}

HMENU create_tray_context_menu(const Settings& settings, bool enabled)
{
    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return nullptr;
    }

    const wchar_t* toggle_text = enabled ? L"\u6682\u505c\u7ba1\u7406"
                                         : L"\u542f\u7528\u7ba1\u7406";
    AppendMenuW(menu, MF_STRING, TrayController::kCommandToggle, toggle_text);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    const HMENU settings_menu = CreatePopupMenu();
    AppendMenuW(menu,
                MF_STRING,
                TrayController::kCommandSettings,
                L"\u6253\u5f00\u53ef\u89c6\u5316\u8bbe\u7f6e\u2026");
    if (settings_menu != nullptr) {
        for (const auto field : setting_fields()) {
            append_setting_menu(settings_menu, settings, field);
        }
        if (!AppendMenuW(menu,
                         MF_POPUP | MF_STRING,
                         reinterpret_cast<UINT_PTR>(settings_menu),
                         L"\u5feb\u901f\u53c2\u6570\u8bbe\u7f6e")) {
            DestroyMenu(settings_menu);
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayController::kCommandExit, L"\u9000\u51fa");
    return menu;
}

TrayController::~TrayController()
{
    shutdown();
}

bool TrayController::initialize(HWND owner, const Settings& settings)
{
    if (owner == nullptr) {
        return false;
    }

    owner_ = owner;
    settings_ = settings;
    enabled_ = settings.enabled;
    status_ = enabled_ ? TrayStatus::Running : TrayStatus::Paused;
    icon_data_ = {};
    icon_data_.cbSize = sizeof(icon_data_);
    icon_data_.hWnd = owner_;
    icon_data_.uID = 1;
    icon_data_.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    icon_data_.uCallbackMessage = kTrayCallbackMessage;
    icon_data_.hIcon = static_cast<HICON>(LoadImageW(
        GetModuleHandleW(nullptr),
        MAKEINTRESOURCEW(IDI_STAGE_MANAGER),
        IMAGE_ICON,
        GetSystemMetrics(SM_CXSMICON),
        GetSystemMetrics(SM_CYSMICON),
        LR_SHARED));
    if (icon_data_.hIcon == nullptr) {
        owner_ = nullptr;
        return false;
    }
    update_tooltip();

    if (!Shell_NotifyIconW(NIM_ADD, &icon_data_)) {
        owner_ = nullptr;
        return false;
    }

    installed_ = true;
    icon_data_.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &icon_data_);
    return true;
}

void TrayController::shutdown()
{
    if (installed_) {
        Shell_NotifyIconW(NIM_DELETE, &icon_data_);
    }
    installed_ = false;
    owner_ = nullptr;
    icon_data_ = {};
    last_unsatisfiable_notification_ms_.reset();
}

void TrayController::set_enabled(bool enabled)
{
    enabled_ = enabled;
    status_ = enabled ? TrayStatus::Running : TrayStatus::Paused;
    update_tooltip();
    if (installed_) {
        Shell_NotifyIconW(NIM_MODIFY, &icon_data_);
    }
}

void TrayController::set_status(TrayStatus status)
{
    status_ = status;
    enabled_ = status != TrayStatus::Paused;
    update_tooltip();
    if (installed_) {
        Shell_NotifyIconW(NIM_MODIFY, &icon_data_);
    }
}

TrayStatus TrayController::status() const noexcept
{
    return status_;
}

bool TrayController::show_unsatisfiable_notification(std::uint64_t now_ms)
{
    if (!installed_ ||
        !unsatisfiable_notification_due(last_unsatisfiable_notification_ms_, now_ms)) {
        return false;
    }
    const auto content = unsatisfiable_notification();
    auto notification = icon_data_;
    notification.uFlags = NIF_INFO;
    wcsncpy_s(notification.szInfoTitle, content.title.data(), _TRUNCATE);
    wcsncpy_s(notification.szInfo, content.message.data(), _TRUNCATE);
    notification.dwInfoFlags = content.flags;
    notification.uTimeout = content.timeoutMs;
    if (!Shell_NotifyIconW(NIM_MODIFY, &notification)) {
        return false;
    }
    last_unsatisfiable_notification_ms_ = now_ms;
    return true;
}

bool TrayController::enabled() const noexcept
{
    return enabled_;
}

void TrayController::set_settings(const Settings& settings)
{
    settings_ = settings;
}

bool TrayController::is_setting_checked(std::uint32_t command) const noexcept
{
    const auto selection = decode_setting_command(command);
    return selection.has_value() &&
        current_setting_value(settings_, selection->field) == selection->value;
}

TrayAction TrayController::handle_callback(LPARAM event)
{
    switch (LOWORD(event)) {
    case WM_LBUTTONUP:
        return {TrayActionType::ToggleEnabled, std::nullopt};
    case WM_RBUTTONUP:
        show_context_menu();
        return {};
    default:
        return {};
    }
}

TrayAction TrayController::handle_command(WPARAM command)
{
    switch (LOWORD(command)) {
    case kCommandToggle:
        return {TrayActionType::ToggleEnabled, std::nullopt};
    case kCommandExit:
        return {TrayActionType::Exit, std::nullopt};
    case kCommandSettings:
        return {TrayActionType::OpenSettings, std::nullopt};
    default:
        if (const auto selection = decode_setting_command(LOWORD(command))) {
            return {TrayActionType::ApplySetting, selection};
        }
        if (const auto field = decode_custom_setting_command(LOWORD(command))) {
            return {TrayActionType::RequestCustomSetting,
                    SettingSelection{*field, current_setting_value(settings_, *field)}};
        }
        return {};
    }
}

void TrayController::show_context_menu()
{
    if (owner_ == nullptr) {
        return;
    }

    const HMENU menu = create_tray_context_menu(settings_, enabled_);
    if (menu == nullptr) {
        return;
    }

    POINT cursor{};
    GetCursorPos(&cursor);
    SetForegroundWindow(owner_);
    TrackPopupMenuEx(menu, TPM_RIGHTBUTTON, cursor.x, cursor.y, owner_, nullptr);
    PostMessageW(owner_, WM_NULL, 0, 0);
    DestroyMenu(menu);
}

void TrayController::update_tooltip()
{
    switch (status_) {
    case TrayStatus::Running:
        tooltip_ = L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u8fd0\u884c\u4e2d\uff09";
        break;
    case TrayStatus::Paused:
        tooltip_ = L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u5df2\u6682\u505c\uff09";
        break;
    case TrayStatus::Unsatisfiable:
        tooltip_ = L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u65e0\u53ef\u7528\u5e03\u5c40\uff09";
        break;
    case TrayStatus::ApiError:
        tooltip_ = L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08API \u9519\u8bef\uff09";
        break;
    case TrayStatus::Rebuilding:
        tooltip_ = L"Windows \u7a97\u53e3\u7ba1\u7406\u5668\uff08\u6b63\u5728\u91cd\u5efa\uff09";
        break;
    }
    wcsncpy_s(icon_data_.szTip, tooltip_.c_str(), _TRUNCATE);
}

} // namespace stage_manager::app

#endif
