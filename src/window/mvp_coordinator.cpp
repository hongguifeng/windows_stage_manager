#include "window/mvp_coordinator.h"

#include "geometry/dpi.h"
#include "window/activation_placement.h"

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

bool same_rectangle(const PixelRect& left, const PixelRect& right) noexcept
{
    return left.left == right.left && left.top == right.top &&
        left.right == right.right && left.bottom == right.bottom;
}

bool directly_obscured_by(const WindowSnapshot& blocker,
                          const WindowSnapshot& target) noexcept
{
    return blocker.zOrderKnown && target.zOrderKnown &&
        blocker.zIndex < target.zIndex &&
        rectangles_intersect(blocker.visualRect, target.visualRect);
}

bool can_block_managed_layout(
    const WindowSnapshot& candidate,
    const std::vector<const WindowSnapshot*>& managed_windows) noexcept
{
    if (!valid_for_layout(candidate) || !candidate.visible || candidate.iconic ||
        candidate.cloaked || !candidate.currentDesktop) {
        return false;
    }

    return std::any_of(
        managed_windows.begin(), managed_windows.end(), [&candidate](const auto* target) {
            return target != nullptr && candidate.monitor == target->monitor &&
                candidate.zIndex < target->zIndex &&
                rectangles_intersect(candidate.visualRect, target->workArea);
        });
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
        suppressed_activation_window_.reset();
        dragging_window_.reset();
        dragging_start_rect_.reset();
        dragging_started_by_activation_ = false;
        dragging_location_change_seen_ = false;
        status_ = MvpBatchStatus::Disabled;
        result.status = status_;
        return result;
    }

    std::optional<NativeWindowHandle> ended_drag_window;
    std::optional<NativeWindowHandle> foreground_window;
    bool foreground_changed = false;
    bool foreground_suppresses_layout = false;
    for (const auto& event : result.events.events) {
        provider_.handle_event(event);
        if (event.type == WindowEventType::MoveSizeStart) {
            guard_.observe(event);
            const bool activation_preceded_drag = foreground_window &&
                foreground_changed && *foreground_window == event.hwnd;
            const bool suppressed_activation_preceded_drag =
                activation_preceded_drag && foreground_suppresses_layout;
            ended_drag_window.reset();
            if (!suppressed_activation_preceded_drag) {
                foreground_window.reset();
                foreground_changed = false;
                foreground_suppresses_layout = false;
            }
            active_window_ = event.hwnd;
            dragging_window_ = event.hwnd;
            dragging_start_rect_.reset();
            dragging_started_by_activation_ = activation_preceded_drag;
            dragging_location_change_seen_ = false;
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
            if (active != initial->windows.end()) {
                dragging_start_rect_ = active->placementRect;
                if (active->managed && suppressed_activation_window_ &&
                    active->key.hwnd != *suppressed_activation_window_) {
                    suppressed_activation_window_.reset();
                }
            }
            guard_.activate(next_transaction_id_, layout_generation_);
            status_ = MvpBatchStatus::Dragging;
        } else if (event.type == WindowEventType::Foreground) {
            foreground_window = event.hwnd;
            foreground_suppresses_layout = event.suppressLayout;
            const bool changed_now = event.hwnd != foreground_window_;
            if (changed_now) {
                foreground_changed = true;
                foreground_window_ = event.hwnd;
            }
            if (changed_now && dragging_window_ && event.hwnd == *dragging_window_) {
                dragging_started_by_activation_ = true;
            }
        } else if (event.type == WindowEventType::MoveSizeEnd &&
                   dragging_window_ && event.hwnd == *dragging_window_) {
            ended_drag_window = event.hwnd;
        } else if (event.type == WindowEventType::LocationChange) {
            const auto token = internal_moves_.find(event.hwnd);
            if (token) {
                internal_moves_.complete(*token);
            } else if (dragging_window_ && event.hwnd == *dragging_window_) {
                dragging_location_change_seen_ = true;
            }
        } else if (event.type == WindowEventType::Destroy &&
                   suppressed_activation_window_ &&
                   event.hwnd == *suppressed_activation_window_) {
            suppressed_activation_window_.reset();
        } else if (event.type == WindowEventType::HookError) {
            status_ = MvpBatchStatus::Rebuilding;
        }
    }

    // A genuine foreground transition supersedes an older move/size transaction.
    // This also recovers when Windows omits the corresponding MoveSizeEnd event.
    // A foreground event for the window currently being dragged is intentionally
    // ignored so a user drag is never replaced by activation placement.
    const bool returns_to_suppressed_activation = foreground_window &&
        suppressed_activation_window_ &&
        *foreground_window == *suppressed_activation_window_;
    if (foreground_window && foreground_changed &&
        (foreground_suppresses_layout || returns_to_suppressed_activation)) {
        guard_.cancel();
        dragging_window_.reset();
        dragging_start_rect_.reset();
        dragging_started_by_activation_ = false;
        dragging_location_change_seen_ = false;
        active_window_ = *foreground_window;
        if (foreground_suppresses_layout) {
            suppressed_activation_window_ = *foreground_window;
        }
        status_ = MvpBatchStatus::Idle;
        result.status = status_;
        result.transactionId = next_transaction_id_;
        result.layoutGeneration = layout_generation_;
        result.activationLayoutSuppressed = true;
        return result;
    }
    if (foreground_window && foreground_changed &&
        (!dragging_window_ || *foreground_window != *dragging_window_)) {
        dragging_window_.reset();
        dragging_start_rect_.reset();
        dragging_started_by_activation_ = false;
        dragging_location_change_seen_ = false;
        active_window_ = *foreground_window;
        ++next_transaction_id_;
        ++layout_generation_;
        transaction_seen_states_.clear();
        preferred_edge_.reset();
        std::optional<WindowSnapshotBatch> initial;
        constexpr int kMaximumCaptureAttempts = 3;
        for (int attempt = 0; attempt < kMaximumCaptureAttempts; ++attempt) {
            initial = capture(SnapshotRefreshReason::Event);
            if (!initial) {
                continue;
            }
            const auto candidate = std::find_if(
                initial->windows.begin(), initial->windows.end(), [this](const auto& window) {
                    return window.key.hwnd == active_window_;
                });
            if (candidate == initial->windows.end()) {
                continue;
            }
            if (candidate->managed ||
                classifier_.classify(*candidate, initial->windows).reason !=
                    UnmanagedReason::ApiQueryFailed) {
                break;
            }
        }
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
        if (active != initial->windows.end() && active->managed &&
            suppressed_activation_window_ &&
            active->key.hwnd != *suppressed_activation_window_) {
            suppressed_activation_window_.reset();
        }
        guard_.activate(next_transaction_id_, layout_generation_);
        return settle(
            dry_run,
            std::move(result.events),
            settings_.placeActivatedWindow,
            std::move(initial));
    }
    if (ended_drag_window && dragging_window_ &&
        *ended_drag_window == *dragging_window_) {
        const bool place_activated_window = dragging_started_by_activation_ &&
            !dragging_location_change_seen_ && settings_.placeActivatedWindow;
        const auto unchanged_activation_rect = dragging_start_rect_;
        foreground_window_ = *ended_drag_window;
        dragging_window_.reset();
        dragging_start_rect_.reset();
        dragging_started_by_activation_ = false;
        dragging_location_change_seen_ = false;
        return settle(dry_run,
                      std::move(result.events),
                      place_activated_window,
                      std::nullopt,
                      unchanged_activation_rect);
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

MvpBatchResult MvpCoordinator::settle(bool dry_run,
                                      CoalescedBatch events,
                                      bool place_activated_window,
                                      std::optional<WindowSnapshotBatch> captured,
                                      std::optional<PixelRect> unchanged_activation_rect)
{
    MvpBatchResult result;
    result.events = std::move(events);
    result.transactionId = next_transaction_id_;
    result.layoutGeneration = layout_generation_;
    status_ = MvpBatchStatus::Idle;

    if (!captured) {
        captured = capture(SnapshotRefreshReason::Event);
    }
    if (!captured) {
        status_ = MvpBatchStatus::Rebuilding;
        result.status = status_;
        result.reason = MvpSuspendReason::SnapshotUnavailable;
        return result;
    }
    result.snapshotWindowCount = captured->windows.size();
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
    if (place_activated_window && unchanged_activation_rect &&
        !same_rectangle(active->placementRect, *unchanged_activation_rect)) {
        place_activated_window = false;
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
    if (managed.size() < 2 && !place_activated_window) {
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
        const bool is_managed = managed_handles.contains(window.key.hwnd);
        if (!is_managed && !can_block_managed_layout(window, managed)) {
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
        item.dpi = window.dpi;
        item.titleBarHeight = window.titleBarHeight;
        item.zIndex = window.zIndex;
        item.managed = is_managed;
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
    result.solverWindowCount = layout.windows.size();
    result.blockingWindowCount = result.solverWindowCount - result.managedWindowCount;
    if (!active_index) {
        status_ = MvpBatchStatus::Suspended;
        result.status = status_;
        result.reason = MvpSuspendReason::ActiveWindowUnavailable;
        return result;
    }

    const auto observed_state = solver::hash_layout(layout);
    std::optional<solver::MovePlan> activation_move;
    if (place_activated_window) {
        auto& active_item = layout.windows[*active_index];
        const auto placement = calculate_activated_placement(
            active_item,
            settings_.activationHorizontalAlignment,
            settings_.activationVerticalAlignment);
        if (placement && *placement != active_item.placementRect) {
            const auto delta_x = placement->left - active_item.placementRect.left;
            const auto delta_y = placement->top - active_item.placementRect.top;
            const auto centered_visual = active_item.visualRect.translated(delta_x, delta_y);
            if (centered_visual) {
                solver::MovePlan move;
                move.window = active_item.key;
                move.from = active_item.placementRect;
                move.to = *placement;
                move.cost.movedWindowCount = 1;
                move.cost.centerDistance = 0;
                active_item.placementRect = *placement;
                active_item.visualRect = *centered_visual;
                activation_move = move;
            }
        }
    }

    std::uint32_t dpi = active->dpi;
    for (const auto* window : managed) {
        dpi = std::max(dpi, window->dpi);
    }
    solver::SolverPolicy policy;
    policy.ranking.visibility.top = {
        settings_.topMinimumLengthDip, settings_.topMaximumLengthDip,
        settings_.topDepthDip, settings_.topLengthPercent};
    policy.ranking.visibility.left = {
        settings_.leftMinimumLengthDip, settings_.leftMaximumLengthDip,
        settings_.leftDepthDip, settings_.leftLengthPercent};
    policy.ranking.visibility.right = {
        settings_.rightMinimumLengthDip, settings_.rightMaximumLengthDip,
        settings_.rightDepthDip, settings_.rightLengthPercent};
    policy.ranking.visibility.bottom = {
        settings_.bottomMinimumLengthDip, settings_.bottomMaximumLengthDip,
        settings_.bottomDepthDip, settings_.bottomLengthPercent};
    policy.ranking.visibility.goal = solver::VisibilityGoal::TopAndSide;
    result.affordanceGoal = solver::VisibilityGoal::TopAndSide;
    policy.ranking.minimumOnscreenWidth = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minOnscreenWidthDip, dpi));
    policy.ranking.minimumOnscreenHeight = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minOnscreenHeightDip, dpi));
    policy.ranking.activeWindowIndex = active_index;
    policy.ranking.preferredEdge = preferred_edge_;
    const auto maximum_moves = std::max(settings_.maxMovesPerBatch, 1u);
    const auto remaining_position_moves = maximum_moves -
        (activation_move.has_value() ? 1u : 0u);
    // solve_layout requires a positive search limit even when the centered layout
    // already satisfies every constraint. Keep its scan enabled, then reject any
    // peer moves that would exceed the batch budget below.
    policy.limits.maximumMoves = std::max(remaining_position_moves, 1u);
    policy.limits.maximumStates = settings_.maxSolverStates;
    policy.limits.maximumElapsedMs = settings_.maxSolveTimeMs;
    const auto full_layout = layout;
    const auto full_managed_handles = managed_handles;
    const auto full_managed_count = result.managedWindowCount;
    result.solveAttempted = true;
    std::optional<solver::SolveResult> strict_partial;
    const auto include_activation_move = [&] {
        if (!activation_move) {
            return;
        }
        result.solve.moves.insert(result.solve.moves.begin(), *activation_move);
        if (result.solve.status == solver::SolveStatus::NoViolation) {
            result.solve.status = solver::SolveStatus::Solved;
        }
        result.activationPlacementUsed = true;
    };
    const auto attempt_goal = [&](std::uint32_t exposed_edges) {
        layout = full_layout;
        managed_handles = full_managed_handles;
        result.managedWindowCount = full_managed_count;
        result.fallbackUsed = false;
        policy.ranking.visibility.goal = exposed_edges > 1
            ? solver::VisibilityGoal::TopAndSide
            : solver::VisibilityGoal::AnyRecognizableEdge;
        result.affordanceGoal = exposed_edges > 1
            ? solver::VisibilityGoal::TopAndSide
            : solver::VisibilityGoal::AnyRecognizableEdge;
        result.solve = solver::solve_layout(layout, policy);
        if (result.solve.status == solver::SolveStatus::Solved ||
            result.solve.status == solver::SolveStatus::NoViolation) {
            if (result.solve.moves.size() <= remaining_position_moves) {
                include_activation_move();
                return true;
            }
            result.solve.status = solver::SolveStatus::Unsatisfiable;
            result.solve.moves.clear();
            result.solve.finalSnapshot = layout;
            result.solve.finalState = solver::hash_layout(layout);
        }

        // For the relaxed goal, the prioritized solver is both the completion
        // fallback and the source of a safe upper-window prefix when the whole
        // desktop cannot be solved in time. This avoids running both the
        // all-or-nothing incremental pass and the prioritized pass.
        const bool retryable = result.solve.status == solver::SolveStatus::Unsatisfiable ||
            result.solve.status == solver::SolveStatus::Timeout;
        if (!retryable) {
            return false;
        }
        auto fallback = exposed_edges > 1
            ? solver::solve_layout_incrementally(layout, policy, *active_index)
            : solver::solve_layout_prioritized(layout, policy, *active_index);
        if (fallback.status == solver::SolveStatus::Solved &&
            fallback.moves.size() <= remaining_position_moves) {
            result.solve = std::move(fallback);
            include_activation_move();
            result.fallbackUsed = true;
            return true;
        }
        if (fallback.status == solver::SolveStatus::PartiallySolved &&
            fallback.moves.size() <= remaining_position_moves) {
            if (exposed_edges == 1) {
                result.solve = std::move(fallback);
                include_activation_move();
                result.fallbackUsed = true;
                result.partialLayoutUsed = true;
                return true;
            }
            strict_partial = fallback;
        }
        result.solve = std::move(fallback);
        return false;
    };

    auto solved = attempt_goal(2);
    if (!solved) {
        result.affordanceGoalDegraded = true;
        solved = attempt_goal(1);
    }
    if (!solved && strict_partial) {
        result.managedWindowCount = full_managed_count;
        managed_handles = full_managed_handles;
        layout = full_layout;
        policy.ranking.visibility.goal = solver::VisibilityGoal::TopAndSide;
        result.affordanceGoal = solver::VisibilityGoal::TopAndSide;
        result.affordanceGoalDegraded = false;
        result.solve = std::move(*strict_partial);
        include_activation_move();
        result.fallbackUsed = true;
        result.partialLayoutUsed = true;
        solved = true;
    }
    if (!solved && activation_move) {
        // Activation placement is an independent user-facing action. A peer
        // visibility failure must not silently cancel it.
        policy.ranking.visibility.goal = solver::VisibilityGoal::AnyRecognizableEdge;
        result.affordanceGoal = solver::VisibilityGoal::AnyRecognizableEdge;
        result.affordanceGoalDegraded = true;
        const auto remaining = solver::scan_visibility_violations(
            full_layout, policy.ranking.visibility);
        if (remaining.status == solver::ViolationScanStatus::Ok) {
            result.solve.status = remaining.violations.empty()
                ? solver::SolveStatus::Solved
                : solver::SolveStatus::PartiallySolved;
            result.solve.moves = {*activation_move};
            result.solve.violations = remaining.violations;
            result.solve.finalSnapshot = full_layout;
            result.solve.finalState = solver::hash_layout(full_layout);
            result.activationPlacementUsed = true;
            result.partialLayoutUsed = !remaining.violations.empty();
            solved = true;
        }
    }
    if (transaction_seen_states_.empty()) {
        transaction_seen_states_.push_back(observed_state);
    }
    if (result.solve.status == solver::SolveStatus::NoViolation) {
        result.status = status_;
        return result;
    }
    if (result.solve.status != solver::SolveStatus::Solved &&
        result.solve.status != solver::SolveStatus::PartiallySolved &&
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
        const auto peer_move = std::find_if(
            result.solve.moves.begin(), result.solve.moves.end(), [&active](const auto& move) {
                return move.window != active->key;
            });
        if (peer_move != result.solve.moves.end()) {
            preferred_edge_ = move_direction(*peer_move);
        }
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
    result.apply = applier_.apply(result.solve.moves, options);
    if (result.apply.status == MoveApplyStatus::DryRun) {
        status_ = MvpBatchStatus::DryRun;
    } else if (result.apply.status == MoveApplyStatus::Applied) {
        auto verified_layout = result.solve.finalSnapshot;
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
            if (actual->zIndex != item.zIndex) {
                verification_failed = true;
            }
            item.placementRect = actual_placement;
            item.visualRect = to_rect(actual->visualRect);
            item.workArea = to_rect(actual->workArea);
            item.monitor = actual->monitor;
            item.dpi = actual->dpi;
            item.titleBarHeight = actual->titleBarHeight;
            item.visible = actual->visible && !actual->iconic && !actual->cloaked;
            item.blocksVisibility = item.visible && actual->currentDesktop;
            item.currentDesktop = actual->currentDesktop;
            item.zIndex = actual->zIndex;
            item.topmost = actual->topmost;
        }
        const auto verification = solver::scan_visibility_violations(
            verified_layout, policy.ranking.visibility);
        const auto violation_targets = [](const auto& snapshot, const auto& violations) {
            std::vector<WindowKey> targets;
            targets.reserve(violations.size());
            for (const auto& violation : violations) {
                if (violation.targetIndex < snapshot.windows.size()) {
                    targets.push_back(snapshot.windows[violation.targetIndex].key);
                }
            }
            std::sort(targets.begin(), targets.end(), [](const auto& left, const auto& right) {
                if (left.hwnd != right.hwnd) {
                    return left.hwnd < right.hwnd;
                }
                if (left.processId != right.processId) {
                    return left.processId < right.processId;
                }
                return left.instanceGeneration < right.instanceGeneration;
            });
            return targets;
        };
        const bool violations_match = result.solve.status ==
                solver::SolveStatus::PartiallySolved
            ? violation_targets(verified_layout, verification.violations) ==
                violation_targets(result.solve.finalSnapshot, result.solve.violations)
            : verification.violations.empty();
        if (verification_failed || verification.status != solver::ViolationScanStatus::Ok ||
            !violations_match) {
            result.apply.status = MoveApplyStatus::VerificationFailed;
            result.apply.requiresReconcile = true;
            status_ = MvpBatchStatus::ApiError;
            result.reason = MvpSuspendReason::ApplyFailure;
            result.status = status_;
            return result;
        }
        status_ = result.solve.status == solver::SolveStatus::PartiallySolved
            ? MvpBatchStatus::PartiallySolved
            : MvpBatchStatus::Applied;
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
