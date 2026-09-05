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
        stage_manager::window::WindowSnapshotBatch result;
        result.version = static_cast<std::uint64_t>(captureCalls);
        result.reason = reason;
        result.status = stage_manager::window::SnapshotStatus::Ok;
        result.complete = true;
        result.windows = windows;
        return result;
    }

    stage_manager::window::NativeMoveResult move(
        const stage_manager::window::WindowKey& key,
        const stage_manager::geometry::Rect& destination) override
    {
        ++moveCalls;
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
    std::optional<int> switchMonitorAtCapture;
    std::optional<int> moveWindowAtCapture;
    std::uintptr_t switchMonitorWindow = 0;
    std::uintptr_t externallyMovedWindow = 0;
    int captureCalls = 0;
    int moveCalls = 0;
    int reorderCalls = 0;
};

stage_manager::app::Settings test_settings()
{
    stage_manager::app::Settings settings;
    settings.maxSolveTimeMs = 1000;
    return settings;
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

std::vector<stage_manager::window::WindowEvent> activation_drag_events(
    std::uintptr_t active)
{
    using stage_manager::window::WindowEventType;
    return {
        {WindowEventType::Foreground, active, 1, 100, 1},
        {WindowEventType::MoveSizeStart, active, 1, 101, 2},
        {WindowEventType::LocationChange, active, 1, 102, 3},
        {WindowEventType::MoveSizeEnd, active, 1, 103, 4},
    };
}

} // namespace

int main()
{
    using stage_manager::solver::SolveStatus;
    using stage_manager::window::MvpBatchStatus;
    using stage_manager::window::MvpSuspendReason;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;

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
    CHECK(dry_result.solve.moves.size() == 1);
    CHECK(dry_result.solve.moves[0].window.hwnd == 2);
    CHECK(dry_result.solve.moves[0].to.left != dry_result.solve.moves[0].from.left);
    CHECK(dry_result.solve.moves[0].to.top != dry_result.solve.moves[0].from.top);
    CHECK(dry_result.apply.status == stage_manager::window::MoveApplyStatus::DryRun);
    CHECK(dry.desktop.moveCalls == 0);
    CHECK(dry_result.events.inputCount == 4);
    CHECK(dry_result.events.coalescedLocationCount == 1);
    CHECK(dry_result.managedWindowCount == 3);
    CHECK(dry_result.movedWindowCount == 1);
    CHECK(!dry_result.zOrderFallbackUsed);
    CHECK(dry_result.reorderedWindowCount == 0);
    CHECK(dry.desktop.reorderCalls == 0);
    const std::vector<WindowEvent> duplicate_end = {
        {WindowEventType::MoveSizeEnd, 1, 1, 104, 5},
    };
    const auto duplicate_result = dry.coordinator.process(duplicate_end, true, true);
    CHECK(duplicate_result.status == MvpBatchStatus::Unsatisfiable);
    CHECK(duplicate_result.solve.status == SolveStatus::Unsatisfiable);
    CHECK(duplicate_result.solve.moves.empty());

    MvpFixture chain;
    chain.desktop.windows = {
        make_window(10, {192, 100, 392, 300}, 0),
        make_window(11, {192, 100, 392, 300}, 1),
        make_window(12, {128, 100, 328, 300}, 2),
        make_window(13, {64, 100, 264, 300}, 3),
    };
    const auto chain_result = chain.coordinator.process(drag_events(10), true, true);
    CHECK(chain_result.status == MvpBatchStatus::DryRun);
    CHECK(chain_result.solve.status == SolveStatus::Solved);
    CHECK(chain_result.managedWindowCount == 4);
    CHECK(chain_result.movedWindowCount >= 1);
    CHECK(chain_result.solve.moves.size() >= 1);
    const auto chain_visibility = stage_manager::solver::scan_visibility_violations(
        chain_result.solve.finalSnapshot,
        stage_manager::solver::VisibilityRequirements{48, 24, 128, 2});
    CHECK(chain_visibility.status ==
          stage_manager::solver::ViolationScanStatus::Ok);
    CHECK(chain_visibility.violations.empty());

    auto stacked_settings = test_settings();
    stacked_settings.maxSolverStates = 4;
    MvpFixture stacked(stacked_settings);
    stacked.desktop.windows = {
        make_window(40, {100, 100, 400, 400}, 0),
        make_window(41, {100, 100, 400, 400}, 1),
        make_window(42, {100, 100, 400, 400}, 2),
        make_window(43, {100, 100, 400, 400}, 3),
    };
    const auto stacked_result =
        stacked.coordinator.process(drag_events(40), true, true);
    CHECK(stacked_result.status == MvpBatchStatus::DryRun);
    CHECK(stacked_result.fallbackUsed);
    CHECK(stacked_result.managedWindowCount == 4);
    CHECK(stacked_result.solve.status == SolveStatus::Solved);
    CHECK(stacked_result.solve.moves.size() == 3);
    const auto stacked_visibility = stage_manager::solver::scan_visibility_violations(
        stacked_result.solve.finalSnapshot,
        stage_manager::solver::VisibilityRequirements{48, 24, 128, 2});
    CHECK(stacked_visibility.status ==
          stage_manager::solver::ViolationScanStatus::Ok);
    CHECK(stacked_visibility.violations.empty());

    MvpFixture two_edge_goal;
    two_edge_goal.desktop.windows = {
        make_window(14, {130, 50, 430, 450}, 0),
        make_window(15, {100, 100, 400, 400}, 1),
    };
    const auto two_edge_result =
        two_edge_goal.coordinator.process(drag_events(14), true, true);
    CHECK(two_edge_result.status == MvpBatchStatus::DryRun);
    CHECK(!two_edge_result.edgeGoalDegraded);
    CHECK(two_edge_result.requiredExposedEdges == 2);
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
    CHECK(one_edge_result.status == MvpBatchStatus::Idle);
    CHECK(one_edge_result.edgeGoalDegraded);
    CHECK(one_edge_result.requiredExposedEdges == 1);
    CHECK(one_edge_result.solve.status == SolveStatus::NoViolation);
    CHECK(one_edge_result.solve.moves.empty());

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

    MvpFixture z_order_dry(z_order_settings);
    z_order_dry.desktop.windows = make_z_order_layout();
    const auto z_order_dry_result =
        z_order_dry.coordinator.process(drag_events(201), true, true);
    CHECK(z_order_dry_result.status == MvpBatchStatus::DryRun);
    CHECK(z_order_dry_result.zOrderFallbackUsed);
    CHECK(!z_order_dry_result.edgeGoalDegraded);
    CHECK(z_order_dry_result.requiredExposedEdges == 2);
    CHECK(z_order_dry_result.reorderedWindowCount == 1);
    CHECK(z_order_dry_result.zOrderSolve.candidatesTried == 1);
    CHECK(z_order_dry_result.zOrderSolve.reorders[0].window.hwnd == 203);
    CHECK(z_order_dry_result.zOrderSolve.reorders[0].insertAfter.hwnd == 204);
    CHECK(z_order_dry_result.zOrderSolve.reorders[0].toZIndex == 2);
    CHECK(z_order_dry_result.solve.status == SolveStatus::NoViolation);
    CHECK(z_order_dry_result.solve.moves.empty());
    CHECK(z_order_dry_result.apply.status ==
          stage_manager::window::MoveApplyStatus::DryRun);
    CHECK(z_order_dry.desktop.reorderCalls == 0);
    CHECK(z_order_dry.desktop.moveCalls == 0);

    MvpFixture activation_z_order(z_order_settings);
    activation_z_order.desktop.windows = make_z_order_layout();
    activation_z_order.desktop.windows[0].placementRect = {0, 300, 900, 400};
    activation_z_order.desktop.windows[0].visualRect = {0, 300, 900, 400};
    const auto activation_z_order_result =
        activation_z_order.coordinator.process(foreground_event(201), true, true);
    CHECK(activation_z_order_result.status == MvpBatchStatus::DryRun);
    CHECK(activation_z_order_result.zOrderFallbackUsed);
    CHECK(activation_z_order_result.activationCenteringUsed);
    CHECK(activation_z_order_result.reorderedWindowCount == 1);
    CHECK(activation_z_order_result.solve.status == SolveStatus::Solved);
    CHECK(activation_z_order_result.solve.moves.size() == 1);
    CHECK(activation_z_order_result.solve.moves[0].window.hwnd == 201);
    CHECK((activation_z_order_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{50, 300, 950, 400}));
    CHECK(activation_z_order.desktop.reorderCalls == 0);
    CHECK(activation_z_order.desktop.moveCalls == 0);

    MvpFixture z_order_live(z_order_settings);
    z_order_live.desktop.windows = make_z_order_layout();
    const auto z_order_live_result =
        z_order_live.coordinator.process(drag_events(201), true, false);
    CHECK(z_order_live_result.status == MvpBatchStatus::Applied);
    CHECK(z_order_live_result.zOrderFallbackUsed);
    CHECK(z_order_live_result.apply.status ==
          stage_manager::window::MoveApplyStatus::Applied);
    CHECK(z_order_live_result.apply.appliedReorders.size() == 1);
    CHECK(z_order_live_result.apply.appliedMoves.empty());
    CHECK(z_order_live.desktop.reorderCalls == 1);
    CHECK(z_order_live.desktop.moveCalls == 0);
    CHECK(z_order_live.desktop.windows[0].zIndex == 0);
    CHECK(z_order_live.desktop.windows[1].zIndex == 1);
    CHECK(z_order_live.desktop.windows[3].zIndex == 2);
    CHECK(z_order_live.desktop.windows[2].zIndex == 3);

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
    CHECK(crowded_result.zOrderFallbackUsed);
    CHECK(crowded_result.managedWindowCount == 3);
    CHECK(crowded_result.solve.status == SolveStatus::Solved);
    CHECK(crowded_result.solve.moves.size() == 1);
    CHECK(crowded_result.zOrderSolve.reorders.size() == 1);

    MvpFixture crowded_live;
    crowded_live.desktop.windows = crowded.desktop.windows;
    const auto crowded_live_result =
        crowded_live.coordinator.process(drag_events(20), true, false);
    CHECK(crowded_live_result.status == MvpBatchStatus::Applied);
    CHECK(!crowded_live_result.fallbackUsed);
    CHECK(crowded_live_result.zOrderFallbackUsed);
    CHECK(crowded_live_result.apply.appliedMoves.size() == 1);
    CHECK(crowded_live_result.apply.appliedReorders.size() == 1);
    CHECK(crowded_live.desktop.moveCalls == 1);
    CHECK(crowded_live.desktop.reorderCalls == 1);

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
    CHECK(coverage_result.solve.moves[0].window.hwnd == 142);

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
    CHECK(activated_result.solve.moves.size() == 1);
    CHECK(activated_result.solve.moves[0].window.hwnd == 150);
    CHECK((activated_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{350, 200, 650, 500}));
    CHECK(activated_result.activationCenteringUsed);
    CHECK(activated_result.apply.status == stage_manager::window::MoveApplyStatus::DryRun);
    CHECK(activated.desktop.moveCalls == 0);

    auto centering_disabled_settings = test_settings();
    centering_disabled_settings.centerActivatedWindow = false;
    MvpFixture centering_disabled(centering_disabled_settings);
    centering_disabled.desktop.windows = {
        make_window(152, {100, 100, 400, 400}, 0),
        make_window(153, {100, 100, 400, 400}, 1),
    };
    const auto centering_disabled_result =
        centering_disabled.coordinator.process(foreground_event(152), true, true);
    CHECK(centering_disabled_result.status == MvpBatchStatus::DryRun);
    CHECK(!centering_disabled_result.activationCenteringUsed);
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
    CHECK(centered_result.activationCenteringUsed);
    CHECK(centered_result.solve.moves.size() == 1);
    CHECK(centered_result.solve.moves[0].window.hwnd == 160);
    CHECK((centered_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{400, 250, 600, 450}));
    CHECK(activation_centering.desktop.moveCalls == 1);

    const auto duplicate_foreground =
        activation_centering.coordinator.process(foreground_event(160), true, false);
    CHECK(duplicate_foreground.transactionId == centered_result.transactionId);
    CHECK(duplicate_foreground.solve.moves.empty());
    CHECK(!duplicate_foreground.activationCenteringUsed);
    CHECK(activation_centering.desktop.moveCalls == 1);

    activation_centering.desktop.windows[0].placementRect = {50, 50, 250, 250};
    activation_centering.desktop.windows[0].visualRect = {50, 50, 250, 250};
    const auto manually_moved =
        activation_centering.coordinator.process(drag_events(160), true, false);
    CHECK(manually_moved.status == MvpBatchStatus::Idle);
    CHECK(!manually_moved.activationCenteringUsed);
    CHECK(activation_centering.desktop.moveCalls == 1);
    CHECK(activation_centering.desktop.windows[0].placementRect.left == 50);
    CHECK(activation_centering.desktop.windows[0].placementRect.top == 50);
    CHECK(activation_centering.desktop.windows[0].placementRect.right == 250);
    CHECK(activation_centering.desktop.windows[0].placementRect.bottom == 250);

    MvpFixture activated_by_drag;
    activated_by_drag.desktop.windows = {
        make_window(164, {50, 50, 250, 250}, 0),
    };
    const auto activated_by_drag_result = activated_by_drag.coordinator.process(
        activation_drag_events(164), true, false);
    CHECK(activated_by_drag_result.status == MvpBatchStatus::Idle);
    CHECK(!activated_by_drag_result.activationCenteringUsed);
    CHECK(activated_by_drag_result.solve.moves.empty());
    CHECK(activated_by_drag.desktop.moveCalls == 0);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.left == 50);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.top == 50);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.right == 250);
    CHECK(activated_by_drag.desktop.windows[0].placementRect.bottom == 250);

    MvpFixture current_monitor_center;
    auto offset_frame = make_window(165, {1090, 90, 1310, 310}, 0);
    offset_frame.visualRect = {1100, 100, 1300, 300};
    offset_frame.workArea = {1000, 0, 2000, 700};
    offset_frame.monitor = 2;
    current_monitor_center.desktop.windows = {offset_frame};
    const auto current_monitor_result = current_monitor_center.coordinator.process(
        foreground_event(165), true, true);
    CHECK(current_monitor_result.status == MvpBatchStatus::DryRun);
    CHECK(current_monitor_result.activationCenteringUsed);
    CHECK(current_monitor_result.solve.moves.size() == 1);
    CHECK((current_monitor_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{1390, 240, 1610, 460}));
    CHECK(current_monitor_center.desktop.moveCalls == 0);

    MvpFixture already_centered;
    already_centered.desktop.windows = {
        make_window(161, {400, 250, 600, 450}, 0),
    };
    const auto already_centered_result =
        already_centered.coordinator.process(foreground_event(161), true, false);
    CHECK(already_centered_result.status == MvpBatchStatus::Idle);
    CHECK(already_centered_result.solve.status == SolveStatus::NoViolation);
    CHECK(already_centered_result.solve.moves.empty());
    CHECK(!already_centered_result.activationCenteringUsed);
    CHECK(already_centered.desktop.moveCalls == 0);

    MvpFixture center_and_repair;
    center_and_repair.desktop.windows = {
        make_window(162, {0, 0, 300, 300}, 0),
        make_window(163, {350, 200, 650, 500}, 1),
    };
    const auto center_and_repair_result =
        center_and_repair.coordinator.process(foreground_event(162), true, true);
    CHECK(center_and_repair_result.status == MvpBatchStatus::DryRun);
    CHECK(center_and_repair_result.solve.status == SolveStatus::Solved);
    CHECK(center_and_repair_result.activationCenteringUsed);
    CHECK(center_and_repair_result.solve.moves.size() == 2);
    CHECK(center_and_repair_result.solve.moves[0].window.hwnd == 162);
    CHECK((center_and_repair_result.solve.moves[0].to ==
           stage_manager::geometry::Rect{350, 200, 650, 500}));
    CHECK(center_and_repair_result.solve.moves[1].window.hwnd == 163);
    CHECK(center_and_repair_result.movedWindowCount == 2);
    CHECK(center_and_repair.desktop.moveCalls == 0);

    auto one_move_settings = test_settings();
    one_move_settings.maxMovesPerBatch = 1;
    MvpFixture center_budget(one_move_settings);
    center_budget.desktop.windows = center_and_repair.desktop.windows;
    const auto center_budget_result =
        center_budget.coordinator.process(foreground_event(162), true, true);
    CHECK(center_budget_result.status == MvpBatchStatus::Unsatisfiable);
    CHECK(!center_budget_result.activationCenteringUsed);
    CHECK(center_budget_result.solve.moves.size() <= 1);
    CHECK(center_budget.desktop.moveCalls == 0);

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
