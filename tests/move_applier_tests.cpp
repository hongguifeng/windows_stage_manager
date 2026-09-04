#include "window/move_applier.h"

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
        if (failMove) {
            return {stage_manager::window::NativeMoveStatus::ApiFailure, 1234};
        }
        auto* snapshot = find(key);
        if (snapshot == nullptr) {
            return {stage_manager::window::NativeMoveStatus::InvalidWindow, 1400};
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

    stage_manager::window::WindowSnapshotBatch capture(
        stage_manager::window::SnapshotRefreshReason reason) override
    {
        ++captureCalls;
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
    int captureCalls = 0;
    int moveCalls = 0;
};

} // namespace

int main()
{
    using stage_manager::geometry::Rect;
    using stage_manager::window::InternalMoveTracker;
    using stage_manager::window::MoveApplyOptions;
    using stage_manager::window::MoveApplyStatus;
    using stage_manager::window::NativeMoveStatus;
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
    CHECK(changed.status == MoveApplyStatus::VerificationFailed);
    CHECK(changed.appliedMoves.empty());
    CHECK(!changed_tracker.find(first.hwnd));

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
