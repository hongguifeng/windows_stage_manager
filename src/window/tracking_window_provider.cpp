#include "window/tracking_window_provider.h"

namespace stage_manager::window {

TrackingWindowProvider::TrackingWindowProvider(IWindowProvider& provider,
                                               WindowIdentityTracker& identities)
    : provider_(provider), identities_(identities)
{
}

WindowSnapshotBatch TrackingWindowProvider::capture(SnapshotRefreshReason reason)
{
    auto batch = provider_.capture(reason);
    if (batch.status == SnapshotStatus::Ok && batch.complete) {
        identities_.apply(batch);
    }
    return batch;
}

void TrackingWindowProvider::handle_event(const WindowEvent& event)
{
    if (event.type == WindowEventType::HookError) {
        identities_.clear();
    } else {
        identities_.handle_event(event);
    }
}

} // namespace stage_manager::window
