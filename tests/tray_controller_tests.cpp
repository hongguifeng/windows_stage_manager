#include "app/tray_controller.h"

#include <cassert>

int main()
{
    using stage_manager::app::TrayAction;
    using stage_manager::app::TrayController;
    using stage_manager::app::TrayStatus;

    TrayController controller;
    assert(controller.handle_command(TrayController::kCommandToggle) == TrayAction::ToggleEnabled);
    assert(controller.handle_command(TrayController::kCommandExit) == TrayAction::Exit);
    assert(controller.handle_callback(WM_LBUTTONUP) == TrayAction::ToggleEnabled);
    assert(controller.handle_callback(WM_RBUTTONUP) == TrayAction::None);
    assert(controller.handle_command(9999) == TrayAction::None);
    assert(controller.status() == TrayStatus::Running);
    controller.set_status(TrayStatus::Unsatisfiable);
    assert(controller.status() == TrayStatus::Unsatisfiable);
    assert(controller.enabled());
    controller.set_status(TrayStatus::ApiError);
    assert(controller.status() == TrayStatus::ApiError);
    controller.set_status(TrayStatus::Rebuilding);
    assert(controller.status() == TrayStatus::Rebuilding);
    controller.set_enabled(false);
    assert(controller.status() == TrayStatus::Paused);
    assert(!controller.enabled());
    return 0;
}
