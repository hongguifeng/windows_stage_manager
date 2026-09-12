#include "window/mvp_coordinator.h"
#include "window/tracking_window_provider.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

stage_manager::window::WindowSnapshot make_window(std::uintptr_t hwnd,
                                                   stage_manager::window::PixelRect rectangle,
                                                   std::int32_t z_index)
{
    stage_manager::window::WindowSnapshot window;
    window.key = {hwnd, static_cast<std::uint32_t>(hwnd + 100), 0};
    window.rootHwnd = hwnd;
    window.className = L"MvpCoordinatorTestWindow";
    window.placementRect = rectangle;
    window.visualRect = rectangle;
    window.workArea = {0, 0, 1000, 700};
    window.monitor = 1;
    window.dpi = 96;
    window.titleBarHeight = 24;
    window.style = 1;
    window.visible = true;
    window.currentDesktop = true;
    window.zIndex = z_index;
    window.zOrderKnown = true;
    return window;
}

class FakeDesktop final : public stage_manager::window::IWindowProvider,
                          public stage_manager::window::IWindowMover {
public:
    stage_manager::window::WindowSnapshotBatch capture(
        stage_manager::window::SnapshotRefreshReason reason) override
    {
        ++captureCalls;
        if (staleSnapshots || (staleAtCapture && captureCalls == *staleAtCapture)) {
            stage_manager::window::WindowSnapshotBatch stale;
            stale.status = stage_manager::window::SnapshotStatus::Stale;
            stale.complete = false;
            return stale;
        }
        if (switchMonitorAtCapture && captureCalls == *switchMonitorAtCapture) {
            const auto iterator = std::find_if(windows.begin(), windows.end(), [this](const auto& item) {
                return item.key.hwnd == switchMonitorWindow;
            });
            if (iterator != windows.end()) {
                iterator->monitor = 2;
                iterator->workArea = {1000, 0, 2000, 700};
            }
        }
        if (moveWindowAtCapture && captureCalls == *moveWindowAtCapture) {
            const auto iterator = std::find_if(windows.begin(), windows.end(), [this](const auto& item) {
                return item.key.hwnd == externallyMovedWindow;
            });
            if (iterator != windows.end()) {
                iterator->placementRect.left += 10;
                iterator->placementRect.right += 10;
                iterator->visualRect = iterator->placementRect;
            }
        }
        if (changeZOrderAtCapture && captureCalls == *changeZOrderAtCapture) {
            const auto iterator = std::find_if(windows.begin(), windows.end(), [this](const auto& item) {
                return item.key.hwnd == zOrderChangedWindow;
            });
            if (iterator != windows.end()) {
                ++iterator->zIndex;
            }
        }
        stage_manager::window::WindowSnapshotBatch result;
        result.version = static_cast<std::uint64_t>(captureCalls);
        result.reason = reason;
        result.status = stage_manager::window::SnapshotStatus::Ok;
        result.complete = true;
        result.windows = windows;
        if (omitWindowAtCapture && captureCalls == *omitWindowAtCapture) {
            std::erase_if(result.windows, [this](const auto& item) {
                return item.key.hwnd == transientWindow;
            });
        }
        if (omitWindowThroughCapture && captureCalls <= *omitWindowThroughCapture) {
            std::erase_if(result.windows, [this](const auto& item) {
                return item.key.hwnd == transientWindow;
            });
        }
        if (failWindowQueriesAtCapture && captureCalls == *failWindowQueriesAtCapture) {
            const auto iterator = std::find_if(
                result.windows.begin(), result.windows.end(), [this](const auto& item) {
                    return item.key.hwnd == transientWindow;
                });
            if (iterator != result.windows.end()) {
                iterator->queryFailures = stage_manager::window::field_bit(
                    stage_manager::window::SnapshotField::VisualRect);
            }
        }
        return result;
    }

    stage_manager::window::NativeMoveResult move(
        const stage_manager::window::WindowKey& key,
        const stage_manager::geometry::Rect& destination) override
    {
        ++moveCalls;
        if (failMoveWindow == key.hwnd)
            return {stage_manager::window::NativeMoveStatus::ApiFailure, 5};
        const auto iterator = std::find_if(windows.begin(), windows.end(), [&key](const auto& item) {
            return item.key.hwnd == key.hwnd && item.key.processId == key.processId;
        });
        if (iterator == windows.end()) {
            return {stage_manager::window::NativeMoveStatus::InvalidWindow, 1400};
        }
        const auto width = iterator->placementRect.right - iterator->placementRect.left;
        const auto height = iterator->placementRect.bottom - iterator->placementRect.top;
        iterator->placementRect = {
            static_cast<std::int32_t>(destination.left),
            static_cast<std::int32_t>(destination.top),
            static_cast<std::int32_t>(destination.left + width),
            static_cast<std::int32_t>(destination.top + height),
        };
        iterator->visualRect = iterator->placementRect;
        return {stage_manager::window::NativeMoveStatus::Moved, 0};
    }

    stage_manager::window::NativeReorderResult reorder(
        const stage_manager::window::WindowKey& key,
        const stage_manager::window::WindowKey& insert_after) override
    {
        const auto target = std::find_if(windows.begin(), windows.end(), [&key](const auto& item) {
            return item.key.hwnd == key.hwnd && item.key.processId == key.processId;
        });
        const auto reference = std::find_if(
            windows.begin(), windows.end(), [&insert_after](const auto& item) {
                return item.key.hwnd == insert_after.hwnd &&
                    item.key.processId == insert_after.processId;
            });
        if (target == windows.end()) {
            return {stage_manager::window::NativeReorderStatus::InvalidWindow, 1400};
        }
        if (reference == windows.end()) {
            return {stage_manager::window::NativeReorderStatus::InvalidReference, 1400};
        }
        const auto target_z = target->zIndex;
        const auto reference_z = reference->zIndex;
        for (auto& window : windows) {
            if (window.zIndex > reference_z && window.zIndex < target_z) {
                ++window.zIndex;
            }
        }
        target->zIndex = reference_z + 1;
        ++reorderCalls;
        return {stage_manager::window::NativeReorderStatus::Reordered, 0};
    }

    std::vector<stage_manager::window::WindowSnapshot> windows;
    bool staleSnapshots = false;
    std::optional<int> staleAtCapture;
    std::optional<int> switchMonitorAtCapture;
    std::optional<int> moveWindowAtCapture;
    std::optional<int> changeZOrderAtCapture;
    std::optional<int> omitWindowAtCapture;
    std::optional<int> omitWindowThroughCapture;
    std::optional<int> failWindowQueriesAtCapture;
    std::uintptr_t switchMonitorWindow = 0;
    std::uintptr_t externallyMovedWindow = 0;
    std::uintptr_t zOrderChangedWindow = 0;
    std::uintptr_t transientWindow = 0;
    int captureCalls = 0;
    int moveCalls = 0;
    std::uintptr_t failMoveWindow = 0;
    int reorderCalls = 0;
};

stage_manager::app::Settings test_settings()
{
    stage_manager::app::Settings settings;
    settings.affordancePreset = stage_manager::app::AffordancePreset::Custom;
    settings.topMinimumLengthDip = 48;
    settings.topMaximumLengthDip = 48;
    settings.topDepthDip = 24;
    settings.topLengthPercent = 100;
    settings.leftMinimumLengthDip = 48;
    settings.leftMaximumLengthDip = 48;
    settings.leftDepthDip = 24;
    settings.leftLengthPercent = 100;
    settings.rightMinimumLengthDip = 48;
    settings.rightMaximumLengthDip = 48;
    settings.rightDepthDip = 24;
    settings.rightLengthPercent = 100;
    settings.bottomMinimumLengthDip = 48;
    settings.bottomMaximumLengthDip = 48;
    settings.bottomDepthDip = 24;
    settings.bottomLengthPercent = 100;
    settings.maxSolveTimeMs = 1000;
    return settings;
}

stage_manager::solver::VisibilityRequirements two_edge_requirements()
{
    stage_manager::solver::VisibilityRequirements requirements;
    const stage_manager::solver::EdgeAffordanceRule rule{48, 48, 24, 100};
    requirements.top = rule;
    requirements.left = rule;
    requirements.right = rule;
    requirements.bottom = rule;
    requirements.maximumRegionRectangles = 128;
    requirements.goal = stage_manager::solver::VisibilityGoal::TitleBarLeftHalf;
    return requirements;
}

struct MvpFixture {
    FakeDesktop desktop;
    stage_manager::window::WindowIdentityTracker identities;
    stage_manager::window::TrackingWindowProvider provider;
    stage_manager::window::InternalMoveTracker internalMoves;
    stage_manager::window::MoveTransactionGuard guard;
    stage_manager::window::MoveFailureTracker failures;
    stage_manager::app::Settings settings;
    stage_manager::window::VerifiedMoveApplier applier;
    stage_manager::window::MvpCoordinator coordinator;

    explicit MvpFixture(stage_manager::app::Settings initial_settings = test_settings())
        : provider(desktop, identities),
          settings(initial_settings),
          applier(desktop, provider, internalMoves, &guard, &failures),
          coordinator(provider,
                      applier,
                      guard,
                      internalMoves,
                      stage_manager::window::ConservativeWindowClassifier(settings),
                      settings)
    {
    }
};

std::vector<stage_manager::window::WindowEvent> drag_events(std::uintptr_t active)
{
    using stage_manager::window::WindowEventType;
    return {
        {WindowEventType::MoveSizeStart, active, 1, 100, 1},
        {WindowEventType::LocationChange, active, 1, 101, 2},
        {WindowEventType::LocationChange, active, 1, 102, 3},
        {WindowEventType::MoveSizeEnd, active, 1, 103, 4},
    };
}

std::vector<stage_manager::window::WindowEvent> foreground_event(std::uintptr_t active)
{
    using stage_manager::window::WindowEventType;
    return {{WindowEventType::Foreground, active, 1, 100, 1}};
}

} // namespace

int main(int argc, char** argv)
{
    using stage_manager::solver::SolveStatus;
    using stage_manager::solver::VisibilityGoal;
    using stage_manager::window::MvpBatchStatus;
    using stage_manager::window::MvpSuspendReason;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;

    // Suppressed gestures must never invoke the solver, even when the manual
    // placement completely covers a peer and periodic reconciliation follows.
    for (const bool dry_run : {false, true}) {
        for (const bool split_batch : {false, true}) {
            MvpFixture manual;
            manual.desktop.windows = {make_window(190, {50, 50, 250, 250}, 0)};
            manual.coordinator.process(foreground_event(190), true, dry_run);
            manual.desktop.windows[0].placementRect = {50, 50, 250, 250};
            manual.desktop.windows[0].visualRect = {50, 50, 250, 250};
            manual.desktop.windows.push_back(make_window(191, {50, 50, 250, 250}, 1));
            const auto moves_before = manual.desktop.moveCalls;
            const auto events = drag_events(190);
            stage_manager::window::MvpBatchResult finished;
            if (split_batch) {
                manual.coordinator.process(std::span(events).first(1), true, dry_run);
                finished = manual.coordinator.process(std::span(events).subspan(1), true, dry_run);
            } else {
                finished = manual.coordinator.process(events, true, dry_run);
            }
            CHECK(finished.status == MvpBatchStatus::Idle);
            CHECK(!finished.solveAttempted);
            CHECK(!finished.applyAttempted);
            CHECK(!finished.backgroundRetryPending);
            const std::vector<WindowEvent> reconcile{{WindowEventType::Reconcile, 0, 0, 2000, 5}};
            const auto refreshed = manual.coordinator.process(reconcile, true, dry_run);
            CHECK(!refreshed.solveAttempted);
            CHECK(!refreshed.applyAttempted);
            CHECK(manual.desktop.moveCalls == moves_before);
            const auto switched = manual.coordinator.process(foreground_event(191), true, dry_run);
            CHECK(switched.solveAttempted);
        }
    }
    for (const bool already_foreground : {false, true}) {
        MvpFixture right;
        right.desktop.windows = {make_window(190, {50, 50, 250, 250}, 0)};
        if (already_foreground) right.coordinator.process(foreground_event(190), true, false);
        right.desktop.windows.push_back(make_window(191, right.desktop.windows[0].placementRect, 1));
        const auto moves_before = right.desktop.moveCalls;
        auto event = foreground_event(190);
        event[0].suppressLayout = true;
        const auto suppressed = right.coordinator.process(event, true, false);
        CHECK(suppressed.activationLayoutSuppressed);
        CHECK(!suppressed.solveAttempted);
        const auto dragged = right.coordinator.process(drag_events(190), true, false);
        CHECK(!dragged.solveAttempted);
        const std::vector<WindowEvent> reconcile{{WindowEventType::Reconcile, 0, 0, 2000, 5}};
        CHECK(!right.coordinator.process(reconcile, true, false).solveAttempted);
        right.coordinator.process(foreground_event(999), true, false);
        CHECK(!right.coordinator.process(foreground_event(190), true, false).solveAttempted);
        CHECK(!right.coordinator.process(reconcile, true, false).solveAttempted);
        CHECK(right.desktop.moveCalls == moves_before);
    }

    for (const bool right_click : {false, true}) {
        auto settings = test_settings();
        settings.placeActivatedWindow = false;
        MvpFixture pending(settings);
        pending.desktop.windows = {
            make_window(901, {200, 200, 800, 600}, 0),
            make_window(902, {200, 200, 1600, 1200}, 1)};
        CHECK(pending.coordinator.process(foreground_event(901), true, false).backgroundRetryPending);
        const auto moves_before = pending.desktop.moveCalls;
        auto gesture = right_click ? foreground_event(901) : drag_events(901);
        if (right_click) gesture[0].suppressLayout = true;
        const auto suppressed = pending.coordinator.process(gesture, true, false);
        CHECK(!suppressed.solveAttempted);
        CHECK(!suppressed.backgroundRetryPending);
        const std::vector<WindowEvent> reconcile{{WindowEventType::Reconcile, 0, 0, 2000, 5}};
        CHECK(!pending.coordinator.process(reconcile, true, false).solveAttempted);
        CHECK(pending.desktop.moveCalls == moves_before);
    }
    {
        MvpFixture newly_activated;
        newly_activated.desktop.windows = {
            make_window(190, {50, 50, 250, 250}, 0),
            make_window(191, {50, 50, 250, 250}, 1)};
        auto gesture = drag_events(190);
        auto activation = foreground_event(190)[0];
        activation.sequence = 0;
        gesture.insert(gesture.begin(), activation);
        CHECK(newly_activated.coordinator.process(gesture, true, true).solveAttempted);
    }

    if (argc > 1 && std::string_view(argv[1]) == "--layout-suppression") return 0;


    {
        auto settings = test_settings();
        settings.placeActivatedWindow = false;
        MvpFixture retry_gate(settings);
        retry_gate.desktop.windows = {
            make_window(901, {200, 200, 800, 600}, 0),
            make_window(902, {200, 200, 1600, 1200}, 1)};
        const auto first = retry_gate.coordinator.process(foreground_event(901), true, false);
        CHECK(first.backgroundRetryPending);
        CHECK(first.backgroundRetryDelayMs > 0);
        const std::vector<WindowEvent> early{{WindowEventType::Reconcile, 0, 0, 200, 2}};
        const auto waiting = retry_gate.coordinator.process(early, true, false);
        CHECK(!waiting.solveAttempted);
        CHECK(!waiting.applyAttempted);
        CHECK(waiting.backgroundRetryPending);
        CHECK(waiting.snapshotWindowCount == 2);
        CHECK(waiting.status == MvpBatchStatus::Idle);
        const std::vector<WindowEvent> due{{WindowEventType::Reconcile, 0, 0, 2000, 3}};
        const auto retried = retry_gate.coordinator.process(due, true, false);
        CHECK(retried.backgroundRetryUsed);
        CHECK(retried.solveAttempted);
        std::puts("retry delay gate regression passed");
        if (argc > 1) {
            auto ordered_settings = test_settings();
            ordered_settings.placeActivatedWindow = false;
            ordered_settings.maxManagedWindows = 3;
            MvpFixture ordered(ordered_settings);
            ordered.desktop.windows = {
                make_window(911, {300, 300, 900, 650}, 0),
                make_window(912, {300, 300, 900, 650}, 1),
                make_window(913, {264, 264, 864, 614}, 2),
                make_window(914, {300, 300, 900, 650}, 3)};
            const auto plan = ordered.coordinator.process(foreground_event(911), true, false);
            CHECK(plan.applyAttempted);
            CHECK(plan.apply.status == stage_manager::window::MoveApplyStatus::Applied);
            CHECK(ordered.desktop.windows[1].placementRect.left == 264);
            CHECK(ordered.desktop.windows[2].placementRect.left == 228);
            CHECK(ordered.desktop.windows[3].placementRect.left == 300);
            MvpFixture rejected(ordered_settings);
            rejected.desktop.windows = {
                make_window(911, {300, 300, 900, 650}, 0),
                make_window(912, {300, 300, 900, 650}, 1),
                make_window(913, {300, 300, 900, 650}, 2)};
            rejected.desktop.failMoveWindow = 912;
            const auto failed = rejected.coordinator.process(foreground_event(911), true, false);
            CHECK(failed.status == MvpBatchStatus::ApiError);
            CHECK(rejected.desktop.moveCalls == 1);
            CHECK(rejected.desktop.windows[2].placementRect.left == 300);
            CHECK(failed.solve.finalSnapshot.windows[1].managed);
            std::puts("Z-order selection and failed-prefix execution regressions passed");
            return 0;
        }
    }

    // Candidate-search and alternate-edge cases are covered by the generic
    // solver suites. TitleBarLeftHalf coordinator coverage lives above and
    // exercises only the deterministic prefix contract used in production.
    return 0;
}

