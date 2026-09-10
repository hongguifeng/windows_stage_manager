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

int main(int argc, char**)
{
    using stage_manager::solver::SolveStatus;
    using stage_manager::solver::VisibilityGoal;
    using stage_manager::window::MvpBatchStatus;
    using stage_manager::window::MvpSuspendReason;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;

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

    CHECK(stage_manager::window::suspend_reason_name(MvpSuspendReason::None) == "none");
    CHECK(stage_manager::window::suspend_reason_name(MvpSuspendReason::SnapshotUnavailable) ==
          "snapshot_unavailable");
    CHECK(stage_manager::window::suspend_reason_name(MvpSuspendReason::ActiveWindowUnavailable) ==
          "active_window_unavailable");
    CHECK(stage_manager::window::suspend_reason_name(MvpSuspendReason::ActiveMonitorChanged) ==
          "active_monitor_changed");
    CHECK(stage_manager::window::suspend_reason_name(MvpSuspendReason::NoManagedPeer) ==
          "no_managed_peer");
    CHECK(stage_manager::window::suspend_reason_name(MvpSuspendReason::SolverFailure) ==
          "solver_failure");
    CHECK(stage_manager::window::suspend_reason_name(MvpSuspendReason::ApplyFailure) ==
          "apply_failure");
    CHECK(stage_manager::window::suspend_reason_name(static_cast<MvpSuspendReason>(255)) ==
          "unknown");

    MvpFixture dry;
    dry.desktop.windows = {
        make_window(1, {100, 100, 400, 400}, 0),
        make_window(2, {100, 100, 400, 400}, 1),
        make_window(3, {700, 100, 950, 350}, 2),
    };
    const auto dry_result = dry.coordinator.process(drag_events(1), true, true);
    CHECK(dry_result.status == MvpBatchStatus::DryRun);
    CHECK(dry_result.solve.status == SolveStatus::Solved);
    CHECK(dry_result.solve.moves.size() >= 1);
    CHECK(dry_result.solve.moves[0].window.hwnd == 2);
    CHECK(dry_result.solve.moves[0].to.left != dry_result.solve.moves[0].from.left);
    CHECK(dry_result.solve.moves[0].to.top != dry_result.solve.moves[0].from.top);
    CHECK(dry_result.apply.status == stage_manager::window::MoveApplyStatus::DryRun);
    CHECK(dry.desktop.moveCalls == 0);
    CHECK(dry_result.events.inputCount == 4);
    CHECK(dry_result.events.coalescedLocationCount == 1);
    CHECK(dry_result.managedWindowCount == 3);
    CHECK(dry_result.movedWindowCount >= 1);
    CHECK(!dry_result.partialLayoutUsed);
    CHECK(dry.desktop.reorderCalls == 0);

    MvpFixture filtered_snapshot;
    auto relevant_fixed = make_window(301, {900, 600, 950, 650}, 1);
    relevant_fixed.ownerHwnd = 999;
    auto hidden_noise = make_window(303, {100, 100, 300, 300}, 3);
    hidden_noise.visible = false;
    auto minimized_noise = make_window(304, {100, 100, 300, 300}, 4);
    minimized_noise.iconic = true;
    auto cloaked_noise = make_window(305, {100, 100, 300, 300}, 5);
    cloaked_noise.cloaked = true;
    cloaked_noise.ownerHwnd = 999;
    auto other_desktop_noise = make_window(306, {100, 100, 300, 300}, 6);
    other_desktop_noise.currentDesktop = false;
    auto other_monitor_noise = make_window(307, {1100, 100, 1300, 300}, 7);
    other_monitor_noise.monitor = 2;
    other_monitor_noise.workArea = {1000, 0, 2000, 700};
    auto outside_work_area_noise = make_window(308, {0, 700, 1000, 750}, 8);
    outside_work_area_noise.ownerHwnd = 999;
    auto behind_all_noise = make_window(309, {100, 100, 300, 300}, 9);
    behind_all_noise.ownerHwnd = 999;
    filtered_snapshot.desktop.windows = {
        make_window(300, {0, 0, 200, 200}, 0),
        relevant_fixed,
        make_window(302, {400, 0, 600, 200}, 2),
        hidden_noise,
        minimized_noise,
        cloaked_noise,
        other_desktop_noise,
        other_monitor_noise,
        outside_work_area_noise,
        behind_all_noise,
    };
    const auto filtered_result =
        filtered_snapshot.coordinator.process(drag_events(300), true, true);
    CHECK(filtered_result.status == MvpBatchStatus::Idle);
    CHECK(filtered_result.solve.status == SolveStatus::NoViolation);
    CHECK(filtered_result.snapshotWindowCount == 10);
    CHECK(filtered_result.managedWindowCount == 2);
    CHECK(filtered_result.blockingWindowCount == 1);
    CHECK(filtered_result.solverWindowCount == 3);
    CHECK(filtered_result.solve.finalSnapshot.windows.size() == 3);
    CHECK(std::any_of(filtered_result.solve.finalSnapshot.windows.begin(),
                      filtered_result.solve.finalSnapshot.windows.end(),
                      [](const auto& item) { return item.key.hwnd == 301; }));
    const std::vector<WindowEvent> duplicate_end = {
        {WindowEventType::MoveSizeEnd, 1, 1, 104, 5},
    };
    const auto duplicate_result = dry.coordinator.process(duplicate_end, true, true);
    CHECK(duplicate_result.status == MvpBatchStatus::DryRun);
    CHECK(duplicate_result.solve.status == SolveStatus::InvalidSnapshot);
    CHECK(duplicate_result.solve.moves.empty());

    MvpFixture chain;
    chain.desktop.windows = {
        make_window(10, {192, 200, 392, 400}, 0),
        make_window(11, {192, 200, 392, 400}, 1),
        make_window(12, {128, 200, 328, 400}, 2),
        make_window(13, {64, 200, 264, 400}, 3),
    };
    const auto chain_result = chain.coordinator.process(drag_events(10), true, true);
    CHECK(chain_result.status == MvpBatchStatus::DryRun);
    CHECK(chain_result.solve.status == SolveStatus::Solved);
    CHECK(chain_result.managedWindowCount == 4);
    CHECK(chain_result.movedWindowCount >= 1);
    CHECK(chain_result.solve.moves.size() >= 1);
    const auto chain_visibility = stage_manager::solver::scan_visibility_violations(
        chain_result.solve.finalSnapshot,
        two_edge_requirements());
    CHECK(chain_visibility.status ==
          stage_manager::solver::ViolationScanStatus::Ok);
    CHECK(chain_visibility.violations.empty());

    auto stacked_settings = test_settings();
    stacked_settings.maxSolverStates = 16;
    MvpFixture stacked(stacked_settings);
    stacked.desktop.windows = {
        make_window(40, {100, 200, 400, 500}, 0),
        make_window(41, {100, 200, 400, 500}, 1),
        make_window(42, {100, 200, 400, 500}, 2),
        make_window(43, {100, 200, 400, 500}, 3),
    };
    const auto stacked_result =
        stacked.coordinator.process(drag_events(40), true, true);
    CHECK(stacked_result.status == MvpBatchStatus::DryRun);
    CHECK(!stacked_result.fallbackUsed);
    CHECK(stacked_result.managedWindowCount == 4);
    CHECK(stacked_result.solve.status == SolveStatus::Solved ||
          stacked_result.solve.status == SolveStatus::PartiallySolved);
    CHECK(stacked_result.solve.moves.size() >= 1);
    const auto stacked_visibility = stage_manager::solver::scan_visibility_violations(
        stacked_result.solve.finalSnapshot,
        two_edge_requirements());
    CHECK(stacked_visibility.status ==
          stage_manager::solver::ViolationScanStatus::Ok);
    CHECK(stacked_visibility.violations.size() <= stacked_result.solve.violations.size());

    MvpFixture ordered_fallback;
    ordered_fallback.desktop.windows.push_back(
        make_window(44, {400, 150, 700, 450}, 0));
    for (std::uintptr_t hwnd = 45; hwnd <= 50; ++hwnd) {
        ordered_fallback.desktop.windows.push_back(make_window(
            hwnd, {400, 150, 700, 450}, static_cast<std::int32_t>(hwnd - 44)));
    }
    const auto ordered_fallback_result =
        ordered_fallback.coordinator.process(drag_events(44), true, true);
    CHECK(ordered_fallback_result.status == MvpBatchStatus::DryRun);
    CHECK(!ordered_fallback_result.affordanceGoalDegraded);
    CHECK(ordered_fallback_result.affordanceGoal == VisibilityGoal::TitleBarLeftHalf);
    CHECK(ordered_fallback_result.solve.status == SolveStatus::Solved);
    CHECK(ordered_fallback_result.solve.moves.size() == 6);
    const auto& fallback_windows = ordered_fallback_result.solve.finalSnapshot.windows;
    CHECK(stage_manager::solver::scan_visibility_violations(
        ordered_fallback_result.solve.finalSnapshot, two_edge_requirements()).violations.empty());
    for (std::size_t index = 1; index < fallback_windows.size(); ++index) {
        CHECK(fallback_windows[index].workArea.contains(fallback_windows[index].placementRect));
        CHECK(fallback_windows[index].zIndex == static_cast<std::int32_t>(index));
    }

    MvpFixture two_edge_goal;
    two_edge_goal.desktop.windows = {
        make_window(14, {130, 50, 430, 450}, 0),
        make_window(15, {100, 100, 400, 400}, 1),
    };
    const auto two_edge_result =
        two_edge_goal.coordinator.process(drag_events(14), true, true);
    CHECK(two_edge_result.status == MvpBatchStatus::DryRun);
    CHECK(!two_edge_result.affordanceGoalDegraded);
    CHECK(two_edge_result.affordanceGoal == VisibilityGoal::TitleBarLeftHalf);
    CHECK(two_edge_result.solve.status == SolveStatus::Solved);
    CHECK(two_edge_result.solve.moves.size() == 1);
    CHECK(two_edge_result.solve.moves[0].window.hwnd == 15);

    auto one_edge_settings = test_settings();
    one_edge_settings.maxSolverStates = 1;
    MvpFixture one_edge_fallback(one_edge_settings);
    auto fixed_top = make_window(16, {100, 100, 300, 124}, 1);
    fixed_top.topmost = true;
    auto fixed_bottom = make_window(17, {100, 276, 300, 300}, 2);
    fixed_bottom.topmost = true;
    one_edge_fallback.desktop.windows = {
        make_window(18, {100, 124, 276, 276}, 0),
        fixed_top,
        fixed_bottom,
        make_window(19, {100, 100, 300, 300}, 3),
    };
    const auto one_edge_result =
        one_edge_fallback.coordinator.process(drag_events(18), true, true);
    CHECK(one_edge_result.status == MvpBatchStatus::DryRun);
    CHECK(!one_edge_result.affordanceGoalDegraded);
    CHECK(one_edge_result.affordanceGoal == VisibilityGoal::TitleBarLeftHalf);
    CHECK(one_edge_result.solve.status == SolveStatus::Solved);
    CHECK(one_edge_result.solve.moves.size() == 1);
    CHECK(stage_manager::solver::scan_visibility_violations(
        one_edge_result.solve.finalSnapshot, two_edge_requirements()).violations.empty());

    auto z_order_settings = test_settings();
    z_order_settings.maxManagedWindows = 3;
    z_order_settings.minOnscreenWidthDip = 900;
    z_order_settings.minOnscreenHeightDip = 100;
    auto make_z_order_layout = [] {
        auto blocker = make_window(202, {0, 0, 1000, 700}, 2);
        blocker.ownerHwnd = 999;
        return std::vector{
            make_window(201, {50, 300, 950, 400}, 0),
            make_window(204, {50, 0, 950, 100}, 1),
            blocker,
            make_window(203, {50, 50, 950, 650}, 3),
        };
    };

    MvpFixture immutable_z_order(z_order_settings);
    immutable_z_order.desktop.windows = make_z_order_layout();
    const auto impossible_result =
        immutable_z_order.coordinator.process(drag_events(201), true, false);
    CHECK(impossible_result.status == MvpBatchStatus::Unsatisfiable);
    CHECK(impossible_result.solve.status == SolveStatus::Unsatisfiable);
    CHECK(!impossible_result.partialLayoutUsed);
    CHECK(impossible_result.solve.moves.empty());
    CHECK(immutable_z_order.desktop.moveCalls == 0);
    CHECK(immutable_z_order.desktop.reorderCalls == 0);
    CHECK(immutable_z_order.desktop.windows[0].zIndex == 0);
    CHECK(immutable_z_order.desktop.windows[1].zIndex == 1);
    CHECK(immutable_z_order.desktop.windows[2].zIndex == 2);
    CHECK(immutable_z_order.desktop.windows[3].zIndex == 3);

    MvpFixture activation_without_peer_solution(z_order_settings);
    activation_without_peer_solution.desktop.windows = make_z_order_layout();
    activation_without_peer_solution.desktop.windows[0].placementRect =
        {0, 300, 900, 400};
    activation_without_peer_solution.desktop.windows[0].visualRect =
        {0, 300, 900, 400};
    const auto activation_only_dry = activation_without_peer_solution.coordinator.process(
        foreground_event(201), true, true);
    CHECK(activation_only_dry.status == MvpBatchStatus::DryRun);
    CHECK(activation_only_dry.solve.status == SolveStatus::PartiallySolved);
    CHECK(activation_only_dry.activationPlacementUsed);
    CHECK(activation_only_dry.partialLayoutUsed);
    CHECK(activation_only_dry.solve.moves.size() == 1);
    CHECK(activation_only_dry.solve.moves[0].window.hwnd == 201);
    CHECK((activation_only_dry.solve.moves[0].to ==
           stage_manager::geometry::Rect{50, 600, 950, 700}));
    CHECK(!activation_only_dry.solve.violations.empty());
    CHECK(activation_without_peer_solution.desktop.moveCalls == 0);
    CHECK(activation_without_peer_solution.desktop.reorderCalls == 0);

    MvpFixture activation_without_peer_solution_live(z_order_settings);
    activation_without_peer_solution_live.desktop.windows = make_z_order_layout();
    activation_without_peer_solution_live.desktop.windows[0].placementRect =
        {0, 300, 900, 400};
    activation_without_peer_solution_live.desktop.windows[0].visualRect =
        {0, 300, 900, 400};
    const auto activation_only_live =
        activation_without_peer_solution_live.coordinator.process(
            foreground_event(201), true, false);
    CHECK(activation_only_live.status == MvpBatchStatus::PartiallySolved);
    CHECK(activation_only_live.solve.status == SolveStatus::PartiallySolved);
    CHECK(activation_only_live.activationPlacementUsed);
    CHECK(activation_only_live.apply.status ==
          stage_manager::window::MoveApplyStatus::Applied);
    CHECK(activation_only_live.apply.appliedMoves.size() == 1);
    CHECK(activation_without_peer_solution_live.desktop.moveCalls == 1);
    CHECK(activation_without_peer_solution_live.desktop.reorderCalls == 0);
    CHECK(activation_without_peer_solution_live.desktop.windows[0].placementRect.left == 50);
    CHECK(activation_without_peer_solution_live.desktop.windows[0].placementRect.top == 600);
    CHECK(activation_without_peer_solution_live.desktop.windows[0].zIndex == 0);

    MvpFixture crowded;
    auto fixed_blocker = make_window(22, {0, 0, 1000, 700}, 2);
    fixed_blocker.ownerHwnd = 999;
    crowded.desktop.windows = {
        make_window(20, {100, 100, 400, 400}, 0),
        make_window(21, {100, 100, 400, 400}, 1),
        fixed_blocker,
        make_window(23, {0, 0, 1000, 700}, 3),
    };
    const auto crowded_result = crowded.coordinator.process(drag_events(20), true, true);
    CHECK(crowded_result.status == MvpBatchStatus::DryRun);
    CHECK(!crowded_result.fallbackUsed);
    CHECK(crowded_result.partialLayoutUsed);
    CHECK(crowded_result.managedWindowCount == 3);
    CHECK(crowded_result.solve.status == SolveStatus::PartiallySolved);
    CHECK(crowded_result.solve.moves.size() == 1);
    CHECK(crowded_result.solve.moves[0].window.hwnd == 21);
    CHECK(crowded_result.solve.violations.size() == 1);
    CHECK(crowded_result.solve.finalSnapshot.windows[
              crowded_result.solve.violations[0].targetIndex].key.hwnd == 23);
    CHECK(crowded.desktop.reorderCalls == 0);

    MvpFixture crowded_live;
    crowded_live.desktop.windows = crowded.desktop.windows;
    const auto crowded_live_result =
        crowded_live.coordinator.process(drag_events(20), true, false);
    CHECK(crowded_live_result.status == MvpBatchStatus::PartiallySolved);
    CHECK(!crowded_live_result.fallbackUsed);
    CHECK(crowded_live_result.partialLayoutUsed);
    CHECK(crowded_live_result.apply.appliedMoves.size() == 1);
    CHECK(crowded_live_result.apply.appliedReorders.empty());
    CHECK(crowded_live.desktop.moveCalls == 1);
    CHECK(crowded_live.desktop.reorderCalls == 0);
    CHECK(crowded_live.desktop.windows[0].zIndex == 0);
    CHECK(crowded_live.desktop.windows[1].zIndex == 1);
    CHECK(crowded_live.desktop.windows[2].zIndex == 2);
    CHECK(crowded_live.desktop.windows[3].zIndex == 3);

    MvpFixture partial_z_order_drift;
    partial_z_order_drift.desktop.windows = crowded.desktop.windows;
    partial_z_order_drift.desktop.changeZOrderAtCapture = 5;
    partial_z_order_drift.desktop.zOrderChangedWindow = 23;
    const auto partial_drift_result =
        partial_z_order_drift.coordinator.process(drag_events(20), true, false);
    CHECK(partial_drift_result.status == MvpBatchStatus::ApiError);
    CHECK(partial_drift_result.reason == MvpSuspendReason::ApplyFailure);
    CHECK(partial_drift_result.apply.status ==
          stage_manager::window::MoveApplyStatus::VerificationFailed);
    CHECK(partial_drift_result.apply.requiresReconcile);
    CHECK(partial_z_order_drift.desktop.reorderCalls == 0);

    MvpFixture capped;
    for (std::uintptr_t index = 0; index < 21; ++index) {
        const auto left = static_cast<std::int32_t>(index * 300);
        auto window = make_window(100 + index, {left, 100, left + 200, 300},
                                  static_cast<std::int32_t>(index));
        window.workArea = {0, 0, 10000, 10000};
        capped.desktop.windows.push_back(window);
    }
    const auto capped_result = capped.coordinator.process(drag_events(100), true, true);
    CHECK(capped_result.status == MvpBatchStatus::Idle);
    CHECK(capped_result.solve.status == SolveStatus::NoViolation);
    CHECK(capped_result.managedWindowCount == 20);

    auto prioritized_settings = test_settings();
    prioritized_settings.maxManagedWindows = 2;
    MvpFixture prioritized(prioritized_settings);
    prioritized.desktop.windows = {
        make_window(130, {700, 100, 950, 350}, 0),
        make_window(131, {100, 100, 400, 400}, 1),
        make_window(132, {100, 100, 400, 400}, 2),
    };
    const auto prioritized_result =
        prioritized.coordinator.process(drag_events(131), true, true);
    CHECK(prioritized_result.status == MvpBatchStatus::DryRun);
    CHECK(prioritized_result.solve.status == SolveStatus::Solved);
    CHECK(prioritized_result.managedWindowCount == 2);
    CHECK(prioritized_result.solve.moves.size() == 1);
    CHECK(prioritized_result.solve.moves[0].window.hwnd == 132);
    CHECK(prioritized_result.activeWindow && prioritized_result.activeWindow->hwnd == 131);

    auto repeated_type_settings = test_settings();
    repeated_type_settings.maxManagedWindows = 3;
    MvpFixture repeated_type_priority(repeated_type_settings);
    auto single_one = make_window(301, {300, 200, 600, 500}, 1);
    single_one.className = L"SingleOne";
    auto single_two = make_window(302, {300, 200, 600, 500}, 2);
    single_two.className = L"SingleTwo";
    auto explorer_one = make_window(303, {300, 200, 600, 500}, 3);
    explorer_one.key.processId = 900;
    explorer_one.className = L"CabinetWClass";
    auto explorer_two = make_window(304, {300, 200, 600, 500}, 4);
    explorer_two.key.processId = 900;
    explorer_two.className = L"CabinetWClass";
    repeated_type_priority.desktop.windows = {
        make_window(300, {300, 200, 600, 500}, 0),
        single_one,
        single_two,
        explorer_one,
        explorer_two,
    };
    const auto repeated_type_result = repeated_type_priority.coordinator.process(
        drag_events(300), true, true);
    CHECK(repeated_type_result.status == MvpBatchStatus::DryRun);
    CHECK(repeated_type_result.managedWindowCount == 3);
    CHECK(repeated_type_result.solve.status == SolveStatus::Solved);
    CHECK(repeated_type_result.solve.moves.size() == 2);
    CHECK(repeated_type_result.solve.moves[0].window.hwnd == 301);
    CHECK(repeated_type_result.solve.moves[1].window.hwnd == 303);
    CHECK(repeated_type_result.solve.moves[0].cost.placementDirection ==
          stage_manager::solver::PlacementDirectionRank::TopLeft);
    // The older title may align above its predecessor instead of extending
    // the left staircase; recency proximity outranks an extra side strip.
    CHECK(repeated_type_result.solve.moves[1].to.top < repeated_type_result.solve.moves[0].to.top);
    CHECK(std::abs(repeated_type_result.solve.moves[1].to.left - 300) <= repeated_type_settings.leftDepthDip);

    MvpFixture coverage_prioritized(prioritized_settings);
    coverage_prioritized.desktop.windows = {
        make_window(140, {100, 100, 400, 400}, 0),
        make_window(141, {350, 100, 650, 400}, 1),
        make_window(142, {100, 100, 400, 400}, 2),
    };
    const auto coverage_result =
        coverage_prioritized.coordinator.process(drag_events(140), true, true);
    CHECK(coverage_result.status == MvpBatchStatus::DryRun);
    CHECK(coverage_result.solve.status == SolveStatus::Solved);
    CHECK(coverage_result.managedWindowCount == 2);
    CHECK(coverage_result.solve.moves.size() == 1);
    CHECK(coverage_result.solve.moves[0].window.hwnd == 141);

    MvpFixture activated;
    activated.desktop.windows = {
        make_window(150, {100, 100, 400, 400}, 0),
        make_window(151, {100, 100, 400, 400}, 1),
    };
    const auto activated_result =
        activated.coordinator.process(foreground_event(150), true, true);
    CHECK(activated_result.status == MvpBatchStatus::DryRun);
    CHECK(activated_result.solve.status == SolveStatus::Solved);
    CHECK(activated_result.transactionId == 1);
    CHECK(activated_result.managedWindowCount == 2);
    CHECK(activated_result.solve.moves.size() == 2);
    CHECK(activated_result.solve.moves[0].window.hwnd == 150);
    CHECK((activated_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{350, 400, 650, 700}));
    CHECK(activated_result.activationPlacementUsed);
    CHECK(activated_result.apply.status == stage_manager::window::MoveApplyStatus::DryRun);
    CHECK(activated.desktop.moveCalls == 0);

    auto right_bottom_settings = test_settings();
    right_bottom_settings.activationHorizontalAlignment =
        stage_manager::app::ActivationHorizontalAlignment::Right;
    right_bottom_settings.activationVerticalAlignment =
        stage_manager::app::ActivationVerticalAlignment::Bottom;
    MvpFixture right_bottom(right_bottom_settings);
    right_bottom.desktop.windows = {make_window(154, {0, 0, 200, 200}, 0)};
    const auto right_bottom_result =
        right_bottom.coordinator.process(foreground_event(154), true, false);
    CHECK(right_bottom_result.status == MvpBatchStatus::Applied);
    CHECK(right_bottom_result.activationPlacementUsed);
    CHECK(right_bottom_result.solve.moves.size() == 1);
    CHECK((right_bottom_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{800, 500, 1000, 700}));
    CHECK(right_bottom.desktop.moveCalls == 1);

    auto centered_settings = test_settings();
    centered_settings.activationHorizontalAlignment =
        stage_manager::app::ActivationHorizontalAlignment::Center;
    centered_settings.activationVerticalAlignment =
        stage_manager::app::ActivationVerticalAlignment::Center;
    MvpFixture fully_centered(centered_settings);
    fully_centered.desktop.windows = {make_window(155, {0, 0, 200, 200}, 0)};
    const auto fully_centered_result =
        fully_centered.coordinator.process(foreground_event(155), true, true);
    CHECK(fully_centered_result.status == MvpBatchStatus::DryRun);
    CHECK((fully_centered_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{400, 250, 600, 450}));

    auto centering_disabled_settings = test_settings();
    centering_disabled_settings.placeActivatedWindow = false;
    MvpFixture centering_disabled(centering_disabled_settings);
    centering_disabled.desktop.windows = {
        make_window(152, {100, 100, 400, 400}, 0),
        make_window(153, {100, 100, 400, 400}, 1),
    };
    const auto centering_disabled_result =
        centering_disabled.coordinator.process(foreground_event(152), true, true);
    CHECK(centering_disabled_result.status == MvpBatchStatus::DryRun);
    CHECK(!centering_disabled_result.activationPlacementUsed);
    CHECK(!centering_disabled_result.solve.moves.empty());
    CHECK(std::none_of(centering_disabled_result.solve.moves.begin(),
                       centering_disabled_result.solve.moves.end(),
                       [](const auto& move) { return move.window.hwnd == 152; }));
    CHECK(centering_disabled_result.solve.moves.front().window.hwnd == 153);
    CHECK(centering_disabled.desktop.moveCalls == 0);

    MvpFixture activation_centering;
    activation_centering.desktop.windows = {
        make_window(160, {0, 0, 200, 200}, 0),
    };
    const auto centered_result =
        activation_centering.coordinator.process(foreground_event(160), true, false);
    CHECK(centered_result.status == MvpBatchStatus::Applied);
    CHECK(centered_result.activationPlacementUsed);
    CHECK(centered_result.solve.moves.size() == 1);
    CHECK(centered_result.solve.moves[0].window.hwnd == 160);
    CHECK((centered_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{400, 500, 600, 700}));
    CHECK(activation_centering.desktop.moveCalls == 1);

    const auto duplicate_foreground =
        activation_centering.coordinator.process(foreground_event(160), true, false);
    CHECK(duplicate_foreground.transactionId == centered_result.transactionId);
    CHECK(duplicate_foreground.solve.moves.empty());
    CHECK(!duplicate_foreground.activationPlacementUsed);
    CHECK(activation_centering.desktop.moveCalls == 1);

    activation_centering.desktop.windows[0].placementRect = {50, 50, 250, 250};
    activation_centering.desktop.windows[0].visualRect = {50, 50, 250, 250};
    const auto manually_moved =
        activation_centering.coordinator.process(drag_events(160), true, false);
    CHECK(manually_moved.status == MvpBatchStatus::Idle);
    CHECK(!manually_moved.activationPlacementUsed);
    CHECK(activation_centering.desktop.moveCalls == 1);
    CHECK(activation_centering.desktop.windows[0].placementRect.left == 50);
    CHECK(activation_centering.desktop.windows[0].placementRect.top == 50);
    CHECK(activation_centering.desktop.windows[0].placementRect.right == 250);
    CHECK(activation_centering.desktop.windows[0].placementRect.bottom == 250);

    MvpFixture activated_by_drag;
    activated_by_drag.desktop.windows = {
        make_window(164, {50, 50, 250, 250}, 0),
    };
    const std::vector<WindowEvent> activated_drag_start = {
        {WindowEventType::Foreground, 164, 1, 100, 1},
        {WindowEventType::MoveSizeStart, 164, 1, 101, 2},
    };
    const auto activated_drag_started = activated_by_drag.coordinator.process(
        activated_drag_start, true, false);
    CHECK(activated_drag_started.status == MvpBatchStatus::Dragging);
    activated_by_drag.desktop.windows[0].placementRect = {80, 70, 280, 270};
    activated_by_drag.desktop.windows[0].visualRect = {80, 70, 280, 270};
    const std::vector<WindowEvent> activated_drag_end = {
        {WindowEventType::LocationChange, 164, 1, 102, 3},
        {WindowEventType::MoveSizeEnd, 164, 1, 103, 4},
    };
    const auto activated_by_drag_result = activated_by_drag.coordinator.process(
        activated_drag_end, true, false);
    CHECK(activated_by_drag_result.status == MvpBatchStatus::Idle);
    CHECK(!activated_by_drag_result.activationPlacementUsed);
    CHECK(activated_by_drag_result.solve.moves.empty());
    CHECK(activated_by_drag.desktop.moveCalls == 0);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.left == 80);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.top == 70);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.right == 280);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.bottom == 270);

    MvpFixture quick_activated_drag;
    quick_activated_drag.desktop.windows = {
        make_window(178, {50, 50, 250, 250}, 0),
    };
    const std::vector<WindowEvent> quick_drag_events = {
        {WindowEventType::Foreground, 178, 1, 100, 1},
        {WindowEventType::MoveSizeStart, 178, 1, 101, 2},
        {WindowEventType::LocationChange, 178, 1, 102, 3},
        {WindowEventType::MoveSizeEnd, 178, 1, 103, 4},
    };
    const auto quick_drag_result = quick_activated_drag.coordinator.process(
        quick_drag_events, true, false);
    CHECK(quick_drag_result.status == MvpBatchStatus::Idle);
    CHECK(!quick_drag_result.activationPlacementUsed);
    CHECK(quick_drag_result.solve.moves.empty());
    CHECK(quick_activated_drag.desktop.moveCalls == 0);

    const auto activate_from_border_without_moving = [](bool foreground_first) {
        MvpFixture fixture;
        fixture.desktop.windows = {
            make_window(179, {50, 50, 250, 250}, 0),
        };
        std::vector<WindowEvent> border_click;
        if (foreground_first) {
            border_click = {
                {WindowEventType::Foreground, 179, 1, 100, 1},
                {WindowEventType::MoveSizeStart, 179, 1, 101, 2},
            };
        } else {
            border_click = {
                {WindowEventType::MoveSizeStart, 179, 1, 100, 1},
                {WindowEventType::Foreground, 179, 1, 101, 2},
            };
        }
        const auto started = fixture.coordinator.process(border_click, true, false);
        CHECK(started.status == MvpBatchStatus::Dragging);
        CHECK(fixture.desktop.moveCalls == 0);
        const std::vector<WindowEvent> released = {
            {WindowEventType::MoveSizeEnd, 179, 1, 102, 3},
        };
        const auto result = fixture.coordinator.process(released, true, false);
        CHECK(result.status == MvpBatchStatus::Applied);
        CHECK(result.activationPlacementUsed);
        CHECK(result.solve.moves.size() == 1);
        CHECK(result.solve.moves[0].window.hwnd == 179);
        CHECK((result.solve.moves[0].to ==
               stage_manager::geometry::Rect{400, 500, 600, 700}));
        CHECK(fixture.desktop.moveCalls == 1);
        return 0;
    };
    CHECK(activate_from_border_without_moving(true) == 0);
    CHECK(activate_from_border_without_moving(false) == 0);

    MvpFixture right_click_activation;
    right_click_activation.desktop.windows = {
        make_window(190, {50, 50, 250, 250}, 0),
        make_window(191, {50, 50, 250, 250}, 1),
    };
    const std::vector<WindowEvent> right_click_first = {
        {WindowEventType::Foreground, 190, 1, 100, 1, true},
    };
    const auto right_click_first_result = right_click_activation.coordinator.process(
        right_click_first, true, false);
    CHECK(right_click_first_result.status == MvpBatchStatus::Idle);
    CHECK(right_click_first_result.activationLayoutSuppressed);
    CHECK(!right_click_first_result.activationPlacementUsed);
    CHECK(right_click_first_result.transactionId == 0);
    CHECK(right_click_activation.desktop.captureCalls == 0);
    CHECK(right_click_activation.desktop.moveCalls == 0);

    const auto duplicate_left_click = right_click_activation.coordinator.process(
        foreground_event(190), true, false);
    CHECK(duplicate_left_click.status == MvpBatchStatus::Idle);
    CHECK(!duplicate_left_click.activationLayoutSuppressed);
    CHECK(!duplicate_left_click.activationPlacementUsed);
    CHECK(right_click_activation.desktop.captureCalls == 0);
    CHECK(right_click_activation.desktop.moveCalls == 0);

    const auto context_menu_foreground = right_click_activation.coordinator.process(
        foreground_event(999), true, false);
    CHECK(context_menu_foreground.status == MvpBatchStatus::Suspended);
    CHECK(context_menu_foreground.reason == MvpSuspendReason::ActiveWindowUnavailable);
    CHECK(!context_menu_foreground.activationLayoutSuppressed);
    CHECK(right_click_activation.desktop.captureCalls == 3);
    CHECK(right_click_activation.desktop.moveCalls == 0);
    const auto context_menu_closed = right_click_activation.coordinator.process(
        foreground_event(190), true, false);
    CHECK(context_menu_closed.status == MvpBatchStatus::Idle);
    CHECK(context_menu_closed.activationLayoutSuppressed);
    CHECK(!context_menu_closed.activationPlacementUsed);
    CHECK(context_menu_closed.transactionId == context_menu_foreground.transactionId);
    CHECK(right_click_activation.desktop.captureCalls == 3);
    CHECK(right_click_activation.desktop.moveCalls == 0);

    const std::vector<WindowEvent> right_click_second = {
        {WindowEventType::Foreground, 191, 1, 101, 2, true},
    };
    const auto right_click_second_result = right_click_activation.coordinator.process(
        right_click_second, true, false);
    CHECK(right_click_second_result.activationLayoutSuppressed);
    CHECK(right_click_activation.desktop.captureCalls == 3);
    CHECK(right_click_activation.desktop.moveCalls == 0);
    const auto later_left_click = right_click_activation.coordinator.process(
        foreground_event(190), true, false);
    CHECK(later_left_click.transactionId == 2);
    CHECK(later_left_click.activationPlacementUsed);
    CHECK(right_click_activation.desktop.captureCalls >= 2);
    CHECK(right_click_activation.desktop.moveCalls >= 1);

    MvpFixture activation_capture_retry;
    activation_capture_retry.desktop.windows = {
        make_window(167, {0, 0, 200, 200}, 0),
    };
    activation_capture_retry.desktop.omitWindowAtCapture = 1;
    activation_capture_retry.desktop.transientWindow = 167;
    const auto retried_missing_activation = activation_capture_retry.coordinator.process(
        foreground_event(167), true, true);
    CHECK(retried_missing_activation.status == MvpBatchStatus::DryRun);
    CHECK(retried_missing_activation.activationPlacementUsed);
    CHECK(activation_capture_retry.desktop.captureCalls == 2);

    MvpFixture delayed_activation_retry;
    delayed_activation_retry.desktop.windows = {
        make_window(166, {100, 100, 400, 400}, 0),
        make_window(165, {100, 100, 400, 400}, 1),
    };
    delayed_activation_retry.desktop.omitWindowThroughCapture = 3;
    delayed_activation_retry.desktop.transientWindow = 166;
    const auto initially_missing_activation = delayed_activation_retry.coordinator.process(
        foreground_event(166), true, true);
    CHECK(initially_missing_activation.status == MvpBatchStatus::Suspended);
    CHECK(initially_missing_activation.reason == MvpSuspendReason::ActiveWindowUnavailable);
    CHECK(delayed_activation_retry.desktop.captureCalls == 3);
    const std::vector<WindowEvent> retry_reconcile = {
        {WindowEventType::Reconcile, 0, 0, 200, 2},
    };
    const auto recovered_activation = delayed_activation_retry.coordinator.process(
        retry_reconcile, true, true);
    CHECK(recovered_activation.status == MvpBatchStatus::DryRun);
    CHECK(recovered_activation.solveAttempted);
    CHECK(recovered_activation.activationPlacementUsed);
    CHECK(delayed_activation_retry.desktop.captureCalls == 4);

    MvpFixture activation_query_retry;
    activation_query_retry.desktop.windows = {
        make_window(168, {0, 0, 200, 200}, 0),
    };
    activation_query_retry.desktop.failWindowQueriesAtCapture = 1;
    activation_query_retry.desktop.transientWindow = 168;
    const auto retried_query_activation = activation_query_retry.coordinator.process(
        foreground_event(168), true, true);
    CHECK(retried_query_activation.status == MvpBatchStatus::DryRun);
    CHECK(retried_query_activation.activationPlacementUsed);
    CHECK(activation_query_retry.desktop.captureCalls == 2);

    MvpFixture activation_snapshot_reuse;
    activation_snapshot_reuse.desktop.windows = {
        make_window(169, {0, 0, 200, 200}, 0),
    };
    activation_snapshot_reuse.desktop.omitWindowAtCapture = 2;
    activation_snapshot_reuse.desktop.transientWindow = 169;
    const auto reused_activation = activation_snapshot_reuse.coordinator.process(
        foreground_event(169), true, true);
    CHECK(reused_activation.status == MvpBatchStatus::DryRun);
    CHECK(reused_activation.activationPlacementUsed);
    CHECK(activation_snapshot_reuse.desktop.captureCalls == 1);

    const auto switch_after_drag = [](bool include_move_end) {
        MvpFixture fixture;
        fixture.desktop.windows = {
            make_window(181, {0, 0, 200, 200}, 0),
            make_window(180, {400, 500, 600, 700}, 1),
        };
        const std::vector<WindowEvent> start = {
            {WindowEventType::MoveSizeStart, 180, 1, 100, 1},
        };
        const auto started = fixture.coordinator.process(start, true, false);
        CHECK(started.status == MvpBatchStatus::Dragging);
        std::vector<WindowEvent> switched;
        if (include_move_end) {
            switched.push_back({WindowEventType::MoveSizeEnd, 180, 1, 101, 2});
        }
        switched.push_back({WindowEventType::Foreground, 181, 1, 102, 3});
        const auto result = fixture.coordinator.process(switched, true, false);
        CHECK(result.status == MvpBatchStatus::Applied ||
              result.status == MvpBatchStatus::PartiallySolved);
        CHECK(result.activationPlacementUsed);
        CHECK(std::any_of(result.solve.moves.begin(), result.solve.moves.end(),
                          [](const auto& move) { return move.window.hwnd == 181; }));
        CHECK(fixture.desktop.windows[0].placementRect.left == 400);
        CHECK(fixture.desktop.windows[0].placementRect.top == 500);
        return 0;
    };
    CHECK(switch_after_drag(true) == 0);
    CHECK(switch_after_drag(false) == 0);

    MvpFixture current_monitor_center;
    auto offset_frame = make_window(165, {1090, 90, 1310, 310}, 0);
    offset_frame.visualRect = {1100, 100, 1300, 300};
    offset_frame.workArea = {1000, 0, 2000, 700};
    offset_frame.monitor = 2;
    current_monitor_center.desktop.windows = {offset_frame};
    const auto current_monitor_result = current_monitor_center.coordinator.process(
        foreground_event(165), true, true);
    CHECK(current_monitor_result.status == MvpBatchStatus::DryRun);
    CHECK(current_monitor_result.activationPlacementUsed);
    CHECK(current_monitor_result.solve.moves.size() == 1);
    CHECK((current_monitor_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{1390, 480, 1610, 700}));
    CHECK(current_monitor_center.desktop.moveCalls == 0);

    MvpFixture already_centered;
    already_centered.desktop.windows = {
        make_window(161, {400, 500, 600, 700}, 0),
    };
    const auto already_centered_result =
        already_centered.coordinator.process(foreground_event(161), true, false);
    CHECK(already_centered_result.status == MvpBatchStatus::Idle);
    CHECK(already_centered_result.solve.status == SolveStatus::NoViolation);
    CHECK(already_centered_result.solve.moves.empty());
    CHECK(!already_centered_result.activationPlacementUsed);
    CHECK(already_centered.desktop.moveCalls == 0);

    MvpFixture over_tall_activation;
    over_tall_activation.desktop.windows = {
        make_window(166, {0, 100, 200, 900}, 0),
    };
    const auto over_tall_result = over_tall_activation.coordinator.process(
        foreground_event(166), true, true);
    CHECK(over_tall_result.status == MvpBatchStatus::Idle);
    CHECK(!over_tall_result.activationPlacementUsed);
    CHECK(over_tall_result.solve.moves.empty());

    MvpFixture center_and_repair;
    center_and_repair.desktop.windows = {
        make_window(162, {0, 0, 300, 300}, 0),
        make_window(163, {350, 200, 650, 500}, 1),
    };
    const auto center_and_repair_result =
        center_and_repair.coordinator.process(foreground_event(162), true, true);
    CHECK(center_and_repair_result.status == MvpBatchStatus::DryRun);
    CHECK(center_and_repair_result.solve.status == SolveStatus::Solved);
    CHECK(center_and_repair_result.activationPlacementUsed);
    CHECK(center_and_repair_result.solve.moves.size() == 2);
    CHECK(center_and_repair_result.solve.moves[0].window.hwnd == 162);
    CHECK((center_and_repair_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{350, 400, 650, 700}));
    CHECK(center_and_repair_result.solve.moves[1].window.hwnd == 163);
    CHECK(stage_manager::solver::scan_visibility_violations(
        center_and_repair_result.solve.finalSnapshot, two_edge_requirements()).violations.empty());
    CHECK(center_and_repair_result.movedWindowCount == 2);
    CHECK(center_and_repair.desktop.moveCalls == 0);

    auto one_move_settings = test_settings();
    one_move_settings.maxMovesPerBatch = 1;
    MvpFixture center_budget(one_move_settings);
    center_budget.desktop.windows = center_and_repair.desktop.windows;
    const auto center_budget_result =
        center_budget.coordinator.process(foreground_event(162), true, true);
    CHECK(center_budget_result.status == MvpBatchStatus::DryRun);
    CHECK(center_budget_result.activationPlacementUsed);
    CHECK(center_budget_result.solve.moves.size() == 1);
    CHECK(center_budget.desktop.moveCalls == 0);

    // Activation would hide an initially visible nearest peer, and the single
    // move budget cannot place the active window AND repair that peer.
    // Preserve both real windows instead of accepting activation-only damage.
    for (const bool dry_run : {true, false}) {
        MvpFixture protected_activation(one_move_settings);
        protected_activation.desktop.windows = {
            make_window(180, {0, 0, 300, 300}, 0),
            make_window(181, {350, 450, 650, 650}, 1),
        };
        const auto protected_result = protected_activation.coordinator.process(
            foreground_event(180), true, dry_run);
        CHECK(protected_result.solve.status == SolveStatus::NoViolation);
        CHECK(!protected_result.activationPlacementUsed);
        CHECK(protected_result.solve.moves.empty());
        CHECK(protected_activation.desktop.moveCalls == 0);
        CHECK(protected_result.solve.finalSnapshot.windows[0].placementRect.left == 0);
        CHECK(protected_result.solve.finalSnapshot.windows[1].placementRect.top == 450);
    }

    auto default_profile_settings = stage_manager::app::Settings{};
    default_profile_settings.maxSolveTimeMs = 1000;
    MvpFixture default_profile(default_profile_settings);
    default_profile.desktop.windows = {
        make_window(170, {200, 100, 800, 600}, 0),
        make_window(171, {200, 100, 800, 600}, 1),
    };
    const auto default_profile_result = default_profile.coordinator.process(
        drag_events(170), true, true);
    CHECK(default_profile_result.status == MvpBatchStatus::DryRun);
    CHECK(default_profile_result.affordanceGoal == VisibilityGoal::TitleBarLeftHalf);
    CHECK(!default_profile_result.affordanceGoalDegraded);
    CHECK(default_profile_result.solve.moves.size() == 1);
    CHECK(default_profile_result.solve.moves[0].window.hwnd == 171);
    CHECK((default_profile_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{0, 0, 600, 500}));
    CHECK(default_profile_result.solve.moves[0].cost.placementDirection ==
          stage_manager::solver::PlacementDirectionRank::TopLeft);
    CHECK(stage_manager::solver::analyze_title_bar_exposure(
        default_profile_result.solve.finalSnapshot, 1, two_edge_requirements()).satisfied());

    // A one-move batch continues from fresh readback, without repositioning
    // the active window or rearranging a completed desktop periodically.
    auto retry_settings = test_settings();
    retry_settings.placeActivatedWindow = false;
    retry_settings.maxMovesPerBatch = 1;
    const auto reconcile_at = [](std::uint64_t time) {
        return std::vector<WindowEvent>{{WindowEventType::Reconcile, 0, 0, time, time}};
    };
    const auto retry_windows = std::vector{
        make_window(201, {300, 300, 800, 700}, 0),
        make_window(202, {300, 300, 800, 700}, 1),
        make_window(203, {300, 300, 800, 700}, 2)};
    MvpFixture retry(retry_settings);
    retry.desktop.windows = retry_windows;
    const auto first_repair = retry.coordinator.process(foreground_event(201), true, false);
    CHECK(first_repair.status == MvpBatchStatus::PartiallySolved);
    CHECK(first_repair.backgroundRetryPending);
    CHECK(first_repair.backgroundRetryDelayMs == 500);
    CHECK(retry.desktop.moveCalls == 1);
    CHECK(!retry.coordinator.process(reconcile_at(599), true, false).solveAttempted);
    const auto next_repair = retry.coordinator.process(reconcile_at(600), true, false);
    CHECK(next_repair.backgroundRetryUsed);
    CHECK(!next_repair.activationPlacementUsed);
    CHECK(!next_repair.backgroundRetryPending);
    CHECK(next_repair.status == MvpBatchStatus::Applied);
    CHECK(retry.desktop.moveCalls == 2);
    CHECK(!retry.coordinator.process(reconcile_at(1200), true, false).solveAttempted);
    CHECK(retry.desktop.reorderCalls == 0);
    CHECK(retry.desktop.windows[0].placementRect.left == 300);

    MvpFixture backoff(retry_settings);
    backoff.desktop.windows = {
        make_window(201, {0, 0, 1000, 700}, 0),
        make_window(202, {300, 300, 800, 700}, 1)};
    CHECK(backoff.coordinator.process(foreground_event(201), true, false).backgroundRetryDelayMs == 500);
    CHECK(backoff.coordinator.process(reconcile_at(600), true, false).backgroundRetryDelayMs == 1000);
    CHECK(!backoff.coordinator.process(reconcile_at(1599), true, false).solveAttempted);
    CHECK(backoff.coordinator.process(reconcile_at(1600), true, false).backgroundRetryDelayMs == 2000);
    // The next retry must use the changed geometry, not replay an old plan.
    backoff.desktop.windows[0].placementRect = {300, 300, 800, 700};
    backoff.desktop.windows[0].visualRect = backoff.desktop.windows[0].placementRect;
    CHECK(backoff.coordinator.process(reconcile_at(3600), true, false).status == MvpBatchStatus::Applied);

    for (int cancel_case = 0; cancel_case < 6; ++cancel_case) {
        MvpFixture cancelled(retry_settings);
        cancelled.desktop.windows = retry_windows;
        CHECK(cancelled.coordinator.process(foreground_event(201), true, false).backgroundRetryPending);
        if (cancel_case == 0) {
            const std::vector<WindowEvent> drag{{WindowEventType::MoveSizeStart, 201, 1, 200, 2}};
            cancelled.coordinator.process(drag, true, false);
        } else if (cancel_case == 1) {
            cancelled.coordinator.process({}, false, false);
        } else if (cancel_case == 2) {
            auto suppressed = foreground_event(203);
            suppressed[0].timestampMs = 200;
            suppressed[0].suppressLayout = true;
            cancelled.coordinator.process(suppressed, true, false);
        } else if (cancel_case == 3) {
            cancelled.desktop.windows[0].monitor = 2;
        } else if (cancel_case == 4) {
            cancelled.desktop.windows[0].key.processId += 1;
        } else {
            auto foreign = foreground_event(999);
            foreign[0].timestampMs = 200;
            cancelled.coordinator.process(foreign, true, false);
        }
        CHECK(!cancelled.coordinator.process(reconcile_at(700), true, false).backgroundRetryUsed);
        CHECK(cancelled.desktop.moveCalls == 1);
    }
    MvpFixture quiet(retry_settings);
    quiet.desktop.windows = retry_windows;
    CHECK(quiet.coordinator.process(foreground_event(201), true, false).backgroundRetryPending);
    const std::vector<WindowEvent> external{{WindowEventType::LocationChange, 201, 1, 550, 2}};
    quiet.coordinator.process(external, true, false);
    CHECK(!quiet.coordinator.process(reconcile_at(600), true, false).solveAttempted);
    CHECK(quiet.coordinator.process(reconcile_at(800), true, false).backgroundRetryUsed);
    MvpFixture dry_retry(retry_settings);
    dry_retry.desktop.windows = retry_windows;
    CHECK(!dry_retry.coordinator.process(foreground_event(201), true, true).backgroundRetryPending);
    CHECK(!dry_retry.coordinator.process(reconcile_at(700), true, true).solveAttempted);

    for (int stale_phase = 0; stale_phase < 3; ++stale_phase) {
        MvpFixture stale(retry_settings);
        stale.desktop.windows = retry_windows;
        if (stale_phase == 0) stale.desktop.staleSnapshots = true;
        else stale.desktop.staleAtCapture = stale_phase + 1; // before apply / after first native move
        const auto unavailable = stale.coordinator.process(foreground_event(201), true, false);
        CHECK(unavailable.reason == MvpSuspendReason::SnapshotStale);
        CHECK(unavailable.status == MvpBatchStatus::Rebuilding);
        CHECK(stale.desktop.moveCalls == (stale_phase == 2 ? 1 : 0));
        stale.desktop.staleSnapshots = false;
        stale.desktop.staleAtCapture.reset();
        const auto resumed = stale.coordinator.process(reconcile_at(700), true, false);
        CHECK(resumed.solveAttempted);
        CHECK(resumed.reason == MvpSuspendReason::None);
        CHECK(!resumed.activationPlacementUsed);
    }
    MvpFixture stale_retry(retry_settings);
    stale_retry.desktop.windows = retry_windows;
    CHECK(stale_retry.coordinator.process(foreground_event(201), true, false).backgroundRetryPending);
    stale_retry.desktop.staleSnapshots = true;
    for (std::uint64_t time = 600; time < 1600; time += 250) {
        const auto waiting = stale_retry.coordinator.process(reconcile_at(time), true, false);
        CHECK(waiting.reason == MvpSuspendReason::SnapshotStale);
        CHECK(waiting.backgroundRetryPending);
        CHECK(stale_retry.desktop.moveCalls == 1);
    }
    stale_retry.desktop.staleSnapshots = false;
    CHECK(stale_retry.coordinator.process(reconcile_at(1700), true, false).status == MvpBatchStatus::Applied);

    MvpFixture live;
    live.desktop.windows = dry.desktop.windows;
    const auto live_result = live.coordinator.process(drag_events(1), true, false);
    CHECK(live_result.status == MvpBatchStatus::Applied);
    CHECK(live_result.apply.status == stage_manager::window::MoveApplyStatus::Applied);
    CHECK(live.desktop.moveCalls == 1);
    CHECK(live_result.apply.appliedMoves.size() == 1);
    CHECK(live.internalMoves.find(2).has_value());
    const std::vector<WindowEvent> internal_location = {
        {WindowEventType::LocationChange, 2, 1, 110, 5},
    };
    const auto internal_result = live.coordinator.process(internal_location, true, false);
    CHECK(internal_result.status == MvpBatchStatus::Applied);
    CHECK(live.desktop.moveCalls == 1);
    CHECK(!live.internalMoves.find(2).has_value());

    MvpFixture drifted;
    drifted.desktop.windows = dry.desktop.windows;
    drifted.desktop.moveWindowAtCapture = 5;
    drifted.desktop.externallyMovedWindow = 3;
    const auto drifted_result = drifted.coordinator.process(drag_events(1), true, false);
    CHECK(drifted_result.status == MvpBatchStatus::ApiError);
    CHECK(drifted_result.reason == MvpSuspendReason::ApplyFailure);
    CHECK(drifted_result.apply.status ==
          stage_manager::window::MoveApplyStatus::VerificationFailed);
    CHECK(drifted_result.apply.requiresReconcile);

    MvpFixture crossed;
    crossed.desktop.windows = dry.desktop.windows;
    crossed.desktop.switchMonitorAtCapture = 2;
    crossed.desktop.switchMonitorWindow = 1;
    const auto crossed_result = crossed.coordinator.process(drag_events(1), true, true);
    CHECK(crossed_result.status == MvpBatchStatus::Suspended);
    CHECK(crossed_result.reason == MvpSuspendReason::ActiveMonitorChanged);
    CHECK(crossed.desktop.moveCalls == 0);

    MvpFixture maximized;
    maximized.desktop.windows = dry.desktop.windows;
    maximized.desktop.windows[0].zoomed = true;
    const auto maximized_result = maximized.coordinator.process(drag_events(1), true, true);
    CHECK(maximized_result.status == MvpBatchStatus::Suspended);
    CHECK(maximized_result.reason == MvpSuspendReason::ActiveWindowUnavailable);
    CHECK(maximized.desktop.moveCalls == 0);

    MvpFixture disabled;
    disabled.desktop.windows = dry.desktop.windows;
    const auto disabled_result = disabled.coordinator.process(drag_events(1), false, false);
    CHECK(disabled_result.status == MvpBatchStatus::Disabled);
    CHECK(disabled.desktop.captureCalls == 0);
    CHECK(disabled.desktop.moveCalls == 0);

    MvpFixture rebuilding;
    rebuilding.desktop.windows = dry.desktop.windows;
    const std::vector<WindowEvent> hook_error = {
        {WindowEventType::HookError, 0, 0, 100, 1},
    };
    const auto rebuilding_result = rebuilding.coordinator.process(hook_error, true, true);
    CHECK(rebuilding_result.status == MvpBatchStatus::Rebuilding);
    CHECK(rebuilding_result.reason == MvpSuspendReason::None);
    CHECK(rebuilding.desktop.captureCalls == 1);
    const auto rebuilt_idle = rebuilding.coordinator.process({}, true, true);
    CHECK(rebuilt_idle.status == MvpBatchStatus::Idle);
    CHECK(rebuilding.desktop.moveCalls == 0);

    FakeDesktop identity_desktop;
    identity_desktop.windows = {make_window(50, {10, 10, 210, 210}, 0)};
    stage_manager::window::WindowIdentityTracker identities;
    stage_manager::window::TrackingWindowProvider identity_provider(identity_desktop, identities);
    const auto first_identity = identity_provider.capture(
        stage_manager::window::SnapshotRefreshReason::Initial);
    const auto second_identity = identity_provider.capture(
        stage_manager::window::SnapshotRefreshReason::Event);
    CHECK(first_identity.windows[0].key.instanceGeneration != 0);
    CHECK(second_identity.windows[0].key == first_identity.windows[0].key);
    identity_provider.handle_event({WindowEventType::Destroy, 50, 0, 0, 0});
    const auto recreated = identity_provider.capture(
        stage_manager::window::SnapshotRefreshReason::Event);
    CHECK(recreated.windows[0].key.instanceGeneration !=
          first_identity.windows[0].key.instanceGeneration);

    MvpFixture recreated_during_flow;
    recreated_during_flow.desktop.windows = dry.desktop.windows;
    const std::vector<WindowEvent> destroyed = {
        {WindowEventType::Destroy, 2, 1, 100, 1},
    };
    static_cast<void>(recreated_during_flow.coordinator.process(destroyed, true, true));
    const auto identity_after_destroy = recreated_during_flow.provider.capture(
        stage_manager::window::SnapshotRefreshReason::Event);
    CHECK(identity_after_destroy.windows[1].key.instanceGeneration != 0);

    MvpFixture soak;
    for (std::uintptr_t index = 0; index < 20; ++index) {
        const auto column = static_cast<std::int32_t>(index % 5);
        const auto row = static_cast<std::int32_t>(index / 5);
        const auto left = 10 + column * 195;
        const auto top = 10 + row * 165;
        soak.desktop.windows.push_back(make_window(
            100 + index, {left, top, left + 180, top + 140},
            static_cast<std::int32_t>(index)));
    }
    constexpr std::uint64_t kSoakTransactions = 5'000;
    constexpr std::uint64_t kLocationsPerTransaction = 100;
    std::uint64_t sequence = 1;
    for (std::uint64_t transaction = 0; transaction < kSoakTransactions; ++transaction) {
        const auto active = static_cast<std::uintptr_t>(100 + transaction % 20);
        std::vector<WindowEvent> storm = {
            {WindowEventType::MoveSizeStart, active, 1, sequence, sequence},
        };
        ++sequence;
        storm.reserve(kLocationsPerTransaction + 2);
        for (std::uint64_t index = 0; index < kLocationsPerTransaction; ++index) {
            const auto hwnd = static_cast<std::uintptr_t>(100 + index % 20);
            storm.push_back({WindowEventType::LocationChange, hwnd, 1, sequence, sequence});
            ++sequence;
        }
        storm.push_back({WindowEventType::MoveSizeEnd, active, 1, sequence, sequence});
        ++sequence;
        const auto result = soak.coordinator.process(storm, true, true);
        CHECK(result.status == MvpBatchStatus::Idle);
        CHECK(result.solve.status == SolveStatus::NoViolation);
        CHECK(result.events.inputCount == kLocationsPerTransaction + 2);
        CHECK(result.events.coalescedLocationCount == 20);
        CHECK(result.managedWindowCount == 20);
        CHECK(result.transactionId == transaction + 1);
    }
    CHECK(soak.desktop.moveCalls == 0);
    CHECK(soak.desktop.captureCalls == kSoakTransactions * 2);
    return 0;
}

