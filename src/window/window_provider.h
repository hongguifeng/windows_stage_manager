#pragma once

#include "window/window_snapshot.h"

namespace stage_manager::window {

class IWindowProvider {
public:
    virtual ~IWindowProvider() = default;

    virtual WindowSnapshotBatch capture(SnapshotRefreshReason reason) = 0;
};

} // namespace stage_manager::window
