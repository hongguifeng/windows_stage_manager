#include "app/tray_controller.h"

#include <cassert>

int main()
{
    using stage_manager::app::TrayAction;
    using stage_manager::app::TrayController;

    TrayController controller;
    assert(controller.handle_command(TrayController::kCommandToggle) == TrayAction::ToggleEnabled);
    assert(controller.handle_command(TrayController::kCommandExit) == TrayAction::Exit);
    assert(controller.handle_callback(WM_LBUTTONUP) == TrayAction::ToggleEnabled);
    assert(controller.handle_callback(WM_RBUTTONUP) == TrayAction::None);
    assert(controller.handle_command(9999) == TrayAction::None);
    return 0;
}

