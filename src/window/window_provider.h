#pragma once

#include "window/window_event.h"
#include "window/window_snapshot.h"

namespace stage_manager::window {

class IWindowProvider {
public:
    virtual ~IWindowProvider() = default;

    virtual WindowSnapshotBatch capture(SnapshotRefreshReason reason) = 0;
    virtual void handle_event(const WindowEvent& event)
    {
        static_cast<void>(event);
    }
};

} // namespace stage_manager::window
