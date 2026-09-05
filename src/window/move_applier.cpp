#include "window/move_applier.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <unordered_map>

namespace stage_manager::window {
namespace {

struct WindowInvariant {
    std::uint32_t style = 0;
    std::uint32_t exStyle = 0;
    NativeMonitorHandle monitor = 0;
    bool visible = false;
    bool topmost = false;
};

bool fits_pixel_coordinate(std::int64_t value) noexcept
{
    return value >= std::numeric_limits<std::int32_t>::min() &&
        value <= std::numeric_limits<std::int32_t>::max();
}

std::optional<PixelRect> to_pixel_rect(const geometry::Rect& rectangle) noexcept
{
    if (!fits_pixel_coordinate(rectangle.left) || !fits_pixel_coordinate(rectangle.top) ||
        !fits_pixel_coordinate(rectangle.right) || !fits_pixel_coordinate(rectangle.bottom)) {
        return std::nullopt;
    }
    return PixelRect{
        static_cast<std::int32_t>(rectangle.left),
        static_cast<std::int32_t>(rectangle.top),
        static_cast<std::int32_t>(rectangle.right),
        static_cast<std::int32_t>(rectangle.bottom),
    };
}

std::uint64_t coordinate_distance(std::int32_t left, std::int32_t right) noexcept
{
    return left >= right
        ? static_cast<std::uint64_t>(static_cast<std::int64_t>(left) - right)
        : static_cast<std::uint64_t>(static_cast<std::int64_t>(right) - left);
}

bool rectangles_match(const PixelRect& actual,
                      const geometry::Rect& expected,
                      std::uint32_t tolerance) noexcept
{
    const auto converted = to_pixel_rect(expected);
    if (!converted) {
        return false;
    }
    return coordinate_distance(actual.left, converted->left) <= tolerance &&
        coordinate_distance(actual.top, converted->top) <= tolerance &&
        coordinate_distance(actual.right, converted->right) <= tolerance &&
        coordinate_distance(actual.bottom, converted->bottom) <= tolerance;
}

const WindowSnapshot* find_window(const WindowSnapshotBatch& batch, const WindowKey& key)
{
    const auto iterator = std::find_if(batch.windows.begin(), batch.windows.end(),
                                       [&key](const auto& snapshot) {
                                           return snapshot.key == key;
                                       });
    return iterator == batch.windows.end() ? nullptr : &*iterator;
}

bool usable_snapshot(const WindowSnapshotBatch& batch) noexcept
{
    return batch.status == SnapshotStatus::Ok && batch.complete;
}

bool usable_window(const WindowSnapshot& snapshot) noexcept
{
    constexpr auto required_fields = field_bit(SnapshotField::Process) |
        field_bit(SnapshotField::PlacementRect) | field_bit(SnapshotField::Monitor) |
        field_bit(SnapshotField::Style);
    return (snapshot.queryFailures & required_fields) == 0 &&
        snapshot.placementRect.valid() && snapshot.monitor != 0;
}

WindowInvariant invariant_of(const WindowSnapshot& snapshot) noexcept
{
    return {snapshot.style,
            snapshot.exStyle,
            snapshot.monitor,
            snapshot.visible,
            snapshot.topmost};
}

bool preserves_invariant(const WindowSnapshot& snapshot,
                         const WindowInvariant& invariant) noexcept
{
    return snapshot.style == invariant.style && snapshot.exStyle == invariant.exStyle &&
        snapshot.monitor == invariant.monitor && snapshot.visible == invariant.visible &&
        snapshot.topmost == invariant.topmost;
}

bool same_rectangle(const PixelRect& left, const PixelRect& right) noexcept
{
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

bool valid_plan(std::span<const solver::MovePlan> plan)
{
    std::unordered_map<NativeWindowHandle, geometry::Rect> latest_positions;
    std::unordered_map<NativeWindowHandle, WindowKey> identities;
    for (const auto& move : plan) {
        if (move.window.hwnd == 0 || move.window.processId == 0 || move.from.empty() ||
            move.to.empty() || move.from.width() != move.to.width() ||
            move.from.height() != move.to.height() || !to_pixel_rect(move.to)) {
            return false;
        }
        const auto [identity, inserted] = identities.try_emplace(move.window.hwnd, move.window);
        if (!inserted && identity->second != move.window) {
            return false;
        }
        const auto previous = latest_positions.find(move.window.hwnd);
        if (previous != latest_positions.end() && previous->second != move.from) {
            return false;
        }
        latest_positions[move.window.hwnd] = move.to;
    }
    return true;
}

bool valid_reorders(std::span<const solver::ZOrderPlan> reorders)
{
    if (reorders.size() > 1) {
        return false;
    }
    for (const auto& reorder : reorders) {
        if (reorder.window.hwnd == 0 || reorder.window.processId == 0 ||
            reorder.insertAfter.hwnd == 0 || reorder.insertAfter.processId == 0 ||
            reorder.window.hwnd == reorder.insertAfter.hwnd ||
            reorder.fromZIndex < 0 || reorder.toZIndex < 0 ||
            reorder.fromZIndex <= reorder.toZIndex) {
            return false;
        }
    }
    return true;
}

} // namespace

VerifiedMoveApplier::VerifiedMoveApplier(IWindowMover& mover,
                                         IWindowProvider& provider,
                                         InternalMoveTracker& tracker,
                                         const IMoveApplyGuard* guard,
                                         MoveFailureTracker* failure_tracker)
    : mover_(mover),
      provider_(provider),
      tracker_(tracker),
      guard_(guard),
      failure_tracker_(failure_tracker)
{
}

MoveApplyResult VerifiedMoveApplier::apply(
    std::span<const solver::MovePlan> plan,
    std::span<const solver::ZOrderPlan> reorders,
    const MoveApplyOptions& options)
{
    MoveApplyResult result;
    if ((plan.empty() && reorders.empty()) || !valid_plan(plan) ||
        !valid_reorders(reorders) ||
        (!options.dryRun &&
         (options.transactionId == 0 || options.layoutGeneration == 0))) {
        return result;
    }
    if (options.dryRun) {
        result.status = MoveApplyStatus::DryRun;
        return result;
    }
    const auto cancelled = [this, &options] {
        return guard_ != nullptr &&
            !guard_->allows(options.transactionId, options.layoutGeneration);
    };
    const auto record_failure = [this, &options, &result](const WindowKey& window) {
        result.requiresReconcile = true;
        if (failure_tracker_ != nullptr) {
            const auto failure = failure_tracker_->record_failure(
                options.transactionId, window);
            result.windowFailureCount = failure.count;
            result.transactionNonCooperative = failure.nonCooperative;
        }
    };
    if (cancelled()) {
        result.status = MoveApplyStatus::Cancelled;
        result.requiresReconcile = true;
        return result;
    }

    auto snapshot = provider_.capture(SnapshotRefreshReason::Event);
    if (!usable_snapshot(snapshot)) {
        result.status = MoveApplyStatus::SnapshotFailed;
        result.requiresReconcile = true;
        result.finalSnapshot = std::move(snapshot);
        return result;
    }

    struct ReorderVerification {
        solver::ZOrderPlan plan;
        WindowInvariant targetInvariant;
        WindowInvariant referenceInvariant;
        PixelRect targetPlacement;
        PixelRect referencePlacement;
        std::int32_t referenceZIndex = -1;
    };
    std::vector<ReorderVerification> reorder_verifications;
    reorder_verifications.reserve(reorders.size());
    for (std::size_t index = 0; index < reorders.size(); ++index) {
        result.failedReorderIndex = index;
        const auto& reorder = reorders[index];
        if (cancelled()) {
            result.status = MoveApplyStatus::Cancelled;
            result.requiresReconcile = true;
            result.finalSnapshot = std::move(snapshot);
            return result;
        }
        const auto* before_target = find_window(snapshot, reorder.window);
        const auto* before_reference = find_window(snapshot, reorder.insertAfter);
        if (before_target == nullptr || before_reference == nullptr ||
            !usable_window(*before_target) || !usable_window(*before_reference) ||
            !before_target->zOrderKnown || !before_reference->zOrderKnown ||
            before_target->topmost || before_reference->topmost ||
            before_reference->zIndex >= before_target->zIndex ||
            before_target->zIndex != reorder.fromZIndex ||
            before_reference->zIndex + 1 != reorder.toZIndex) {
            result.status = MoveApplyStatus::WindowUnavailable;
            record_failure(reorder.window);
            result.finalSnapshot = std::move(snapshot);
            return result;
        }

        ReorderVerification verification{
            reorder,
            invariant_of(*before_target),
            invariant_of(*before_reference),
            before_target->placementRect,
            before_reference->placementRect,
            before_reference->zIndex,
        };
        const auto native_result = mover_.reorder(reorder.window, reorder.insertAfter);
        if (native_result.status != NativeReorderStatus::Reordered) {
            result.status = native_result.status == NativeReorderStatus::InvalidWindow
                ? MoveApplyStatus::WindowDestroyed
                : MoveApplyStatus::NativeReorderFailed;
            result.nativeReorderStatus = native_result.status;
            result.lastError = native_result.lastError;
            record_failure(reorder.window);
            result.finalSnapshot = std::move(snapshot);
            return result;
        }

        auto after = provider_.capture(SnapshotRefreshReason::Event);
        const auto* actual_target = usable_snapshot(after)
            ? find_window(after, reorder.window)
            : nullptr;
        const auto* actual_reference = usable_snapshot(after)
            ? find_window(after, reorder.insertAfter)
            : nullptr;
        if (actual_target == nullptr || actual_reference == nullptr ||
            !usable_window(*actual_target) || !usable_window(*actual_reference) ||
            !actual_target->zOrderKnown || !actual_reference->zOrderKnown ||
            actual_reference->zIndex != verification.referenceZIndex ||
            actual_target->zIndex != actual_reference->zIndex + 1 ||
            !same_rectangle(actual_target->placementRect, verification.targetPlacement) ||
            !same_rectangle(actual_reference->placementRect, verification.referencePlacement) ||
            !preserves_invariant(*actual_target, verification.targetInvariant) ||
            !preserves_invariant(*actual_reference, verification.referenceInvariant)) {
            result.status = usable_snapshot(after) ? MoveApplyStatus::ReorderRejected
                                                   : MoveApplyStatus::SnapshotFailed;
            record_failure(reorder.window);
            result.finalSnapshot = std::move(after);
            return result;
        }
        result.appliedReorders.push_back({reorder, actual_target->zIndex});
        reorder_verifications.push_back(std::move(verification));
        snapshot = std::move(after);
    }

    std::unordered_map<NativeWindowHandle, WindowInvariant> invariants;
    std::unordered_map<NativeWindowHandle, geometry::Rect> final_positions;
    for (std::size_t index = 0; index < plan.size(); ++index) {
        result.failedMoveIndex = index;
        const auto& move = plan[index];
        if (cancelled()) {
            result.status = MoveApplyStatus::Cancelled;
            result.requiresReconcile = true;
            result.finalSnapshot = std::move(snapshot);
            return result;
        }
        if (failure_tracker_ != nullptr && failure_tracker_->is_non_cooperative(
                options.transactionId, move.window)) {
            result.status = MoveApplyStatus::NonCooperative;
            result.windowFailureCount = failure_tracker_->failure_count(
                options.transactionId, move.window);
            result.transactionNonCooperative = true;
            result.requiresReconcile = true;
            result.finalSnapshot = std::move(snapshot);
            return result;
        }
        const auto* before = find_window(snapshot, move.window);
        if (before == nullptr || !usable_window(*before) ||
            !rectangles_match(before->placementRect, move.from, options.positionTolerance)) {
            result.status = MoveApplyStatus::WindowUnavailable;
            record_failure(move.window);
            result.finalSnapshot = std::move(snapshot);
            return result;
        }
        invariants.try_emplace(move.window.hwnd, invariant_of(*before));

        const auto expected = to_pixel_rect(move.to);
        if (!expected) {
            result.status = MoveApplyStatus::InvalidRequest;
            result.finalSnapshot = std::move(snapshot);
            return result;
        }
        const auto token = tracker_.begin(move.window.hwnd,
                                          options.transactionId,
                                          options.layoutGeneration,
                                          *expected);
        const auto native_result = mover_.move(move.window, move.to);
        if (native_result.status != NativeMoveStatus::Moved) {
            tracker_.cancel(move.window.hwnd);
            result.status = native_result.status == NativeMoveStatus::InvalidWindow
                ? MoveApplyStatus::WindowDestroyed
                : MoveApplyStatus::NativeMoveFailed;
            result.nativeStatus = native_result.status;
            result.lastError = native_result.lastError;
            record_failure(move.window);
            result.finalSnapshot = std::move(snapshot);
            return result;
        }

        auto after = provider_.capture(SnapshotRefreshReason::Event);
        if (!usable_snapshot(after)) {
            tracker_.cancel(move.window.hwnd);
            result.status = MoveApplyStatus::SnapshotFailed;
            record_failure(move.window);
            result.finalSnapshot = std::move(after);
            return result;
        }
        const auto* actual = find_window(after, move.window);
        const auto invariant = invariants.find(move.window.hwnd);
        if (actual == nullptr) {
            tracker_.cancel(move.window.hwnd);
            result.status = MoveApplyStatus::WindowDestroyed;
            record_failure(move.window);
            result.finalSnapshot = std::move(after);
            return result;
        }
        if (!usable_window(*actual) || invariant == invariants.end() ||
            !rectangles_match(actual->placementRect, move.to, options.positionTolerance) ||
            !preserves_invariant(*actual, invariant->second)) {
            tracker_.cancel(move.window.hwnd);
            result.status = MoveApplyStatus::MoveRejected;
            record_failure(move.window);
            result.finalSnapshot = std::move(after);
            return result;
        }
        result.appliedMoves.push_back({move, token, actual->placementRect});
        final_positions[move.window.hwnd] = move.to;
        snapshot = std::move(after);
    }

    auto final_snapshot = provider_.capture(SnapshotRefreshReason::Reconcile);
    if (!usable_snapshot(final_snapshot)) {
        result.status = MoveApplyStatus::SnapshotFailed;
        result.requiresReconcile = true;
        result.finalSnapshot = std::move(final_snapshot);
        return result;
    }
    const auto placement_matches_plan = [&final_positions, &options](
                                            const WindowSnapshot& actual,
                                            const PixelRect& original) {
        const auto moved = final_positions.find(actual.key.hwnd);
        return moved == final_positions.end()
            ? same_rectangle(actual.placementRect, original)
            : rectangles_match(actual.placementRect, moved->second, options.positionTolerance);
    };
    for (const auto& verification : reorder_verifications) {
        const auto* actual_target = find_window(final_snapshot, verification.plan.window);
        const auto* actual_reference = find_window(
            final_snapshot, verification.plan.insertAfter);
        if (actual_target == nullptr || actual_reference == nullptr ||
            !usable_window(*actual_target) || !usable_window(*actual_reference) ||
            !actual_target->zOrderKnown || !actual_reference->zOrderKnown ||
            actual_reference->zIndex != verification.referenceZIndex ||
            actual_target->zIndex != actual_reference->zIndex + 1 ||
            !placement_matches_plan(*actual_target, verification.targetPlacement) ||
            !placement_matches_plan(*actual_reference, verification.referencePlacement) ||
            !preserves_invariant(*actual_target, verification.targetInvariant) ||
            !preserves_invariant(*actual_reference, verification.referenceInvariant)) {
            result.status = MoveApplyStatus::VerificationFailed;
            record_failure(verification.plan.window);
            result.finalSnapshot = std::move(final_snapshot);
            return result;
        }
    }
    for (const auto& [hwnd, expected] : final_positions) {
        const auto plan_iterator = std::find_if(plan.begin(), plan.end(), [hwnd](const auto& move) {
            return move.window.hwnd == hwnd;
        });
        const auto invariant = invariants.find(hwnd);
        const auto* actual = plan_iterator == plan.end()
            ? nullptr
            : find_window(final_snapshot, plan_iterator->window);
        if (actual == nullptr || !usable_window(*actual) || invariant == invariants.end() ||
            !rectangles_match(actual->placementRect, expected, options.positionTolerance) ||
            !preserves_invariant(*actual, invariant->second)) {
            tracker_.cancel(hwnd);
            result.status = MoveApplyStatus::VerificationFailed;
            if (plan_iterator != plan.end()) {
                record_failure(plan_iterator->window);
            } else {
                result.requiresReconcile = true;
            }
            result.finalSnapshot = std::move(final_snapshot);
            return result;
        }
    }

    result.failedMoveIndex = plan.size();
    result.failedReorderIndex = reorders.size();
    result.status = MoveApplyStatus::Applied;
    result.finalSnapshot = std::move(final_snapshot);
    return result;
}

} // namespace stage_manager::window
