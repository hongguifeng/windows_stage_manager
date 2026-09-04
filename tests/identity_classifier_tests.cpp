#include "window/window_classifier.h"
#include "window/window_identity.h"

#ifdef _WIN32

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace {

stage_manager::window::WindowSnapshot make_snapshot(
    std::uintptr_t hwnd, std::uint32_t process_id = 100, std::uint64_t generation = 1)
{
    stage_manager::window::WindowSnapshot snapshot;
    snapshot.key = {hwnd, process_id, generation};
    snapshot.rootHwnd = hwnd;
    snapshot.className = L"StageManager.TestWindow";
    snapshot.placementRect = {0, 0, 800, 600};
    snapshot.visualRect = snapshot.placementRect;
    snapshot.workArea = {0, 0, 1920, 1080};
    snapshot.dpi = 96;
    snapshot.visible = true;
    snapshot.currentDesktop = true;
    snapshot.zOrderKnown = true;
    snapshot.zIndex = 0;
    return snapshot;
}

bool has_reason(const stage_manager::window::ConservativeWindowClassifier& classifier,
                const stage_manager::window::WindowSnapshot& snapshot,
                stage_manager::window::UnmanagedReason reason)
{
    return classifier.classify(snapshot).reason == reason;
}

} // namespace

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::window::ConservativeWindowClassifier;
    using stage_manager::window::SnapshotField;
    using stage_manager::window::UnmanagedReason;
    using stage_manager::window::WindowDisposition;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;
    using stage_manager::window::WindowIdentityTracker;
    using stage_manager::window::WindowSnapshotBatch;
    using stage_manager::window::field_bit;

    WindowIdentityTracker identities;
    CHECK(identities.observe(0, 100).instanceGeneration == 0);
    CHECK(identities.size() == 0);

    const auto first = identities.observe(10, 100);
    const auto same = identities.observe(10, 100);
    CHECK(first.instanceGeneration != 0);
    CHECK(same.instanceGeneration == first.instanceGeneration);
    CHECK(identities.size() == 1);

    const auto changed_process = identities.observe(10, 101);
    CHECK(changed_process.instanceGeneration > first.instanceGeneration);
    CHECK(changed_process.processId == 101);

    identities.handle_event({WindowEventType::Destroy, 10, 0, 0, 1});
    CHECK(identities.size() == 0);
    const auto reused = identities.observe(10, 101);
    CHECK(reused.instanceGeneration > changed_process.instanceGeneration);

    const auto before_unknown_process = identities.observe(11, 111);
    CHECK(identities.observe(11, 0).instanceGeneration == 0);
    const auto after_unknown_process = identities.observe(11, 111);
    CHECK(after_unknown_process.instanceGeneration > before_unknown_process.instanceGeneration);

    WindowSnapshotBatch batch;
    batch.windows.push_back(make_snapshot(10, 101));
    batch.windows.push_back(make_snapshot(20, 102));
    identities.apply(batch);
    CHECK(batch.windows[0].key.instanceGeneration == reused.instanceGeneration);
    CHECK(batch.windows[1].key.instanceGeneration != 0);

    ConservativeWindowClassifier classifier;
    const auto normal = make_snapshot(30);
    const auto normal_result = classifier.classify(normal);
    CHECK(normal_result.disposition == WindowDisposition::Managed);
    CHECK(normal_result.reason == UnmanagedReason::None);

    auto invalid_identity = normal;
    invalid_identity.key.instanceGeneration = 0;
    CHECK(has_reason(classifier, invalid_identity, UnmanagedReason::InvalidIdentity));

    auto not_root = normal;
    not_root.rootHwnd = 31;
    CHECK(has_reason(classifier, not_root, UnmanagedReason::NotRootWindow));

    auto query_failed = normal;
    query_failed.queryFailures = field_bit(SnapshotField::VisualRect);
    CHECK(has_reason(classifier, query_failed, UnmanagedReason::ApiQueryFailed));

    auto other_desktop = normal;
    other_desktop.currentDesktop = false;
    CHECK(has_reason(classifier, other_desktop, UnmanagedReason::NotCurrentDesktop));

    auto invisible = normal;
    invisible.visible = false;
    CHECK(has_reason(classifier, invisible, UnmanagedReason::Invisible));

    auto minimized = normal;
    minimized.iconic = true;
    CHECK(has_reason(classifier, minimized, UnmanagedReason::Minimized));

    auto maximized = normal;
    maximized.zoomed = true;
    CHECK(has_reason(classifier, maximized, UnmanagedReason::Maximized));

    auto topmost = normal;
    topmost.topmost = true;
    CHECK(has_reason(classifier, topmost, UnmanagedReason::Topmost));

    auto tool = normal;
    tool.exStyle = WS_EX_TOOLWINDOW;
    CHECK(has_reason(classifier, tool, UnmanagedReason::ToolWindow));

    auto no_activate = normal;
    no_activate.exStyle = WS_EX_NOACTIVATE;
    CHECK(has_reason(classifier, no_activate, UnmanagedReason::NoActivate));

    auto system_ui = normal;
    system_ui.className = L"#32768";
    CHECK(has_reason(classifier, system_ui, UnmanagedReason::SystemUi));

    auto too_small = normal;
    too_small.placementRect = {0, 0, 50, 50};
    CHECK(has_reason(classifier, too_small, UnmanagedReason::TooSmall));

    auto scaled_boundary = normal;
    scaled_boundary.dpi = 144;
    scaled_boundary.placementRect = {0, 0, 150, 150};
    CHECK(classifier.classify(scaled_boundary).managed());
    scaled_boundary.placementRect.right = 149;
    CHECK(has_reason(classifier, scaled_boundary, UnmanagedReason::TooSmall));

    auto owner = make_snapshot(40);
    auto owned = make_snapshot(41);
    owned.ownerHwnd = owner.key.hwnd;
    const std::vector<stage_manager::window::WindowSnapshot> owned_group = {owner, owned};
    const auto group_results = classifier.classify_batch(owned_group);
    CHECK(group_results.size() == 2);
    CHECK(group_results[0].reason == UnmanagedReason::OwnedWindow);
    CHECK(group_results[1].reason == UnmanagedReason::OwnedWindow);

    WindowSnapshotBatch annotated;
    annotated.windows.push_back(normal);
    annotated.windows.push_back(tool);
    classifier.annotate(annotated);
    CHECK(annotated.windows[0].managed);
    CHECK(!annotated.windows[1].managed);

    identities.forget(10);
    CHECK(identities.size() == 2);
    identities.clear();
    CHECK(identities.size() == 0);
    return 0;
}

#else

int main()
{
    return 0;
}

#endif
