#pragma once

#include "window/window_identity.h"
#include "window/window_provider.h"

namespace stage_manager::window {

class TrackingWindowProvider final : public IWindowProvider {
public:
    TrackingWindowProvider(IWindowProvider& provider, WindowIdentityTracker& identities);

    WindowSnapshotBatch capture(SnapshotRefreshReason reason) override;
    void handle_event(const WindowEvent& event) override;

private:
    IWindowProvider& provider_;
    WindowIdentityTracker& identities_;
};

} // namespace stage_manager::window
