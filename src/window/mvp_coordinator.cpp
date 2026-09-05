#include "window/mvp_coordinator.h"

#include "geometry/dpi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <unordered_set>
#include <vector>

namespace stage_manager::window {
namespace {

geometry::Rect to_rect(const PixelRect& rectangle) noexcept
{
    return {rectangle.left, rectangle.top, rectangle.right, rectangle.bottom};
}

bool valid_for_layout(const WindowSnapshot& window) noexcept
{
    return window.key.hwnd != 0 && window.key.processId != 0 &&
        window.key.instanceGeneration != 0 && window.zOrderKnown &&
        window.placementRect.valid() && window.visualRect.valid() &&
        window.workArea.valid();
}

bool rectangles_intersect(const PixelRect& left, const PixelRect& right) noexcept
{
    return left.left < right.right && left.right > right.left &&
        left.top < right.bottom && left.bottom > right.top;
}

bool directly_obscured_by(const WindowSnapshot& blocker,
                          const WindowSnapshot& target) noexcept
{
    return blocker.zOrderKnown && target.zOrderKnown &&
        blocker.zIndex < target.zIndex &&
        rectangles_intersect(blocker.visualRect, target.visualRect);
}

long double obscured_fraction(const PixelRect& blocker, const PixelRect& target) noexcept
{
    if (!rectangles_intersect(blocker, target)) {
        return 0.0L;
    }
    const auto intersection_width = static_cast<std::int64_t>(
        std::min(blocker.right, target.right)) -
        static_cast<std::int64_t>(std::max(blocker.left, target.left));
    const auto intersection_height = static_cast<std::int64_t>(
        std::min(blocker.bottom, target.bottom)) -
        static_cast<std::int64_t>(std::max(blocker.top, target.top));
    const auto target_width = static_cast<std::int64_t>(target.right) - target.left;
    const auto target_height = static_cast<std::int64_t>(target.bottom) - target.top;
    if (target_width <= 0 || target_height <= 0) {
        return 0.0L;
    }
    return (static_cast<long double>(intersection_width) *
            static_cast<long double>(intersection_height)) /
        (static_cast<long double>(target_width) *
         static_cast<long double>(target_height));
}

bool contains_hash(std::span<const solver::LayoutHash> hashes,
                   const solver::LayoutHash& value)
{
    return std::find(hashes.begin(), hashes.end(), value) != hashes.end();
}

std::vector<std::size_t> directly_obscured_targets(
    const solver::LayoutSnapshot& layout,
    std::span<const solver::Violation> violations,
    std::size_t active_index)
{
    std::vector<std::size_t> targets;
    for (const auto& violation : violations) {
        if (violation.targetIndex >= layout.windows.size() ||
            violation.targetIndex == active_index) {
            continue;
        }
        const bool blocked_by_active = std::find(
            violation.blockerIndices.begin(), violation.blockerIndices.end(), active_index) !=
            violation.blockerIndices.end();
        if (blocked_by_active) {
            targets.push_back(violation.targetIndex);
        }
    }
    std::stable_sort(targets.begin(), targets.end(), [&layout](auto left, auto right) {
        return layout.windows[left].zIndex < layout.windows[right].zIndex;
    });
    return targets;
}

std::optional<geometry::Edge> move_direction(const solver::MovePlan& move) noexcept
{
    const auto delta_x = move.to.left - move.from.left;
    const auto delta_y = move.to.top - move.from.top;
    if (delta_x == 0 && delta_y == 0) {
        return std::nullopt;
    }
    const auto abs_x = delta_x < 0 ? -delta_x : delta_x;
    const auto abs_y = delta_y < 0 ? -delta_y : delta_y;
    if (abs_x >= abs_y) {
        return delta_x < 0 ? geometry::Edge::Left : geometry::Edge::Right;
    }
    return delta_y < 0 ? geometry::Edge::Top : geometry::Edge::Bottom;
}

} // namespace

std::string_view suspend_reason_name(MvpSuspendReason reason) noexcept
{
    switch (reason) {
    case MvpSuspendReason::None:
        return "none";
    case MvpSuspendReason::SnapshotUnavailable:
        return "snapshot_unavailable";
    case MvpSuspendReason::ActiveWindowUnavailable:
        return "active_window_unavailable";
    case MvpSuspendReason::ActiveMonitorChanged:
        return "active_monitor_changed";
    case MvpSuspendReason::NoManagedPeer:
        return "no_managed_peer";
    case MvpSuspendReason::SolverFailure:
        return "solver_failure";
    case MvpSuspendReason::ApplyFailure:
        return "apply_failure";
    }
    return "unknown";
}

MvpCoordinator::MvpCoordinator(IWindowProvider& provider,
                               IMoveApplier& applier,
                               MoveTransactionGuard& guard,
                               InternalMoveTracker& internal_moves,
                               ConservativeWindowClassifier classifier,
                               app::Settings settings)
    : provider_(provider),
      applier_(applier),
      guard_(guard),
      internal_moves_(internal_moves),
      classifier_(std::move(classifier)),
      settings_(std::move(settings))
{
}

std::optional<WindowSnapshotBatch> MvpCoordinator::capture(SnapshotRefreshReason reason)
{
    auto snapshot = provider_.capture(reason);
    if (snapshot.status != SnapshotStatus::Ok || !snapshot.complete) {
        return std::nullopt;
    }
    classifier_.annotate(snapshot);
    return snapshot;
}

MvpBatchResult MvpCoordinator::process(std::span<const WindowEvent> events,
                                       bool enabled,
                                       bool dry_run)
{
    MvpBatchResult result;
    result.events = coalescer_.coalesce(events);
    if (!enabled) {
        guard_.cancel();
        status_ = MvpBatchStatus::Disabled;
        result.status = status_;
        return result;
    }

    bool should_settle = false;
    std::optional<NativeWindowHandle> foreground_window;
    for (const auto& event : result.events.events) {
        provider_.handle_event(event);
        if (event.type == WindowEventType::MoveSizeStart) {
            guard_.observe(event);
            active_window_ = event.hwnd;
            ++next_transaction_id_;
            ++layout_generation_;
            transaction_seen_states_.clear();
            preferred_edge_.reset();
            const auto initial = capture(SnapshotRefreshReason::Event);
            if (!initial) {
                status_ = MvpBatchStatus::Rebuilding;
                result.status = status_;
                result.reason = MvpSuspendReason::SnapshotUnavailable;
                return result;
            }
            const auto active = std::find_if(initial->windows.begin(), initial->windows.end(),
                                             [this](const auto& window) {
                                                 return window.key.hwnd == active_window_;
                                             });
            starting_monitor_ = active == initial->windows.end() ? 0 : active->monitor;
            guard_.activate(next_transaction_id_, layout_generation_);
            status_ = MvpBatchStatus::Dragging;
        } else if (event.type == WindowEventType::Foreground) {
            foreground_window = event.hwnd;
        } else if (event.type == WindowEventType::MoveSizeEnd &&
                   event.hwnd == active_window_) {
            should_settle = true;
        } else if (event.type == WindowEventType::LocationChange) {
            const auto token = internal_moves_.find(event.hwnd);
            if (token) {
                internal_moves_.complete(*token);
            }
        } else if (event.type == WindowEventType::HookError) {
            status_ = MvpBatchStatus::Rebuilding;
        }
    }

    if (should_settle) {
        return settle(dry_run, std::move(result.events));
    }
    if (foreground_window && status_ != MvpBatchStatus::Dragging) {
        active_window_ = *foreground_window;
        ++next_transaction_id_;
        ++layout_generation_;
        transaction_seen_states_.clear();
        preferred_edge_.reset();
        const auto initial = capture(SnapshotRefreshReason::Event);
        if (!initial) {
            status_ = MvpBatchStatus::Rebuilding;
            result.status = status_;
            result.reason = MvpSuspendReason::SnapshotUnavailable;
            return result;
        }
        const auto active = std::find_if(initial->windows.begin(), initial->windows.end(),
                                         [this](const auto& window) {
                                             return window.key.hwnd == active_window_;
                                         });
        starting_monitor_ = active == initial->windows.end() ? 0 : active->monitor;
        guard_.activate(next_transaction_id_, layout_generation_);
        return settle(dry_run, std::move(result.events));
    }
    if (result.events.requiresFullReconcile) {
        const auto rebuilt = capture(SnapshotRefreshReason::Reconcile);
        result.status = MvpBatchStatus::Rebuilding;
        result.reason = rebuilt ? MvpSuspendReason::None
                                : MvpSuspendReason::SnapshotUnavailable;
        status_ = rebuilt ? MvpBatchStatus::Idle : MvpBatchStatus::Rebuilding;
        return result;
    }
    result.status = status_;
    result.transactionId = next_transaction_id_;
    result.layoutGeneration = layout_generation_;
    return result;
}

MvpBatchResult MvpCoordinator::settle(bool dry_run, CoalescedBatch events)
{
    MvpBatchResult result;
    result.events = std::move(events);
    result.transactionId = next_transaction_id_;
    result.layoutGeneration = layout_generation_;
    status_ = MvpBatchStatus::Idle;

    auto captured = capture(SnapshotRefreshReason::Event);
    if (!captured) {
        status_ = MvpBatchStatus::Rebuilding;
        result.status = status_;
        result.reason = MvpSuspendReason::SnapshotUnavailable;
        return result;
    }
    const auto active = std::find_if(captured->windows.begin(), captured->windows.end(),
                                     [this](const auto& window) {
                                         return window.key.hwnd == active_window_;
                                     });
    if (active == captured->windows.end() || !active->managed) {
        status_ = MvpBatchStatus::Suspended;
        result.status = status_;
        result.reason = MvpSuspendReason::ActiveWindowUnavailable;
        return result;
    }
    if (starting_monitor_ == 0 || active->monitor != starting_monitor_) {
        status_ = MvpBatchStatus::Suspended;
        result.status = status_;
        result.reason = MvpSuspendReason::ActiveMonitorChanged;
        return result;
    }

    const auto maximum_managed = std::clamp<std::uint32_t>(
        settings_.maxManagedWindows, 2, 20);
    std::vector<const WindowSnapshot*> managed;
    managed.reserve(maximum_managed);
    managed.push_back(&*active);
    const auto eligible_peer = [&active](const WindowSnapshot& window) {
        return window.key.hwnd != active->key.hwnd && window.managed &&
            window.monitor == active->monitor;
    };
    std::vector<const WindowSnapshot*> peers;
    for (const auto& window : captured->windows) {
        if (eligible_peer(window)) {
            peers.push_back(&window);
        }
    }
    std::stable_sort(peers.begin(), peers.end(), [&active](const auto* left, const auto* right) {
        const bool left_direct = directly_obscured_by(*active, *left);
        const bool right_direct = directly_obscured_by(*active, *right);
        if (left_direct != right_direct) {
            return left_direct;
        }
        if (!left_direct) {
            return false;
        }
        return obscured_fraction(active->visualRect, left->visualRect) >
            obscured_fraction(active->visualRect, right->visualRect);
    });
    for (const auto* peer : peers) {
        if (managed.size() == maximum_managed) {
            break;
        }
        managed.push_back(peer);
    }
    result.managedWindowCount = managed.size();
    if (managed.size() < 2) {
        status_ = MvpBatchStatus::Idle;
        result.status = status_;
        result.reason = MvpSuspendReason::NoManagedPeer;
        return result;
    }

    solver::LayoutSnapshot layout;
    layout.version = captured->version;
    std::optional<std::size_t> active_index;
    std::unordered_set<NativeWindowHandle> managed_handles;
    for (const auto* window : managed) {
        managed_handles.insert(window->key.hwnd);
    }
    for (const auto& window : captured->windows) {
        if (!valid_for_layout(window)) {
            continue;
        }
        solver::LayoutWindow item;
        item.key = window.key;
        item.placementRect = to_rect(window.placementRect);
        item.visualRect = to_rect(window.visualRect);
        item.workArea = to_rect(window.workArea);
        const auto stable = stable_layout_.find(window.key.hwnd);
        item.lastStableRect = stable != stable_layout_.end() &&
                stable->second.key == window.key
            ? stable->second.rectangle
            : item.placementRect;
        item.monitor = window.monitor;
        item.zIndex = window.zIndex;
        item.managed = managed_handles.contains(window.key.hwnd);
        item.movable = item.managed;
        item.visible = window.visible && !window.iconic && !window.cloaked;
        item.blocksVisibility = item.visible && window.currentDesktop;
        item.currentDesktop = window.currentDesktop;
        item.topmost = window.topmost;
        if (window.key.hwnd == active->key.hwnd) {
            active_index = layout.windows.size();
        }
        layout.windows.push_back(item);
    }
    if (!active_index) {
        status_ = MvpBatchStatus::Suspended;
        result.status = status_;
        result.reason = MvpSuspendReason::ActiveWindowUnavailable;
        return result;
    }

    std::uint32_t dpi = active->dpi;
    for (const auto* window : managed) {
        dpi = std::max(dpi, window->dpi);
    }
    solver::SolverPolicy policy;
    policy.ranking.visibility.minimumExposedLength = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minExposedEdgeDip, dpi));
    policy.ranking.visibility.minimumExposedDepth = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minExposedDepthDip, dpi));
    const auto preferred_exposed_edges = std::clamp<std::uint32_t>(
        settings_.preferredExposedEdges, 1, 4);
    const auto minimum_exposed_edges = std::clamp<std::uint32_t>(
        settings_.minimumExposedEdges, 1, preferred_exposed_edges);
    policy.ranking.visibility.minimumExposedEdges = preferred_exposed_edges;
    result.requiredExposedEdges = preferred_exposed_edges;
    policy.ranking.minimumOnscreenWidth = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minOnscreenWidthDip, dpi));
    policy.ranking.minimumOnscreenHeight = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minOnscreenHeightDip, dpi));
    policy.ranking.activeWindowIndex = active_index;
    policy.ranking.preferredEdge = preferred_edge_;
    policy.limits.maximumMoves = settings_.maxMovesPerBatch;
    policy.limits.maximumStates = settings_.maxSolverStates;
    policy.limits.maximumElapsedMs = settings_.maxSolveTimeMs;
    policy.repairTargetLength = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.repairTargetEdgeDip, dpi));
    const auto full_layout = layout;
    const auto full_managed_handles = managed_handles;
    const auto full_managed_count = result.managedWindowCount;
    const auto attempt_goal = [&](std::uint32_t exposed_edges) {
        layout = full_layout;
        managed_handles = full_managed_handles;
        result.managedWindowCount = full_managed_count;
        result.fallbackUsed = false;
        policy.ranking.visibility.minimumExposedEdges = exposed_edges;
        result.requiredExposedEdges = exposed_edges;
        result.solve = solver::solve_layout(layout, policy);
        if (result.solve.status == solver::SolveStatus::Solved ||
            result.solve.status == solver::SolveStatus::NoViolation) {
            return true;
        }

        const auto targets = directly_obscured_targets(
            layout, result.solve.violations, *active_index);
        for (const auto target_index : targets) {
            auto fallback_layout = layout;
            for (std::size_t index = 0; index < fallback_layout.windows.size(); ++index) {
                const bool in_fallback = index == *active_index || index == target_index;
                fallback_layout.windows[index].managed = in_fallback;
                fallback_layout.windows[index].movable = in_fallback;
            }
            const auto fallback = solver::solve_layout(fallback_layout, policy);
            if (fallback.status != solver::SolveStatus::Solved) {
                continue;
            }
            layout = std::move(fallback_layout);
            result.solve = fallback;
            managed_handles.clear();
            managed_handles.insert(layout.windows[*active_index].key.hwnd);
            managed_handles.insert(layout.windows[target_index].key.hwnd);
            result.managedWindowCount = managed_handles.size();
            result.fallbackUsed = true;
            return true;
        }
        return false;
    };

    auto solved = attempt_goal(preferred_exposed_edges);
    if (!solved && minimum_exposed_edges < preferred_exposed_edges) {
        result.edgeGoalDegraded = true;
        solved = attempt_goal(minimum_exposed_edges);
    }
    if (!solved && result.solve.status == solver::SolveStatus::Unsatisfiable) {
        result.fallbackUsed = false;
        result.managedWindowCount = full_managed_count;
        managed_handles = full_managed_handles;
        layout = full_layout;
        const auto attempt_z_order = [&](std::uint32_t exposed_edges) {
            policy.ranking.visibility.minimumExposedEdges = exposed_edges;
            result.requiredExposedEdges = exposed_edges;
            result.zOrderSolve = solver::solve_z_order_fallback(
                full_layout, policy, *active_index);
            if (result.zOrderSolve.status != solver::SolveStatus::Solved) {
                return false;
            }
            result.solve = result.zOrderSolve.positionSolve;
            result.zOrderFallbackUsed = true;
            result.reorderedWindowCount = result.zOrderSolve.reorders.size();
            return true;
        };

        result.edgeGoalDegraded = false;
        solved = attempt_z_order(preferred_exposed_edges);
        if (!solved && minimum_exposed_edges < preferred_exposed_edges &&
            result.zOrderSolve.status == solver::SolveStatus::Unsatisfiable) {
            result.edgeGoalDegraded = true;
            solved = attempt_z_order(minimum_exposed_edges);
        }
    }
    if (transaction_seen_states_.empty()) {
        transaction_seen_states_.push_back(solver::hash_layout(layout));
    }
    if (result.solve.status == solver::SolveStatus::NoViolation &&
        !result.zOrderFallbackUsed) {
        result.status = status_;
        return result;
    }
    if (result.solve.status != solver::SolveStatus::Solved &&
        result.solve.status != solver::SolveStatus::NoViolation) {
        status_ = MvpBatchStatus::Unsatisfiable;
        result.status = status_;
        result.reason = MvpSuspendReason::SolverFailure;
        return result;
    }
    if (contains_hash(transaction_seen_states_, result.solve.finalState)) {
        result.solve.status = solver::SolveStatus::Unsatisfiable;
        result.solve.moves.clear();
        status_ = MvpBatchStatus::Unsatisfiable;
        result.status = status_;
        result.reason = MvpSuspendReason::SolverFailure;
        return result;
    }
    transaction_seen_states_.push_back(result.solve.finalState);
    if (!preferred_edge_ && !result.solve.moves.empty()) {
        preferred_edge_ = move_direction(result.solve.moves.front());
    }
    std::unordered_set<NativeWindowHandle> moved_handles;
    for (const auto& move : result.solve.moves) {
        moved_handles.insert(move.window.hwnd);
    }
    result.movedWindowCount = moved_handles.size();

    MoveApplyOptions options;
    options.dryRun = dry_run;
    options.transactionId = next_transaction_id_;
    options.layoutGeneration = layout_generation_;
    result.apply = applier_.apply(
        result.solve.moves, result.zOrderSolve.reorders, options);
    if (result.apply.status == MoveApplyStatus::DryRun) {
        status_ = MvpBatchStatus::DryRun;
    } else if (result.apply.status == MoveApplyStatus::Applied) {
        auto verified_layout = result.zOrderFallbackUsed
            ? result.zOrderSolve.finalSnapshot
            : layout;
        bool verification_failed = false;
        for (auto& item : verified_layout.windows) {
            const auto actual = std::find_if(
                result.apply.finalSnapshot.windows.begin(),
                result.apply.finalSnapshot.windows.end(),
                [&item](const auto& window) { return window.key == item.key; });
            if (actual == result.apply.finalSnapshot.windows.end()) {
                verification_failed = verification_failed || item.managed;
                continue;
            }
            const auto actual_placement = to_rect(actual->placementRect);
            if (item.managed && !moved_handles.contains(item.key.hwnd) &&
                actual_placement != item.placementRect) {
                verification_failed = true;
            }
            if (item.managed && actual->monitor != item.monitor) {
                verification_failed = true;
            }
            item.placementRect = actual_placement;
            item.visualRect = to_rect(actual->visualRect);
            item.workArea = to_rect(actual->workArea);
            item.monitor = actual->monitor;
            item.visible = actual->visible && !actual->iconic && !actual->cloaked;
            item.blocksVisibility = item.visible && actual->currentDesktop;
            item.currentDesktop = actual->currentDesktop;
            item.zIndex = actual->zIndex;
            item.topmost = actual->topmost;
        }
        const auto verification = solver::scan_visibility_violations(
            verified_layout, policy.ranking.visibility);
        if (verification_failed || verification.status != solver::ViolationScanStatus::Ok ||
            !verification.violations.empty()) {
            result.apply.status = MoveApplyStatus::VerificationFailed;
            result.apply.requiresReconcile = true;
            status_ = MvpBatchStatus::ApiError;
            result.reason = MvpSuspendReason::ApplyFailure;
            result.status = status_;
            return result;
        }
        status_ = MvpBatchStatus::Applied;
        for (const auto& window : result.apply.finalSnapshot.windows) {
            if (managed_handles.contains(window.key.hwnd)) {
                stable_layout_[window.key.hwnd] = {
                    window.key, to_rect(window.placementRect)};
            }
        }
    } else {
        status_ = MvpBatchStatus::ApiError;
        result.reason = MvpSuspendReason::ApplyFailure;
    }
    result.status = status_;
    return result;
}

MvpBatchStatus MvpCoordinator::status() const noexcept
{
    return status_;
}

} // namespace stage_manager::window
