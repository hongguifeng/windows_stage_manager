#include "solver/layout_solver.h"

#include "solver/candidate_generator.h"
#include "solver/candidate_ranker.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <utility>
#include <vector>

namespace stage_manager::solver {
namespace {

class SteadySolverClock final : public ISolverClock {
public:
    std::uint64_t now_ms() override
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

struct SearchNode {
    LayoutSnapshot snapshot;
    std::vector<MovePlan> moves;
};

bool contains_hash(std::span<const LayoutHash> hashes, const LayoutHash& hash)
{
    return std::find(hashes.begin(), hashes.end(), hash) != hashes.end();
}

std::uint64_t elapsed_since(std::uint64_t start, std::uint64_t now) noexcept
{
    return now >= start ? now - start : 0;
}

SolveResult terminal_result(SolveStatus status,
                            const LayoutSnapshot& snapshot,
                            std::vector<MovePlan> moves,
                            std::vector<Violation> violations,
                            std::uint32_t states_visited,
                            std::uint64_t start,
                            ISolverClock& clock)
{
    SolveResult result;
    result.status = status;
    result.moves = std::move(moves);
    result.violations = std::move(violations);
    result.finalSnapshot = snapshot;
    result.finalState = hash_layout(snapshot);
    result.statesVisited = states_visited;
    result.elapsedMs = elapsed_since(start, clock.now_ms());
    return result;
}

} // namespace

SolveResult solve_layout(const LayoutSnapshot& initial,
                         const SolverPolicy& policy,
                         ISolverClock* supplied_clock)
{
    SteadySolverClock default_clock;
    ISolverClock& clock = supplied_clock == nullptr ? static_cast<ISolverClock&>(default_clock)
                                                    : *supplied_clock;
    const auto start = clock.now_ms();
    if (policy.limits.maximumMoves == 0 || policy.limits.maximumStates == 0 ||
        policy.limits.maximumElapsedMs == 0 ||
        policy.limits.maximumCandidatesPerViolation == 0) {
        return terminal_result(
            SolveStatus::InvalidSnapshot, initial, {}, {}, 0, start, clock);
    }

    const auto initial_scan = scan_visibility_violations(initial, policy.ranking.visibility);
    if (initial_scan.status == ViolationScanStatus::InvalidSnapshot) {
        return terminal_result(
            SolveStatus::InvalidSnapshot, initial, {}, {}, 1, start, clock);
    }
    if (initial_scan.status == ViolationScanStatus::GeometryTooComplex) {
        return terminal_result(
            SolveStatus::GeometryTooComplex, initial, {}, {}, 1, start, clock);
    }
    if (initial_scan.violations.empty()) {
        return terminal_result(
            SolveStatus::NoViolation, initial, {}, {}, 1, start, clock);
    }

    std::vector<SearchNode> stack;
    stack.push_back({initial, {}});
    std::vector<LayoutHash> visited = {hash_layout(initial)};
    std::vector<CandidateRejection> last_rejections;
    bool reached_state_limit = false;

    while (!stack.empty()) {
        const auto now = clock.now_ms();
        if (elapsed_since(start, now) >= policy.limits.maximumElapsedMs) {
            return terminal_result(SolveStatus::Timeout,
                                   initial,
                                   {},
                                   initial_scan.violations,
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }

        SearchNode node = std::move(stack.back());
        stack.pop_back();
        const auto scan = scan_visibility_violations(node.snapshot, policy.ranking.visibility);
        if (scan.status == ViolationScanStatus::InvalidSnapshot) {
            return terminal_result(SolveStatus::InvalidSnapshot,
                                   initial,
                                   {},
                                   {},
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }
        if (scan.status == ViolationScanStatus::GeometryTooComplex) {
            return terminal_result(SolveStatus::GeometryTooComplex,
                                   initial,
                                   {},
                                   {},
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }
        if (scan.violations.empty()) {
            return terminal_result(SolveStatus::Solved,
                                   node.snapshot,
                                   std::move(node.moves),
                                   {},
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }
        if (node.moves.size() >= policy.limits.maximumMoves) {
            continue;
        }

        const auto& violation = scan.violations.front();
        const auto generated = generate_candidates(node.snapshot,
                                                   violation,
                                                   policy.ranking.visibility,
                                                   policy.limits.maximumCandidatesPerViolation);
        if (generated.status == CandidateGenerationStatus::TooComplex) {
            return terminal_result(SolveStatus::GeometryTooComplex,
                                   initial,
                                   {},
                                   scan.violations,
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }
        if (generated.status != CandidateGenerationStatus::Ok) {
            return terminal_result(SolveStatus::InvalidSnapshot,
                                   initial,
                                   {},
                                   scan.violations,
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }

        auto ranking_policy = policy.ranking;
        ranking_policy.requireStableLayout = false;
        auto ranked = rank_candidates(
            node.snapshot, violation, generated.candidates, ranking_policy);
        if (ranked.status == CandidateRankingStatus::GeometryTooComplex) {
            return terminal_result(SolveStatus::GeometryTooComplex,
                                   initial,
                                   {},
                                   scan.violations,
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }
        if (ranked.status != CandidateRankingStatus::Ok) {
            return terminal_result(SolveStatus::InvalidSnapshot,
                                   initial,
                                   {},
                                   scan.violations,
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }

        last_rejections.clear();
        last_rejections.reserve(ranked.rejected.size());
        for (const auto& rejection : ranked.rejected) {
            if (rejection.originalIndex >= generated.candidates.size()) {
                continue;
            }
            last_rejections.push_back({
                node.snapshot.windows[violation.targetIndex].key,
                generated.candidates[rejection.originalIndex].placementRect,
                rejection.failure,
            });
        }

        std::vector<SearchNode> children;
        for (auto& candidate : ranked.accepted) {
            const auto state_hash = hash_layout(candidate.simulatedSnapshot);
            if (contains_hash(visited, state_hash)) {
                continue;
            }
            if (visited.size() >= policy.limits.maximumStates) {
                reached_state_limit = true;
                break;
            }
            visited.push_back(state_hash);
            SearchNode next;
            next.snapshot = std::move(candidate.simulatedSnapshot);
            next.moves = node.moves;
            next.moves.push_back({
                node.snapshot.windows[violation.targetIndex].key,
                node.snapshot.windows[violation.targetIndex].placementRect,
                candidate.candidate.placementRect,
                candidate.cost,
            });
            children.push_back(std::move(next));
        }
        for (auto child = children.rbegin(); child != children.rend(); ++child) {
            stack.push_back(std::move(*child));
        }
    }

    auto result = terminal_result(reached_state_limit ? SolveStatus::Timeout
                                                      : SolveStatus::Unsatisfiable,
                                  initial,
                                  {},
                                  initial_scan.violations,
                                  static_cast<std::uint32_t>(visited.size()),
                                  start,
                                  clock);
    result.candidateRejections = std::move(last_rejections);
    return result;
}

SolveResult solve_layout_incrementally(const LayoutSnapshot& initial,
                                       const SolverPolicy& policy,
                                       std::size_t active_window_index,
                                       ISolverClock* supplied_clock)
{
    SteadySolverClock default_clock;
    ISolverClock& clock = supplied_clock == nullptr ? static_cast<ISolverClock&>(default_clock)
                                                    : *supplied_clock;
    const auto start = clock.now_ms();
    if (active_window_index >= initial.windows.size() ||
        policy.limits.maximumMoves == 0 || policy.limits.maximumStates == 0 ||
        policy.limits.maximumElapsedMs == 0) {
        return terminal_result(
            SolveStatus::InvalidSnapshot, initial, {}, {}, 0, start, clock);
    }

    const auto initial_scan = scan_visibility_violations(initial, policy.ranking.visibility);
    if (initial_scan.status == ViolationScanStatus::InvalidSnapshot) {
        return terminal_result(
            SolveStatus::InvalidSnapshot, initial, {}, {}, 1, start, clock);
    }
    if (initial_scan.status == ViolationScanStatus::GeometryTooComplex) {
        return terminal_result(
            SolveStatus::GeometryTooComplex, initial, {}, {}, 1, start, clock);
    }
    if (initial_scan.violations.empty()) {
        return terminal_result(
            SolveStatus::NoViolation, initial, {}, {}, 1, start, clock);
    }

    auto current = initial;
    std::vector<MovePlan> moves;
    std::uint32_t states_visited = 1;
    std::vector<LayoutHash> visited = {hash_layout(current)};

    while (moves.size() < policy.limits.maximumMoves) {
        const auto scan = scan_visibility_violations(current, policy.ranking.visibility);
        if (scan.status == ViolationScanStatus::InvalidSnapshot) {
            return terminal_result(SolveStatus::InvalidSnapshot,
                                   initial,
                                   {},
                                   initial_scan.violations,
                                   states_visited,
                                   start,
                                   clock);
        }
        if (scan.status == ViolationScanStatus::GeometryTooComplex) {
            return terminal_result(SolveStatus::GeometryTooComplex,
                                   initial,
                                   {},
                                   initial_scan.violations,
                                   states_visited,
                                   start,
                                   clock);
        }
        if (scan.violations.empty()) {
            return terminal_result(SolveStatus::Solved,
                                   current,
                                   std::move(moves),
                                   {},
                                   states_visited,
                                   start,
                                   clock);
        }

        const auto violation = std::min_element(
            scan.violations.begin(), scan.violations.end(), [&current](const auto& left,
                                                                       const auto& right) {
                return current.windows[left.targetIndex].zIndex <
                    current.windows[right.targetIndex].zIndex;
            });
        if (violation == scan.violations.end() ||
            violation->targetIndex == active_window_index) {
            break;
        }

        auto local = current;
        for (std::size_t index = 0; index < local.windows.size(); ++index) {
            const bool is_target = index == violation->targetIndex;
            local.windows[index].managed = is_target;
            local.windows[index].movable = is_target && current.windows[index].movable;
        }

        auto local_policy = policy;
        local_policy.limits.maximumMoves = 1;
        const auto local_result = solve_layout(local, local_policy, &clock);
        states_visited += local_result.statesVisited;
        if (local_result.status != SolveStatus::Solved || local_result.moves.size() != 1) {
            const auto status = local_result.status == SolveStatus::NoViolation
                ? SolveStatus::Unsatisfiable
                : local_result.status;
            return terminal_result(status,
                                   initial,
                                   {},
                                   initial_scan.violations,
                                   states_visited,
                                   start,
                                   clock);
        }

        const auto target_index = violation->targetIndex;
        current.windows[target_index].placementRect =
            local_result.finalSnapshot.windows[target_index].placementRect;
        current.windows[target_index].visualRect =
            local_result.finalSnapshot.windows[target_index].visualRect;
        moves.push_back(local_result.moves.front());

        const auto state = hash_layout(current);
        if (contains_hash(visited, state) || visited.size() >= policy.limits.maximumStates) {
            return terminal_result(SolveStatus::Timeout,
                                   initial,
                                   {},
                                   initial_scan.violations,
                                   states_visited,
                                   start,
                                   clock);
        }
        visited.push_back(state);
    }

    return terminal_result(SolveStatus::Unsatisfiable,
                           initial,
                           {},
                           initial_scan.violations,
                           states_visited,
                           start,
                           clock);
}

} // namespace stage_manager::solver
