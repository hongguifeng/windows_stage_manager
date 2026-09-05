#include "app/tray_controller.h"

#ifdef _WIN32

#include <shellapi.h>

#include <algorithm>
#include <string>

namespace stage_manager::app {
namespace {

std::wstring choice_text(SettingField field, std::uint32_t value)
{
    if (field == SettingField::DryRun) {
        return value == 0 ? L"Apply window changes" : L"Preview only (DryRun)";
    }
    if (field == SettingField::CenterActivatedWindow) {
        return value == 0 ? L"Off" : L"On";
    }
    auto text = std::to_wstring(value);
    switch (field) {
    case SettingField::MinimumExposedEdgeDip:
    case SettingField::MinimumExposedDepthDip:
    case SettingField::RepairTargetEdgeDip:
    case SettingField::MinimumOnscreenWidthDip:
    case SettingField::MinimumOnscreenHeightDip:
        return text + L" DIP";
    case SettingField::EventCoalesceWindowMs:
    case SettingField::ReconcileIntervalMs:
    case SettingField::MaximumSolveTimeMs:
        return text + L" ms";
    case SettingField::PreferredExposedEdges:
    case SettingField::MinimumExposedEdges:
        return text + (value == 1 ? L" edge" : L" edges");
    case SettingField::MaximumMovesPerBatch:
        return text + L" moves";
    case SettingField::MaximumSolverStates:
        return text + L" states";
    case SettingField::MaximumManagedWindows:
        return text + L" windows";
    case SettingField::MaximumConsecutiveFailures:
        return text + L" failures";
    case SettingField::DryRun:
    case SettingField::CenterActivatedWindow:
    case SettingField::Count:
        return text;
    }
    return text;
}

const wchar_t* field_title(SettingField field) noexcept
{
    switch (field) {
    case SettingField::DryRun:
        return L"Run mode";
    case SettingField::CenterActivatedWindow:
        return L"Center newly activated window";
    case SettingField::MinimumExposedEdgeDip:
        return L"Exposed edge length";
    case SettingField::MinimumExposedDepthDip:
        return L"Exposed edge depth";
    case SettingField::PreferredExposedEdges:
        return L"Preferred exposed edges";
    case SettingField::MinimumExposedEdges:
        return L"Minimum exposed edges";
    case SettingField::RepairTargetEdgeDip:
        return L"Repair target length";
    case SettingField::MinimumOnscreenWidthDip:
        return L"Minimum onscreen width";
    case SettingField::MinimumOnscreenHeightDip:
        return L"Minimum onscreen height";
    case SettingField::EventCoalesceWindowMs:
        return L"Event coalesce window";
    case SettingField::ReconcileIntervalMs:
        return L"Reconciliation interval";
    case SettingField::MaximumMovesPerBatch:
        return L"Maximum moves per batch";
    case SettingField::MaximumSolverStates:
        return L"Maximum solver states";
    case SettingField::MaximumSolveTimeMs:
        return L"Maximum solve time";
    case SettingField::MaximumManagedWindows:
        return L"Maximum managed windows";
    case SettingField::MaximumConsecutiveFailures:
        return L"Failure safety threshold";
    case SettingField::Count:
        return L"Unknown setting";
    }
    return L"Unknown setting";
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
        const auto custom_text = L"Current custom value: " + choice_text(field, current);
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

HMENU create_tray_context_menu(const Settings& settings, bool enabled)
{
    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return nullptr;
    }

    const wchar_t* toggle_text = enabled ? L"Pause manager" : L"Enable manager";
    AppendMenuW(menu, MF_STRING, TrayController::kCommandToggle, toggle_text);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    const HMENU settings_menu = CreatePopupMenu();
    if (settings_menu != nullptr) {
        for (const auto field : setting_fields()) {
            append_setting_menu(settings_menu, settings, field);
        }
        if (!AppendMenuW(menu,
                         MF_POPUP | MF_STRING,
                         reinterpret_cast<UINT_PTR>(settings_menu),
                         L"Settings")) {
            DestroyMenu(settings_menu);
        }
    }

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayController::kCommandExit, L"Exit");
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
    icon_data_.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
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
    default:
        if (const auto selection = decode_setting_command(LOWORD(command))) {
            return {TrayActionType::ApplySetting, selection};
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
        tooltip_ = L"Windows Stage Manager (running)";
        break;
    case TrayStatus::Paused:
        tooltip_ = L"Windows Stage Manager (paused)";
        break;
    case TrayStatus::Unsatisfiable:
        tooltip_ = L"Windows Stage Manager (no layout)";
        break;
    case TrayStatus::ApiError:
        tooltip_ = L"Windows Stage Manager (API error)";
        break;
    case TrayStatus::Rebuilding:
        tooltip_ = L"Windows Stage Manager (rebuilding)";
        break;
    }
    wcsncpy_s(icon_data_.szTip, tooltip_.c_str(), _TRUNCATE);
}

} // namespace stage_manager::app

#endif
