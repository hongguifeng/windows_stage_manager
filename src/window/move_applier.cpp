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

} // namespace

VerifiedMoveApplier::VerifiedMoveApplier(IWindowMover& mover,
                                         IWindowProvider& provider,
                                         InternalMoveTracker& tracker)
    : mover_(mover), provider_(provider), tracker_(tracker)
{
}

MoveApplyResult VerifiedMoveApplier::apply(std::span<const solver::MovePlan> plan,
                                           const MoveApplyOptions& options)
{
    MoveApplyResult result;
    if (plan.empty() || !valid_plan(plan) ||
        (!options.dryRun &&
         (options.transactionId == 0 || options.layoutGeneration == 0))) {
        return result;
    }
    if (options.dryRun) {
        result.status = MoveApplyStatus::DryRun;
        return result;
    }

    auto snapshot = provider_.capture(SnapshotRefreshReason::Event);
    if (!usable_snapshot(snapshot)) {
        result.status = MoveApplyStatus::SnapshotFailed;
        result.finalSnapshot = std::move(snapshot);
        return result;
    }

    std::unordered_map<NativeWindowHandle, WindowInvariant> invariants;
    std::unordered_map<NativeWindowHandle, geometry::Rect> final_positions;
    for (std::size_t index = 0; index < plan.size(); ++index) {
        result.failedMoveIndex = index;
        const auto& move = plan[index];
        const auto* before = find_window(snapshot, move.window);
        if (before == nullptr || !usable_window(*before) ||
            !rectangles_match(before->placementRect, move.from, options.positionTolerance)) {
            result.status = MoveApplyStatus::WindowUnavailable;
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
            result.status = MoveApplyStatus::NativeMoveFailed;
            result.nativeStatus = native_result.status;
            result.lastError = native_result.lastError;
            result.finalSnapshot = std::move(snapshot);
            return result;
        }

        auto after = provider_.capture(SnapshotRefreshReason::Event);
        const auto* actual = find_window(after, move.window);
        const auto invariant = invariants.find(move.window.hwnd);
        if (!usable_snapshot(after) || actual == nullptr || !usable_window(*actual) ||
            invariant == invariants.end() ||
            !rectangles_match(actual->placementRect, move.to, options.positionTolerance) ||
            !preserves_invariant(*actual, invariant->second)) {
            tracker_.cancel(move.window.hwnd);
            result.status = MoveApplyStatus::VerificationFailed;
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
        result.finalSnapshot = std::move(final_snapshot);
        return result;
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
            result.finalSnapshot = std::move(final_snapshot);
            return result;
        }
    }

    result.failedMoveIndex = plan.size();
    result.status = MoveApplyStatus::Applied;
    result.finalSnapshot = std::move(final_snapshot);
    return result;
}

} // namespace stage_manager::window
