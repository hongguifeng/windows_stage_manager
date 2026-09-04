#include "platform/win32/window_provider.h"
#include "window/snapshot_report.h"
#include "window/window_classifier.h"
#include "window/window_identity.h"

#ifdef _WIN32

#include <iostream>

int main()
{
    using stage_manager::platform::win32::Win32WindowProvider;
    using stage_manager::window::ConservativeWindowClassifier;
    using stage_manager::window::SnapshotRefreshReason;
    using stage_manager::window::SnapshotStatus;
    using stage_manager::window::WindowIdentityTracker;

    Win32WindowProvider provider;
    WindowIdentityTracker identities;
    ConservativeWindowClassifier classifier;

    auto snapshot = provider.capture(SnapshotRefreshReason::Manual);
    identities.apply(snapshot);
    const auto classifications = classifier.classify_batch(snapshot.windows);
    stage_manager::window::write_snapshot_report(std::cout, snapshot, classifications);
    return snapshot.status == SnapshotStatus::Ok && snapshot.complete ? 0 : 1;
}

#else

int main()
{
    return 1;
}

#endif
