#pragma once

#include "app/settings.h"
#include "solver/layout_solver.h"
#include "solver/z_order_solver.h"
#include "window/event_coalescer.h"
#include "window/move_applier.h"
#include "window/move_transaction.h"
#include "window/window_classifier.h"
#include "window/window_provider.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace stage_manager::window {

enum class MvpBatchStatus : std::uint8_t {
    Disabled,
    Idle,
    Dragging,
    DryRun,
    Applied,
    Unsatisfiable,
    Suspended,
    ApiError,
    Rebuilding,
};

enum class MvpSuspendReason : std::uint8_t {
    None,
    SnapshotUnavailable,
    ActiveWindowUnavailable,
    ActiveMonitorChanged,
    NoManagedPeer,
    SolverFailure,
    ApplyFailure,
};

std::string_view suspend_reason_name(MvpSuspendReason reason) noexcept;

struct MvpBatchResult {
    MvpBatchStatus status = MvpBatchStatus::Idle;
    MvpSuspendReason reason = MvpSuspendReason::None;
    std::uint64_t transactionId = 0;
    std::uint64_t layoutGeneration = 0;
    CoalescedBatch events;
    solver::SolveResult solve;
    solver::ZOrderSolveResult zOrderSolve;
    MoveApplyResult apply;
    std::size_t managedWindowCount = 0;
    std::size_t movedWindowCount = 0;
    std::size_t reorderedWindowCount = 0;
    bool fallbackUsed = false;
    std::uint32_t requiredExposedEdges = 1;
    bool edgeGoalDegraded = false;
    bool zOrderFallbackUsed = false;
};

class MvpCoordinator final {
public:
    MvpCoordinator(IWindowProvider& provider,
                   IMoveApplier& applier,
                   MoveTransactionGuard& guard,
                   InternalMoveTracker& internal_moves,
                   ConservativeWindowClassifier classifier,
                   app::Settings settings = {});

    MvpBatchResult process(std::span<const WindowEvent> events,
                           bool enabled,
                           bool dry_run);
    MvpBatchStatus status() const noexcept;

private:
    std::optional<WindowSnapshotBatch> capture(SnapshotRefreshReason reason);
    MvpBatchResult settle(bool dry_run, CoalescedBatch events);

    IWindowProvider& provider_;
    IMoveApplier& applier_;
    MoveTransactionGuard& guard_;
    InternalMoveTracker& internal_moves_;
    ConservativeWindowClassifier classifier_;
    app::Settings settings_;
    EventCoalescer coalescer_;
    MvpBatchStatus status_ = MvpBatchStatus::Idle;
    NativeWindowHandle active_window_ = 0;
    NativeMonitorHandle starting_monitor_ = 0;
    std::uint64_t next_transaction_id_ = 0;
    std::uint64_t layout_generation_ = 0;
    struct StablePlacement {
        WindowKey key;
        geometry::Rect rectangle;
    };
    std::unordered_map<NativeWindowHandle, StablePlacement> stable_layout_;
    std::vector<solver::LayoutHash> transaction_seen_states_;
    std::optional<geometry::Edge> preferred_edge_;
};

} // namespace stage_manager::window
