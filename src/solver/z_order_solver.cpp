#include "solver/z_order_solver.h"

#include <algorithm>
#include <numeric>
#include <optional>
#include <span>
#include <vector>

namespace stage_manager::solver {
namespace {

std::optional<std::vector<std::size_t>> ordered_indices(const LayoutSnapshot& snapshot)
{
    std::vector<std::size_t> order(snapshot.windows.size());
    std::iota(order.begin(), order.end(), 0);
    for (const auto& window : snapshot.windows) {
        if (window.zIndex < 0) {
            return std::nullopt;
        }
    }
    std::stable_sort(order.begin(), order.end(), [&snapshot](auto left, auto right) {
        return snapshot.windows[left].zIndex < snapshot.windows[right].zIndex;
    });
    for (std::size_t index = 1; index < order.size(); ++index) {
        if (snapshot.windows[order[index - 1]].zIndex ==
            snapshot.windows[order[index]].zIndex) {
            return std::nullopt;
        }
    }
    return order;
}

LayoutSnapshot promote_after_active(const LayoutSnapshot& initial,
                                    std::span<const std::size_t> original_order,
                                    std::size_t active_index,
                                    std::size_t target_index)
{
    std::vector<std::size_t> order(original_order.begin(), original_order.end());
    order.erase(std::find(order.begin(), order.end(), target_index));
    const auto active = std::find(order.begin(), order.end(), active_index);
    order.insert(active + 1, target_index);

    auto reordered = initial;
    for (std::size_t z_index = 0; z_index < order.size(); ++z_index) {
        reordered.windows[order[z_index]].zIndex = static_cast<std::int32_t>(z_index);
    }
    return reordered;
}

ZOrderSolveResult terminal_result(SolveStatus status,
                                  const LayoutSnapshot& snapshot,
                                  std::size_t candidates_tried)
{
    ZOrderSolveResult result;
    result.status = status;
    result.finalSnapshot = snapshot;
    result.finalState = hash_layout(snapshot);
    result.candidatesTried = candidates_tried;
    return result;
}

} // namespace

ZOrderSolveResult solve_z_order_fallback(const LayoutSnapshot& initial,
                                         const SolverPolicy& policy,
                                         std::size_t active_window_index,
                                         ISolverClock* clock)
{
    const auto order = ordered_indices(initial);
    if (!order || active_window_index >= initial.windows.size()) {
        return terminal_result(SolveStatus::InvalidSnapshot, initial, 0);
    }

    const auto& active = initial.windows[active_window_index];
    if (active.key.hwnd == 0 || active.topmost) {
        return terminal_result(SolveStatus::Unsatisfiable, initial, 0);
    }

    std::vector<std::size_t> candidates;
    for (const auto index : *order) {
        const auto& window = initial.windows[index];
        if (index != active_window_index && window.managed && window.visible &&
            window.currentDesktop && !window.topmost && window.zIndex > active.zIndex) {
            candidates.push_back(index);
        }
    }
    std::stable_sort(candidates.begin(), candidates.end(), [&initial](auto left, auto right) {
        return initial.windows[left].zIndex > initial.windows[right].zIndex;
    });

    std::size_t candidates_tried = 0;
    for (const auto target_index : candidates) {
        if (initial.windows[target_index].zIndex == active.zIndex + 1) {
            continue;
        }
        ++candidates_tried;
        auto reordered = promote_after_active(
            initial, *order, active_window_index, target_index);
        auto solved = solve_layout(reordered, policy, clock);
        if (solved.status == SolveStatus::InvalidSnapshot ||
            solved.status == SolveStatus::GeometryTooComplex ||
            solved.status == SolveStatus::Timeout) {
            auto result = terminal_result(solved.status, initial, candidates_tried);
            result.positionSolve = std::move(solved);
            return result;
        }
        if (solved.status != SolveStatus::Solved &&
            solved.status != SolveStatus::NoViolation) {
            continue;
        }

        ZOrderSolveResult result;
        result.status = SolveStatus::Solved;
        result.reorders.push_back({
            initial.windows[target_index].key,
            active.key,
            initial.windows[target_index].zIndex,
            reordered.windows[target_index].zIndex,
        });
        result.positionSolve = std::move(solved);
        result.finalSnapshot = result.positionSolve.finalSnapshot;
        result.finalState = result.positionSolve.finalState;
        result.candidatesTried = candidates_tried;
        return result;
    }
    return terminal_result(SolveStatus::Unsatisfiable, initial, candidates_tried);
}

} // namespace stage_manager::solver
