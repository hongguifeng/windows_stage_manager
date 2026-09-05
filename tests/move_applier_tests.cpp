#include "window/move_applier.h"

#include <algorithm>
#include <cstdio>
#include <functional>
#include <optional>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int move_transaction_tests();

namespace {

stage_manager::window::WindowSnapshot make_snapshot(
    stage_manager::window::WindowKey key, stage_manager::window::PixelRect rectangle)
{
    stage_manager::window::WindowSnapshot snapshot;
    snapshot.key = key;
    snapshot.rootHwnd = key.hwnd;
    snapshot.placementRect = rectangle;
    snapshot.visualRect = rectangle;
    snapshot.workArea = {0, 0, 1920, 1080};
    snapshot.monitor = 1;
    snapshot.style = 0x10;
    snapshot.exStyle = 0x20;
    snapshot.visible = true;
    snapshot.currentDesktop = true;
    snapshot.zOrderKnown = true;
    return snapshot;
}

stage_manager::solver::MovePlan make_move(stage_manager::window::WindowKey key,
                                           stage_manager::geometry::Rect from,
                                           stage_manager::geometry::Rect to)
{
    stage_manager::solver::MovePlan move;
    move.window = key;
    move.from = from;
    move.to = to;
    return move;
}

class FakeDesktop final : public stage_manager::window::IWindowMover,
                          public stage_manager::window::IWindowProvider {
public:
    stage_manager::window::NativeMoveResult move(
        const stage_manager::window::WindowKey& key,
        const stage_manager::geometry::Rect& destination) override
    {
        ++moveCalls;
        if (onMove) {
            onMove(moveCalls);
        }
        if (failMove) {
            return {stage_manager::window::NativeMoveStatus::ApiFailure, 1234};
        }
        auto* snapshot = find(key);
        if (snapshot == nullptr) {
            return {stage_manager::window::NativeMoveStatus::InvalidWindow, 1400};
        }
        if (destroyOnMove) {
            windows.erase(std::remove_if(windows.begin(), windows.end(), [&key](const auto& item) {
                              return item.key == key;
                          }),
                          windows.end());
            return {stage_manager::window::NativeMoveStatus::Moved, 0};
        }
        const auto offset = alterDestination ? 10 : 0;
        const auto width = snapshot->placementRect.right - snapshot->placementRect.left;
        const auto height = snapshot->placementRect.bottom - snapshot->placementRect.top;
        snapshot->placementRect = {
            static_cast<std::int32_t>(destination.left + offset),
            static_cast<std::int32_t>(destination.top),
            static_cast<std::int32_t>(destination.left + offset + width),
            static_cast<std::int32_t>(destination.top + height),
        };
        snapshot->visualRect = snapshot->placementRect;
        return {stage_manager::window::NativeMoveStatus::Moved, 0};
    }

    stage_manager::window::NativeReorderResult reorder(
        const stage_manager::window::WindowKey& key,
        const stage_manager::window::WindowKey& insert_after) override
    {
        ++reorderCalls;
        if (failReorder) {
            return {stage_manager::window::NativeReorderStatus::ApiFailure, 4321};
        }
        auto* target = find(key);
        auto* reference = find(insert_after);
        if (target == nullptr) {
            return {stage_manager::window::NativeReorderStatus::InvalidWindow, 1400};
        }
        if (reference == nullptr) {
            return {stage_manager::window::NativeReorderStatus::InvalidReference, 1400};
        }
        const auto target_z = target->zIndex;
        const auto reference_z = reference->zIndex;
        for (auto& window : windows) {
            if (window.zIndex > reference_z && window.zIndex < target_z) {
                ++window.zIndex;
            }
        }
        target->zIndex = alterReorder ? reference_z + 2 : reference_z + 1;
        return {stage_manager::window::NativeReorderStatus::Reordered, 0};
    }

    stage_manager::window::WindowSnapshotBatch capture(
        stage_manager::window::SnapshotRefreshReason reason) override
    {
        ++captureCalls;
        if (onCapture) {
            onCapture(captureCalls);
        }
        stage_manager::window::WindowSnapshotBatch batch;
        batch.version = static_cast<std::uint64_t>(captureCalls);
        batch.reason = reason;
        if (failCaptureAt && captureCalls == *failCaptureAt) {
            batch.status = stage_manager::window::SnapshotStatus::EnumerationFailed;
            return batch;
        }
        if (revertAtCapture && captureCalls == *revertAtCapture && !windows.empty()) {
            windows.front().placementRect = {100, 100, 300, 300};
            windows.front().visualRect = windows.front().placementRect;
        }
        batch.status = stage_manager::window::SnapshotStatus::Ok;
        batch.complete = true;
        batch.windows = windows;
        return batch;
    }

    stage_manager::window::WindowSnapshot* find(const stage_manager::window::WindowKey& key)
    {
        const auto iterator = std::find_if(windows.begin(), windows.end(), [&key](const auto& item) {
            return item.key == key;
        });
        return iterator == windows.end() ? nullptr : &*iterator;
    }

    std::vector<stage_manager::window::WindowSnapshot> windows;
    std::optional<int> failCaptureAt;
    std::optional<int> revertAtCapture;
    bool failMove = false;
    bool alterDestination = false;
    bool destroyOnMove = false;
    bool failReorder = false;
    bool alterReorder = false;
    std::function<void(int)> onMove;
    std::function<void(int)> onCapture;
    int captureCalls = 0;
    int moveCalls = 0;
    int reorderCalls = 0;
};

} // namespace

int main()
{
    CHECK(move_transaction_tests() == 0);
    using stage_manager::geometry::Rect;
    using stage_manager::window::InternalMoveTracker;
    using stage_manager::window::MoveApplyOptions;
    using stage_manager::window::MoveApplyStatus;
    using stage_manager::window::NativeMoveStatus;
    using stage_manager::window::NativeReorderStatus;
    using stage_manager::window::MoveFailureTracker;
    using stage_manager::window::MoveTransactionGuard;
    using stage_manager::window::VerifiedMoveApplier;
    using stage_manager::window::WindowEvent;
    using stage_manager::window::WindowEventType;
    using stage_manager::window::WindowKey;

    const WindowKey first{1, 101, 1001};
    const WindowKey second{2, 102, 1002};
    const std::vector plan = {
        make_move(first, {100, 100, 300, 300}, {140, 120, 340, 320}),
        make_move(second, {400, 100, 600, 300}, {440, 120, 640, 320}),
    };

    const WindowKey active{3, 103, 1003};
    stage_manager::solver::ZOrderPlan reorder;
    reorder.window = second;
    reorder.insertAfter = active;
    reorder.fromZIndex = 2;
    reorder.toZIndex = 1;

    FakeDesktop dry_reorder_desktop;
    dry_reorder_desktop.windows = {
        make_snapshot(active, {700, 100, 900, 300}),
        make_snapshot(first, {100, 100, 300, 300}),
        make_snapshot(second, {400, 100, 600, 300}),
    };
    for (std::size_t index = 0; index < dry_reorder_desktop.windows.size(); ++index) {
        dry_reorder_desktop.windows[index].zIndex = static_cast<std::int32_t>(index);
    }
    InternalMoveTracker dry_reorder_tracker;
    VerifiedMoveApplier dry_reorder_applier(
        dry_reorder_desktop, dry_reorder_desktop, dry_reorder_tracker);
    const std::vector reorder_plan = {reorder};
    const auto dry_reorder_result = dry_reorder_applier.apply(
        {}, reorder_plan, MoveApplyOptions{});
    CHECK(dry_reorder_result.status == MoveApplyStatus::DryRun);
    CHECK(dry_reorder_desktop.reorderCalls == 0);
    CHECK(dry_reorder_desktop.captureCalls == 0);

    FakeDesktop dry_desktop;
    dry_desktop.windows = {
        make_snapshot(first, {100, 100, 300, 300}),
        make_snapshot(second, {400, 100, 600, 300}),
    };
    InternalMoveTracker dry_tracker;
    VerifiedMoveApplier dry_applier(dry_desktop, dry_desktop, dry_tracker);
    const auto dry_result = dry_applier.apply(plan, MoveApplyOptions{});
    CHECK(dry_result.status == MoveApplyStatus::DryRun);
    CHECK(dry_desktop.moveCalls == 0);
    CHECK(dry_desktop.captureCalls == 0);
    CHECK(!dry_tracker.find(first.hwnd));

    FakeDesktop reorder_desktop;
    reorder_desktop.windows = dry_reorder_desktop.windows;
    InternalMoveTracker reorder_tracker;
    VerifiedMoveApplier reorder_applier(
        reorder_desktop, reorder_desktop, reorder_tracker);
    MoveApplyOptions reorder_options;
    reorder_options.dryRun = false;
    reorder_options.transactionId = 78;
    reorder_options.layoutGeneration = 10;
    const auto reordered = reorder_applier.apply({}, reorder_plan, reorder_options);
    CHECK(reordered.status == MoveApplyStatus::Applied);
    CHECK(reordered.appliedReorders.size() == 1);
    CHECK(reordered.appliedReorders[0].plan.window == second);
    CHECK(reordered.appliedReorders[0].actualZIndex == 1);
    CHECK(reorder_desktop.reorderCalls == 1);
    CHECK(reorder_desktop.moveCalls == 0);
    CHECK(reorder_desktop.captureCalls == 3);
    CHECK(reorder_desktop.find(active)->zIndex == 0);
    CHECK(reorder_desktop.find(second)->zIndex == 1);
    CHECK(reorder_desktop.find(first)->zIndex == 2);
    CHECK(reorder_desktop.find(active)->placementRect.left == 700);
    CHECK(reorder_desktop.find(second)->placementRect.left == 400);

    FakeDesktop combined_desktop;
    combined_desktop.windows = dry_reorder_desktop.windows;
    InternalMoveTracker combined_tracker;
    VerifiedMoveApplier combined_applier(
        combined_desktop, combined_desktop, combined_tracker);
    const std::vector combined_moves = {
        make_move(active, {700, 100, 900, 300}, {720, 120, 920, 320}),
    };
    const auto combined = combined_applier.apply(
        combined_moves, reorder_plan, reorder_options);
    CHECK(combined.status == MoveApplyStatus::Applied);
    CHECK(combined.appliedReorders.size() == 1);
    CHECK(combined.appliedMoves.size() == 1);
    CHECK(combined_desktop.find(active)->placementRect.left == 720);
    CHECK(combined_desktop.find(second)->zIndex == 1);

    FakeDesktop rejected_reorder_desktop;
    rejected_reorder_desktop.windows = dry_reorder_desktop.windows;
    rejected_reorder_desktop.alterReorder = true;
    InternalMoveTracker rejected_reorder_tracker;
    VerifiedMoveApplier rejected_reorder_applier(
        rejected_reorder_desktop, rejected_reorder_desktop, rejected_reorder_tracker);
    const auto rejected_reorder = rejected_reorder_applier.apply(
        {}, reorder_plan, reorder_options);
    CHECK(rejected_reorder.status == MoveApplyStatus::ReorderRejected);
    CHECK(rejected_reorder.requiresReconcile);
    CHECK(rejected_reorder.appliedReorders.empty());

    FakeDesktop failed_reorder_desktop;
    failed_reorder_desktop.windows = dry_reorder_desktop.windows;
    failed_reorder_desktop.failReorder = true;
    InternalMoveTracker failed_reorder_tracker;
    VerifiedMoveApplier failed_reorder_applier(
        failed_reorder_desktop, failed_reorder_desktop, failed_reorder_tracker);
    const auto failed_reorder = failed_reorder_applier.apply(
        {}, reorder_plan, reorder_options);
    CHECK(failed_reorder.status == MoveApplyStatus::NativeReorderFailed);
    CHECK(failed_reorder.nativeReorderStatus == NativeReorderStatus::ApiFailure);
    CHECK(failed_reorder.lastError == 4321);

    FakeDesktop reverted_reorder_desktop;
    reverted_reorder_desktop.windows = dry_reorder_desktop.windows;
    reverted_reorder_desktop.onCapture = [
        &reverted_reorder_desktop, first, second](int capture_count) {
        if (capture_count == 3) {
            reverted_reorder_desktop.find(first)->zIndex = 1;
            reverted_reorder_desktop.find(second)->zIndex = 2;
        }
    };
    InternalMoveTracker reverted_reorder_tracker;
    VerifiedMoveApplier reverted_reorder_applier(
        reverted_reorder_desktop, reverted_reorder_desktop, reverted_reorder_tracker);
    const auto reverted_reorder = reverted_reorder_applier.apply(
        {}, reorder_plan, reorder_options);
    CHECK(reverted_reorder.status == MoveApplyStatus::VerificationFailed);
    CHECK(reverted_reorder.appliedReorders.size() == 1);

    auto topmost_reorder_desktop = dry_reorder_desktop;
    topmost_reorder_desktop.windows[2].topmost = true;
    InternalMoveTracker topmost_reorder_tracker;
    VerifiedMoveApplier topmost_reorder_applier(
        topmost_reorder_desktop, topmost_reorder_desktop, topmost_reorder_tracker);
    CHECK(topmost_reorder_applier.apply({}, reorder_plan, reorder_options).status ==
          MoveApplyStatus::WindowUnavailable);

    FakeDesktop desktop;
    desktop.windows = dry_desktop.windows;
    InternalMoveTracker tracker;
    VerifiedMoveApplier applier(desktop, desktop, tracker);
    MoveApplyOptions apply_options;
    apply_options.dryRun = false;
    apply_options.transactionId = 77;
    apply_options.layoutGeneration = 9;
    apply_options.positionTolerance = 0;
    const auto applied = applier.apply(plan, apply_options);
    CHECK(applied.status == MoveApplyStatus::Applied);
    CHECK(applied.appliedMoves.size() == 2);
    CHECK(applied.failedMoveIndex == plan.size());
    CHECK(applied.finalSnapshot.complete);
    CHECK(applied.finalSnapshot.reason ==
          stage_manager::window::SnapshotRefreshReason::Reconcile);
    CHECK(desktop.moveCalls == 2);
    CHECK(desktop.captureCalls == 4);
    const auto first_token = tracker.find(first.hwnd);
    CHECK(first_token.has_value());
    CHECK(first_token->transactionId == apply_options.transactionId);
    CHECK(first_token->layoutGeneration == apply_options.layoutGeneration);
    CHECK(first_token->expectedPlacementRect.left == 140);
    WindowEvent internal_event;
    internal_event.type = WindowEventType::LocationChange;
    internal_event.hwnd = first.hwnd;
    CHECK(tracker.matches(internal_event));
    CHECK(tracker.complete(*first_token));
    CHECK(!tracker.matches(internal_event));

    FakeDesktop changed_desktop;
    changed_desktop.windows = dry_desktop.windows;
    changed_desktop.alterDestination = true;
    InternalMoveTracker changed_tracker;
    VerifiedMoveApplier changed_applier(changed_desktop, changed_desktop, changed_tracker);
    const auto changed = changed_applier.apply(
        std::span<const stage_manager::solver::MovePlan>(plan.data(), 1), apply_options);
    CHECK(changed.status == MoveApplyStatus::MoveRejected);
    CHECK(changed.appliedMoves.empty());
    CHECK(!changed_tracker.find(first.hwnd));

    FakeDesktop rejected_desktop;
    rejected_desktop.windows = dry_desktop.windows;
    rejected_desktop.alterDestination = true;
    InternalMoveTracker rejected_tracker;
    MoveFailureTracker failures;
    VerifiedMoveApplier rejected_applier(
        rejected_desktop, rejected_desktop, rejected_tracker, nullptr, &failures);
    const auto rejected = rejected_applier.apply(
        std::span<const stage_manager::solver::MovePlan>(plan.data(), 1), apply_options);
    CHECK(rejected.status == MoveApplyStatus::MoveRejected);
    CHECK(rejected.requiresReconcile);
    CHECK(rejected.windowFailureCount == 1);
    CHECK(rejected.transactionNonCooperative);
    CHECK(failures.failure_count(apply_options.transactionId, first) == 1);
    CHECK(failures.is_non_cooperative(apply_options.transactionId, first));

    FakeDesktop retry_desktop;
    retry_desktop.windows = dry_desktop.windows;
    InternalMoveTracker retry_tracker;
    VerifiedMoveApplier retry_applier(
        retry_desktop, retry_desktop, retry_tracker, nullptr, &failures);
    const auto retry = retry_applier.apply(
        std::span<const stage_manager::solver::MovePlan>(plan.data(), 1), apply_options);
    CHECK(retry.status == MoveApplyStatus::NonCooperative);
    CHECK(retry.transactionNonCooperative);
    CHECK(retry.windowFailureCount == 1);
    CHECK(retry_desktop.moveCalls == 0);

    FakeDesktop destroyed_desktop;
    destroyed_desktop.windows = dry_desktop.windows;
    destroyed_desktop.destroyOnMove = true;
    InternalMoveTracker destroyed_tracker;
    MoveFailureTracker destroyed_failures;
    VerifiedMoveApplier destroyed_applier(
        destroyed_desktop,
        destroyed_desktop,
        destroyed_tracker,
        nullptr,
        &destroyed_failures);
    const auto destroyed = destroyed_applier.apply(
        std::span<const stage_manager::solver::MovePlan>(plan.data(), 1), apply_options);
    CHECK(destroyed.status == MoveApplyStatus::WindowDestroyed);
    CHECK(destroyed.requiresReconcile);
    CHECK(destroyed.windowFailureCount == 1);
    CHECK(destroyed.appliedMoves.empty());

    FakeDesktop cancelled_desktop;
    cancelled_desktop.windows = dry_desktop.windows;
    InternalMoveTracker cancelled_tracker;
    MoveTransactionGuard guard;
    guard.activate(apply_options.transactionId, apply_options.layoutGeneration);
    cancelled_desktop.onMove = [&guard](int move_count) {
        if (move_count == 1) {
            guard.observe({WindowEventType::MoveSizeStart, 999, 0, 0, 0});
        }
    };
    VerifiedMoveApplier cancelled_applier(
        cancelled_desktop, cancelled_desktop, cancelled_tracker, &guard, nullptr);
    const auto cancelled = cancelled_applier.apply(plan, apply_options);
    CHECK(cancelled.status == MoveApplyStatus::Cancelled);
    CHECK(cancelled.requiresReconcile);
    CHECK(cancelled.appliedMoves.size() == 1);
    CHECK(cancelled_desktop.moveCalls == 1);

    FakeDesktop failed_desktop;
    failed_desktop.windows = dry_desktop.windows;
    failed_desktop.failMove = true;
    InternalMoveTracker failed_tracker;
    VerifiedMoveApplier failed_applier(failed_desktop, failed_desktop, failed_tracker);
    const auto failed = failed_applier.apply(
        std::span<const stage_manager::solver::MovePlan>(plan.data(), 1), apply_options);
    CHECK(failed.status == MoveApplyStatus::NativeMoveFailed);
    CHECK(failed.nativeStatus == NativeMoveStatus::ApiFailure);
    CHECK(failed.lastError == 1234);
    CHECK(!failed_tracker.find(first.hwnd));

    FakeDesktop stale_desktop;
    stale_desktop.windows = dry_desktop.windows;
    stale_desktop.failCaptureAt = 1;
    InternalMoveTracker stale_tracker;
    VerifiedMoveApplier stale_applier(stale_desktop, stale_desktop, stale_tracker);
    const auto stale = stale_applier.apply(
        std::span<const stage_manager::solver::MovePlan>(plan.data(), 1), apply_options);
    CHECK(stale.status == MoveApplyStatus::SnapshotFailed);
    CHECK(stale_desktop.moveCalls == 0);

    FakeDesktop reverted_desktop;
    reverted_desktop.windows = dry_desktop.windows;
    reverted_desktop.revertAtCapture = 3;
    InternalMoveTracker reverted_tracker;
    VerifiedMoveApplier reverted_applier(reverted_desktop, reverted_desktop, reverted_tracker);
    const auto reverted = reverted_applier.apply(
        std::span<const stage_manager::solver::MovePlan>(plan.data(), 1), apply_options);
    CHECK(reverted.status == MoveApplyStatus::VerificationFailed);
    CHECK(reverted.appliedMoves.size() == 1);
    CHECK(!reverted_tracker.find(first.hwnd));

    auto invalid_plan = plan;
    invalid_plan[0].to = Rect{140, 120, 341, 320};
    CHECK(applier.apply(invalid_plan, apply_options).status == MoveApplyStatus::InvalidRequest);

    auto reused_handle_plan = plan;
    reused_handle_plan.push_back(
        make_move(WindowKey{first.hwnd, first.processId, first.instanceGeneration + 1},
                  plan[0].to,
                  {180, 140, 380, 340}));
    CHECK(applier.apply(reused_handle_plan, apply_options).status ==
          MoveApplyStatus::InvalidRequest);
    return 0;
}
