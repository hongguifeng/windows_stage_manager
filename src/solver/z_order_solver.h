#pragma once

#include "solver/layout_solver.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stage_manager::solver {

struct ZOrderPlan {
    window::WindowKey window;
    window::WindowKey insertAfter;
    std::int32_t fromZIndex = -1;
    std::int32_t toZIndex = -1;
};

struct ZOrderSolveResult {
    SolveStatus status = SolveStatus::InvalidSnapshot;
    std::vector<ZOrderPlan> reorders;
    SolveResult positionSolve;
    LayoutSnapshot finalSnapshot;
    LayoutHash finalState;
    std::size_t candidatesTried = 0;
};

// Call only after the unchanged-Z-order position solve is Unsatisfiable.
// Candidates first preserve the longest possible top-of-stack prefix. Within
// the same changed suffix, lower inactive managed targets are tried first.
ZOrderSolveResult solve_z_order_fallback(const LayoutSnapshot& initial,
                                         const SolverPolicy& policy,
                                         std::size_t active_window_index,
                                         ISolverClock* clock = nullptr);

} // namespace stage_manager::solver
