#pragma once

#include "solver/candidate_ranker.h"
#include "solver/layout_hash.h"
#include "solver/layout_snapshot.h"
#include "solver/visibility_analyzer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stage_manager::solver {

enum class SolveStatus : std::uint8_t {
    Solved,
    PartiallySolved,
    NoViolation,
    Unsatisfiable,
    InvalidSnapshot,
    GeometryTooComplex,
    Timeout,
};

struct SolverLimits {
    std::uint32_t maximumMoves = 32;
    std::uint32_t maximumStates = 512;
    std::uint64_t maximumElapsedMs = 16;
    std::size_t maximumCandidatesPerViolation = 512;
};

struct SolverPolicy {
    CandidateRankingPolicy ranking;
    SolverLimits limits;
};

struct MovePlan {
    window::WindowKey window;
    geometry::Rect from;
    geometry::Rect to;
    CandidateCost cost;
};

struct CandidateRejection {
    window::WindowKey window;
    geometry::Rect placementRect;
    HardConstraintFailure failure = HardConstraintFailure::None;
};

class ISolverClock {
public:
    virtual ~ISolverClock() = default;
    virtual std::uint64_t now_ms() = 0;
};

struct SolveResult {
    SolveStatus status = SolveStatus::InvalidSnapshot;
    std::vector<MovePlan> moves;
    std::vector<Violation> violations;
    std::vector<CandidateRejection> candidateRejections;
    LayoutSnapshot finalSnapshot;
    LayoutHash finalState;
    std::uint32_t statesVisited = 0;
    std::uint64_t elapsedMs = 0;
};

SolveResult solve_layout(const LayoutSnapshot& initial,
                         const SolverPolicy& policy,
                         ISolverClock* clock = nullptr);

SolveResult solve_layout_incrementally(const LayoutSnapshot& initial,
                                       const SolverPolicy& policy,
                                       std::size_t active_window_index,
                                       ISolverClock* clock = nullptr);

// Builds a deterministic position-only staircase anchored to the active window.
// The complete chain is tried top-left first, then top-right; one-edge side and
// bottom chains are available only for the degraded visibility goal. Z-order is
// immutable and determines the order of windows within a chain.
SolveResult solve_layout_prioritized(const LayoutSnapshot& initial,
                                     const SolverPolicy& policy,
                                     std::size_t active_window_index,
                                     ISolverClock* clock = nullptr);

} // namespace stage_manager::solver
