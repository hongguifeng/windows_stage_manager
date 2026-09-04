#include "window/mvp_coordinator.h"

#include "geometry/dpi.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
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

std::uint32_t maximum_dpi(const WindowSnapshot& first, const WindowSnapshot& second) noexcept
{
    return std::max(first.dpi, second.dpi);
}

} // namespace

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
    for (const auto& event : result.events.events) {
        provider_.handle_event(event);
        if (event.type == WindowEventType::MoveSizeStart) {
            guard_.observe(event);
            active_window_ = event.hwnd;
            ++next_transaction_id_;
            ++layout_generation_;
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

    const auto peer = std::find_if(captured->windows.begin(), captured->windows.end(),
                                   [&active](const auto& window) {
                                       return window.key.hwnd != active->key.hwnd &&
                                           window.managed && window.monitor == active->monitor;
                                   });
    if (peer == captured->windows.end()) {
        status_ = MvpBatchStatus::Idle;
        result.status = status_;
        result.reason = MvpSuspendReason::NoManagedPeer;
        return result;
    }

    solver::LayoutSnapshot layout;
    layout.version = captured->version;
    std::optional<std::size_t> active_index;
    for (const auto& window : captured->windows) {
        if (!valid_for_layout(window)) {
            continue;
        }
        solver::LayoutWindow item;
        item.key = window.key;
        item.placementRect = to_rect(window.placementRect);
        item.visualRect = to_rect(window.visualRect);
        item.workArea = to_rect(window.workArea);
        item.lastStableRect = item.placementRect;
        item.monitor = window.monitor;
        item.zIndex = window.zIndex;
        item.managed = window.key.hwnd == active->key.hwnd ||
            window.key.hwnd == peer->key.hwnd;
        item.movable = item.managed;
        item.visible = window.visible && !window.iconic && !window.cloaked;
        item.blocksVisibility = item.visible && window.currentDesktop;
        item.currentDesktop = window.currentDesktop;
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

    const auto dpi = maximum_dpi(*active, *peer);
    solver::SolverPolicy policy;
    policy.ranking.visibility.minimumExposedLength = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minExposedEdgeDip, dpi));
    policy.ranking.visibility.minimumExposedDepth = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minExposedDepthDip, dpi));
    policy.ranking.minimumOnscreenWidth = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minOnscreenWidthDip, dpi));
    policy.ranking.minimumOnscreenHeight = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.minOnscreenHeightDip, dpi));
    policy.ranking.activeWindowIndex = active_index;
    policy.limits.maximumMoves = settings_.maxMovesPerBatch;
    policy.limits.maximumStates = settings_.maxSolverStates;
    policy.limits.maximumElapsedMs = settings_.maxSolveTimeMs;
    policy.repairTargetLength = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(settings_.repairTargetEdgeDip, dpi));
    result.solve = solver::solve_layout(layout, policy);
    if (result.solve.status == solver::SolveStatus::NoViolation) {
        result.status = status_;
        return result;
    }
    if (result.solve.status != solver::SolveStatus::Solved) {
        status_ = MvpBatchStatus::Unsatisfiable;
        result.status = status_;
        result.reason = MvpSuspendReason::SolverFailure;
        return result;
    }

    MoveApplyOptions options;
    options.dryRun = dry_run;
    options.transactionId = next_transaction_id_;
    options.layoutGeneration = layout_generation_;
    result.apply = applier_.apply(result.solve.moves, options);
    if (result.apply.status == MoveApplyStatus::DryRun) {
        status_ = MvpBatchStatus::DryRun;
    } else if (result.apply.status == MoveApplyStatus::Applied) {
        status_ = MvpBatchStatus::Applied;
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
