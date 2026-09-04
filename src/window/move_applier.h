#pragma once

#include "solver/layout_solver.h"
#include "window/internal_move_tracker.h"
#include "window/window_provider.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace stage_manager::window {

enum class NativeMoveStatus : std::uint8_t {
    Moved,
    InvalidWindow,
    IdentityMismatch,
    InvalidPosition,
    ApiFailure,
};

struct NativeMoveResult {
    NativeMoveStatus status = NativeMoveStatus::ApiFailure;
    std::uint32_t lastError = 0;
};

class IWindowMover {
public:
    virtual ~IWindowMover() = default;
    virtual NativeMoveResult move(const WindowKey& window,
                                  const geometry::Rect& destination) = 0;
};

enum class MoveApplyStatus : std::uint8_t {
    Applied,
    DryRun,
    InvalidRequest,
    SnapshotFailed,
    WindowUnavailable,
    NativeMoveFailed,
    VerificationFailed,
};

struct MoveApplyOptions {
    bool dryRun = true;
    std::uint64_t transactionId = 0;
    std::uint64_t layoutGeneration = 0;
    std::uint32_t positionTolerance = 2;
};

struct AppliedMove {
    solver::MovePlan plan;
    InternalMoveToken token;
    PixelRect actualPlacementRect;
};

struct MoveApplyResult {
    MoveApplyStatus status = MoveApplyStatus::InvalidRequest;
    std::size_t failedMoveIndex = 0;
    std::uint32_t lastError = 0;
    NativeMoveStatus nativeStatus = NativeMoveStatus::Moved;
    std::vector<AppliedMove> appliedMoves;
    WindowSnapshotBatch finalSnapshot;
};

class IMoveApplier {
public:
    virtual ~IMoveApplier() = default;
    virtual MoveApplyResult apply(std::span<const solver::MovePlan> plan,
                                  const MoveApplyOptions& options) = 0;
};

class VerifiedMoveApplier final : public IMoveApplier {
public:
    VerifiedMoveApplier(IWindowMover& mover,
                        IWindowProvider& provider,
                        InternalMoveTracker& tracker);

    MoveApplyResult apply(std::span<const solver::MovePlan> plan,
                          const MoveApplyOptions& options) override;

private:
    IWindowMover& mover_;
    IWindowProvider& provider_;
    InternalMoveTracker& tracker_;
};

} // namespace stage_manager::window
