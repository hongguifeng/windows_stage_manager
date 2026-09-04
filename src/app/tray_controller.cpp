#include "app/tray_controller.h"

#ifdef _WIN32

#include <shellapi.h>

namespace stage_manager::app {

TrayController::~TrayController()
{
    shutdown();
}

bool TrayController::initialize(HWND owner, bool enabled)
{
    if (owner == nullptr) {
        return false;
    }

    owner_ = owner;
    enabled_ = enabled;
    status_ = enabled ? TrayStatus::Running : TrayStatus::Paused;
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

TrayAction TrayController::handle_callback(LPARAM event)
{
    switch (LOWORD(event)) {
    case WM_LBUTTONUP:
        return TrayAction::ToggleEnabled;
    case WM_RBUTTONUP:
        show_context_menu();
        return TrayAction::None;
    default:
        return TrayAction::None;
    }
}

TrayAction TrayController::handle_command(WPARAM command)
{
    switch (LOWORD(command)) {
    case kCommandToggle:
        return TrayAction::ToggleEnabled;
    case kCommandExit:
        return TrayAction::Exit;
    default:
        return TrayAction::None;
    }
}

void TrayController::show_context_menu()
{
    if (owner_ == nullptr) {
        return;
    }

    const HMENU menu = CreatePopupMenu();
    if (menu == nullptr) {
        return;
    }

    const wchar_t* toggle_text = enabled_ ? L"Pause manager" : L"Enable manager";
    AppendMenuW(menu, MF_STRING, kCommandToggle, toggle_text);
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, kCommandExit, L"Exit");

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
