#include "solver/layout_solver.h"

#include "solver/candidate_generator.h"
#include "solver/candidate_ranker.h"
#include "geometry/work_area.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <optional>
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

struct VisibilityEnhancementResult {
    LayoutSnapshot snapshot;
    std::vector<MovePlan> moves;
    std::uint32_t statesVisited = 0;
};

constexpr std::size_t kMaximumSearchBranchesPerState = 4;
constexpr std::uint64_t kMinimumVisibilityGainDivisor = 20;
constexpr std::uint64_t kRelaxedTitleBarTravelMultiplier = 2;
constexpr std::int64_t kStaircaseTopStepExtraDivisor = 2;

enum class StaircaseDirection : std::uint8_t {
    TopLeft,
    TopRight,
    Left,
    Right,
    Bottom,
};

struct StaircaseAttempt {
    LayoutSnapshot snapshot;
    std::vector<MovePlan> moves;
    std::vector<Violation> violations;
    ViolationScanStatus scanStatus = ViolationScanStatus::Ok;
    std::uint32_t statesVisited = 0;
    bool complete = false;
};

bool contains_hash(std::span<const LayoutHash> hashes, const LayoutHash& hash)
{
    return std::find(hashes.begin(), hashes.end(), hash) != hashes.end();
}

std::uint64_t elapsed_since(std::uint64_t start, std::uint64_t now) noexcept
{
    return now >= start ? now - start : 0;
}

std::optional<std::int64_t> checked_add_coordinate(std::int64_t left,
                                                   std::int64_t right) noexcept
{
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return std::nullopt;
    }
    return left + right;
}

std::optional<std::int64_t> checked_subtract_coordinate(std::int64_t left,
                                                        std::int64_t right) noexcept
{
    if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right) ||
        (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)) {
        return std::nullopt;
    }
    return left - right;
}

PlacementDirectionRank direction_rank(StaircaseDirection direction) noexcept
{
    switch (direction) {
    case StaircaseDirection::TopLeft:
        return PlacementDirectionRank::TopLeft;
    case StaircaseDirection::TopRight:
        return PlacementDirectionRank::TopRight;
    case StaircaseDirection::Left:
        return PlacementDirectionRank::Left;
    case StaircaseDirection::Right:
        return PlacementDirectionRank::Right;
    case StaircaseDirection::Bottom:
        return PlacementDirectionRank::Bottom;
    }
    return PlacementDirectionRank::Stationary;
}

std::optional<std::pair<geometry::Rect, geometry::Rect>> staircase_placement(
    const LayoutWindow& target,
    const geometry::Rect& anchor,
    const VisibilityRequirements& requirements,
    StaircaseDirection direction)
{
    const auto affordances = resolve_edge_affordances(target, requirements);
    if (std::any_of(affordances.begin(), affordances.end(), [](const auto& item) {
            return item.depth == 0 ||
                item.depth > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max());
        })) {
        return std::nullopt;
    }
    const auto left_depth = static_cast<std::int64_t>(affordances[0].depth);
    const auto right_depth = static_cast<std::int64_t>(affordances[1].depth);
    const auto top_depth = static_cast<std::int64_t>(affordances[2].depth);
    const auto bottom_depth = static_cast<std::int64_t>(affordances[3].depth);
    const auto target_width = static_cast<std::int64_t>(target.visualRect.width());
    const auto target_height = static_cast<std::int64_t>(target.visualRect.height());
    const auto top_step = checked_add_coordinate(
        top_depth,
        top_depth / kStaircaseTopStepExtraDivisor +
            (top_depth % kStaircaseTopStepExtraDivisor != 0 ? 1 : 0));
    if (!top_step) {
        return std::nullopt;
    }

    std::optional<std::int64_t> desired_left;
    std::optional<std::int64_t> desired_top;
    switch (direction) {
    case StaircaseDirection::TopLeft:
        desired_left = checked_subtract_coordinate(anchor.left, left_depth);
        desired_top = checked_subtract_coordinate(anchor.top, *top_step);
        break;
    case StaircaseDirection::TopRight: {
        const auto desired_right = checked_add_coordinate(anchor.right, right_depth);
        if (desired_right) {
            desired_left = checked_subtract_coordinate(*desired_right, target_width);
        }
        desired_top = checked_subtract_coordinate(anchor.top, *top_step);
        break;
    }
    case StaircaseDirection::Left:
        desired_left = checked_subtract_coordinate(anchor.left, left_depth);
        desired_top = anchor.top;
        break;
    case StaircaseDirection::Right: {
        const auto desired_right = checked_add_coordinate(anchor.right, right_depth);
        if (desired_right) {
            desired_left = checked_subtract_coordinate(*desired_right, target_width);
        }
        desired_top = anchor.top;
        break;
    }
    case StaircaseDirection::Bottom: {
        const auto desired_bottom = checked_add_coordinate(anchor.bottom, bottom_depth);
        desired_left = anchor.left;
        if (desired_bottom) {
            desired_top = checked_subtract_coordinate(*desired_bottom, target_height);
        }
        break;
    }
    }
    if (!desired_left || !desired_top) {
        return std::nullopt;
    }
    const auto delta_x = checked_subtract_coordinate(*desired_left, target.visualRect.left);
    const auto delta_y = checked_subtract_coordinate(*desired_top, target.visualRect.top);
    if (!delta_x || !delta_y) {
        return std::nullopt;
    }
    const auto visual = target.visualRect.translated(*delta_x, *delta_y);
    const auto placement = target.placementRect.translated(*delta_x, *delta_y);
    if (!visual || !placement ||
        !geometry::fully_within_work_area(*placement, target.workArea)) {
        return std::nullopt;
    }
    return std::pair{*placement, *visual};
}

std::uint64_t rectangle_area(const geometry::Rect& rectangle) noexcept
{
    const auto width = rectangle.width();
    const auto height = rectangle.height();
    return height != 0 && width > std::numeric_limits<std::uint64_t>::max() / height
        ? std::numeric_limits<std::uint64_t>::max()
        : width * height;
}

bool relevant_blocker(const LayoutWindow& target, const LayoutWindow& blocker) noexcept
{
    return blocker.visible && blocker.blocksVisibility && blocker.currentDesktop &&
        blocker.zIndex >= 0 && blocker.zIndex < target.zIndex &&
        (target.monitor == 0 || blocker.monitor == target.monitor) &&
        blocker.visualRect.intersects(target.visualRect);
}

bool target_priority_less(const LayoutSnapshot& snapshot,
                          std::size_t left,
                          std::size_t right)
{
    if (snapshot.windows[left].zIndex != snapshot.windows[right].zIndex) {
        return snapshot.windows[left].zIndex < snapshot.windows[right].zIndex;
    }
    return left < right;
}

std::uint64_t coordinate_distance(std::int64_t left, std::int64_t right) noexcept
{
    return left >= right
        ? static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right)
        : static_cast<std::uint64_t>(right) - static_cast<std::uint64_t>(left);
}

std::uint64_t saturating_distance_sum(std::uint64_t left,
                                      std::uint64_t right) noexcept
{
    return left > std::numeric_limits<std::uint64_t>::max() - right
        ? std::numeric_limits<std::uint64_t>::max()
        : left + right;
}

std::vector<MovePlan> staircase_moves(const LayoutSnapshot& initial,
                                      const LayoutSnapshot& result,
                                      std::span<const std::size_t> ordered_targets,
                                      const SolverPolicy& policy,
                                      std::span<const StaircaseDirection> directions,
                                      std::size_t remaining_violations)
{
    std::vector<MovePlan> moves;
    for (const auto target_index : ordered_targets) {
        if (initial.windows[target_index].placementRect ==
            result.windows[target_index].placementRect) {
            continue;
        }
        MovePlan move;
        move.window = initial.windows[target_index].key;
        move.from = initial.windows[target_index].placementRect;
        move.to = result.windows[target_index].placementRect;
        move.cost.placementDirection = direction_rank(directions[target_index]);
        move.cost.remainingViolationCount = remaining_violations >
                std::numeric_limits<std::uint32_t>::max()
            ? std::numeric_limits<std::uint32_t>::max()
            : static_cast<std::uint32_t>(remaining_violations);
        const auto visibility = analyze_window_visibility_with_status(
            result, target_index, policy.ranking.visibility);
        if (visibility.status == ViolationScanStatus::Ok && visibility.visibility) {
            const auto preference = visibility_preference_rank(
                result, target_index, policy.ranking.visibility);
            if (preference) {
                move.cost.visibilityPreference = *preference;
            }
            const auto total_area = rectangle_area(result.windows[target_index].visualRect);
            move.cost.hiddenArea = total_area -
                std::min(total_area, visibility.exposedArea);
            move.cost.incompleteExposurePenalty = move.cost.hiddenArea == 0 ? 0u : 1u;
        }
        const auto& target = result.windows[target_index];
        move.cost.workAreaBoundaryPenalty =
            (target.placementRect.left <= target.workArea.left ? 1u : 0u) +
            (target.placementRect.right >= target.workArea.right ? 1u : 0u) +
            (target.placementRect.top <= target.workArea.top ? 1u : 0u) +
            (target.placementRect.bottom >= target.workArea.bottom ? 1u : 0u);
        move.cost.movedWindowCount = 1;
        move.cost.manhattanDistance = saturating_distance_sum(
            coordinate_distance(move.from.left, move.to.left),
            coordinate_distance(move.from.top, move.to.top));
        move.cost.stableDistance = saturating_distance_sum(
            coordinate_distance(target.lastStableRect.left, move.to.left),
            coordinate_distance(target.lastStableRect.top, move.to.top));
        move.cost.windowHandle = target.key.hwnd;
        move.cost.instanceGeneration = target.key.instanceGeneration;
        move.cost.left = move.to.left;
        move.cost.top = move.to.top;
        moves.push_back(std::move(move));
    }
    return moves;
}

StaircaseAttempt attempt_staircase(const LayoutSnapshot& initial,
                                   const SolverPolicy& policy,
                                   std::size_t active_window_index,
                                   std::span<const std::size_t> ordered_targets,
                                   std::span<const Violation> initial_violations,
                                   std::span<const StaircaseDirection> directions,
                                   std::uint64_t start,
                                   ISolverClock& clock,
                                   std::uint32_t available_states,
                                   bool select_all_targets)
{
    StaircaseAttempt attempt;
    attempt.snapshot = initial;
    std::vector<bool> selected(initial.windows.size(), false);
    for (const auto& violation : initial_violations) {
        if (violation.targetIndex < selected.size()) {
            selected[violation.targetIndex] = true;
        }
    }
    if (select_all_targets) {
        for (const auto target_index : ordered_targets) {
            selected[target_index] = true;
        }
    }

    for (std::size_t closure_round = 0;
         closure_round <= ordered_targets.size(); ++closure_round) {
        if (attempt.statesVisited >= available_states ||
            elapsed_since(start, clock.now_ms()) >= policy.limits.maximumElapsedMs) {
            return attempt;
        }
        ++attempt.statesVisited;
        auto candidate = initial;
        auto anchor = candidate.windows[active_window_index].visualRect;
        auto occupied = anchor;
        std::vector<StaircaseDirection> placement_directions(
            initial.windows.size(), directions.front());
        std::size_t direction_index = 0;
        bool placement_failed = false;
        std::size_t planned_moves = 0;
        for (const auto target_index : ordered_targets) {
            if (!selected[target_index]) {
                continue;
            }
            auto& target = candidate.windows[target_index];
            if (!target.movable) {
                placement_failed = true;
                break;
            }
            auto placement_direction = directions[direction_index];
            auto placement = staircase_placement(
                target, anchor, policy.ranking.visibility, placement_direction);
            while (!placement && ++direction_index < directions.size()) {
                anchor = candidate.windows[active_window_index].visualRect;
                placement_direction = directions[direction_index];
                if (placement_direction == StaircaseDirection::TopRight) {
                    const auto affordances = resolve_edge_affordances(
                        target, policy.ranking.visibility);
                    if (affordances[2].length > static_cast<std::uint64_t>(
                            std::numeric_limits<std::int64_t>::max())) {
                        placement_failed = true;
                        break;
                    }
                    const auto branch_right = checked_add_coordinate(
                        occupied.right,
                        static_cast<std::int64_t>(affordances[2].length));
                    if (!branch_right) {
                        placement_failed = true;
                        break;
                    }
                    anchor.right = *branch_right;
                } else if (placement_direction == StaircaseDirection::Left) {
                    anchor.left = occupied.left;
                } else if (placement_direction == StaircaseDirection::Right) {
                    anchor.right = occupied.right;
                } else if (placement_direction == StaircaseDirection::Bottom) {
                    anchor.bottom = occupied.bottom;
                }
                placement = staircase_placement(
                    target, anchor, policy.ranking.visibility, placement_direction);
            }
            if (placement_failed || !placement) {
                placement_failed = true;
                break;
            }
            placement_directions[target_index] = placement_direction;
            const bool changes_position = placement->first !=
                initial.windows[target_index].placementRect;
            if (changes_position && planned_moves >= policy.limits.maximumMoves) {
                placement_failed = true;
                break;
            }
            target.placementRect = placement->first;
            target.visualRect = placement->second;
            anchor = target.visualRect;
            occupied.left = std::min(occupied.left, target.visualRect.left);
            occupied.top = std::min(occupied.top, target.visualRect.top);
            occupied.right = std::max(occupied.right, target.visualRect.right);
            occupied.bottom = std::max(occupied.bottom, target.visualRect.bottom);
            if (changes_position) {
                ++planned_moves;
            }
        }

        const auto scan = scan_visibility_violations(
            candidate, policy.ranking.visibility);
        attempt.scanStatus = scan.status;
        if (scan.status != ViolationScanStatus::Ok) {
            return attempt;
        }
        attempt.snapshot = std::move(candidate);
        attempt.violations = scan.violations;
        attempt.moves = staircase_moves(initial,
                                         attempt.snapshot,
                                         ordered_targets,
                                         policy,
                                         placement_directions,
                                         attempt.violations.size());
        if (!placement_failed && attempt.violations.empty()) {
            attempt.complete = true;
            return attempt;
        }

        bool expanded = false;
        for (const auto& violation : attempt.violations) {
            if (violation.targetIndex != active_window_index &&
                violation.targetIndex < selected.size() &&
                initial.windows[violation.targetIndex].managed &&
                initial.windows[violation.targetIndex].movable &&
                !selected[violation.targetIndex]) {
                selected[violation.targetIndex] = true;
                expanded = true;
            }
            for (const auto blocker_index : violation.blockerIndices) {
                if (blocker_index != active_window_index && blocker_index < selected.size() &&
                    initial.windows[blocker_index].managed &&
                    initial.windows[blocker_index].movable && !selected[blocker_index]) {
                    selected[blocker_index] = true;
                    expanded = true;
                }
            }
        }
        if (placement_failed || !expanded) {
            if (!attempt.violations.empty()) {
                const auto first_failed = std::min_element(
                    attempt.violations.begin(),
                    attempt.violations.end(),
                    [&attempt](const auto& left, const auto& right) {
                        return target_priority_less(
                            attempt.snapshot, left.targetIndex, right.targetIndex);
                    });
                const auto failed_z = attempt.snapshot.windows[
                    first_failed->targetIndex].zIndex;
                for (const auto target_index : ordered_targets) {
                    if (attempt.snapshot.windows[target_index].zIndex >= failed_z) {
                        attempt.snapshot.windows[target_index].placementRect =
                            initial.windows[target_index].placementRect;
                        attempt.snapshot.windows[target_index].visualRect =
                            initial.windows[target_index].visualRect;
                    }
                }
                const auto safe_scan = scan_visibility_violations(
                    attempt.snapshot, policy.ranking.visibility);
                attempt.scanStatus = safe_scan.status;
                attempt.violations = safe_scan.violations;
                attempt.moves = staircase_moves(initial,
                                                 attempt.snapshot,
                                                 ordered_targets,
                                                 policy,
                                                 placement_directions,
                                                 attempt.violations.size());
            }
            return attempt;
        }
    }
    return attempt;
}

auto select_priority_violation(const LayoutSnapshot& snapshot,
                               const std::vector<Violation>& violations)
{
    return std::min_element(
        violations.begin(), violations.end(), [&snapshot](const auto& left, const auto& right) {
            return target_priority_less(snapshot, left.targetIndex, right.targetIndex);
        });
}

void merge_move(std::vector<MovePlan>& moves, MovePlan move)
{
    const auto first = std::find_if(moves.begin(), moves.end(), [&move](const auto& item) {
        return item.window == move.window;
    });
    if (first == moves.end()) {
        moves.push_back(std::move(move));
        return;
    }

    const auto first_index = static_cast<std::size_t>(first - moves.begin());
    moves[first_index].to = move.to;
    moves[first_index].cost = move.cost;
    moves.erase(std::remove_if(moves.begin() + static_cast<std::ptrdiff_t>(first_index + 1),
                               moves.end(),
                               [&move](const auto& item) {
                                   return item.window == move.window;
                               }),
                moves.end());
    if (moves[first_index].from == moves[first_index].to) {
        moves.erase(moves.begin() + static_cast<std::ptrdiff_t>(first_index));
    }
}

VisibilityEnhancementResult enhance_visibility(
    LayoutSnapshot solved,
    std::vector<MovePlan> moves,
    const SolverPolicy& policy,
    std::uint64_t start,
    ISolverClock& clock,
    std::uint32_t states_visited,
    bool revisit_moved_windows = false,
    bool use_relaxed_title_bar_limit = false)
{
    VisibilityEnhancementResult result{
        std::move(solved), std::move(moves), states_visited};
    if (!policy.ranking.activeWindowIndex ||
        *policy.ranking.activeWindowIndex >= result.snapshot.windows.size()) {
        return result;
    }

    std::vector<std::size_t> targets;
    for (std::size_t index = 0; index < result.snapshot.windows.size(); ++index) {
        const auto& window = result.snapshot.windows[index];
        if (index != *policy.ranking.activeWindowIndex && window.managed &&
            window.movable && window.visible && window.currentDesktop) {
            targets.push_back(index);
        }
    }
    std::stable_sort(targets.begin(), targets.end(), [&result](std::size_t left,
                                                               std::size_t right) {
        return target_priority_less(result.snapshot, left, right);
    });

    for (const auto target_index : targets) {
        if (elapsed_since(start, clock.now_ms()) >= policy.limits.maximumElapsedMs ||
            result.statesVisited >= policy.limits.maximumStates) {
            break;
        }
        const auto already_moved = std::any_of(
            result.moves.begin(), result.moves.end(), [&result, target_index](const auto& move) {
                return move.window == result.snapshot.windows[target_index].key;
            });
        if (already_moved && !revisit_moved_windows) {
            continue;
        }

        const auto current_visibility = analyze_window_visibility_with_status(
            result.snapshot, target_index, policy.ranking.visibility);
        if (current_visibility.status != ViolationScanStatus::Ok ||
            !current_visibility.visibility) {
            break;
        }
        const auto total_area = rectangle_area(
            result.snapshot.windows[target_index].visualRect);
        const auto current_hidden = total_area -
            std::min(total_area, current_visibility.exposedArea);
        if (current_hidden == 0) {
            continue;
        }

        Violation target;
        target.targetIndex = target_index;
        for (std::size_t index = 0; index < result.snapshot.windows.size(); ++index) {
            if (index != target_index && relevant_blocker(
                    result.snapshot.windows[target_index], result.snapshot.windows[index])) {
                target.blockerIndices.push_back(index);
            }
        }

        const auto generated = generate_candidates(
            result.snapshot,
            target,
            policy.ranking.visibility,
            policy.limits.maximumCandidatesPerViolation);
        if (generated.status != CandidateGenerationStatus::Ok) {
            continue;
        }
        auto ranking = policy.ranking;
        ranking.requireStableLayout = true;
        ranking.collectRemainingViolations = false;
        if (use_relaxed_title_bar_limit) {
            const auto title_bar_height = static_cast<std::uint64_t>(
                result.snapshot.windows[target_index].titleBarHeight);
            ranking.maximumUpwardTravel =
                title_bar_height > std::numeric_limits<std::uint64_t>::max() /
                        kRelaxedTitleBarTravelMultiplier
                ? std::numeric_limits<std::uint64_t>::max()
                : title_bar_height * kRelaxedTitleBarTravelMultiplier;
        }
        const auto ranked = rank_candidates(
            result.snapshot, target, generated.candidates, ranking);
        if (ranked.status != CandidateRankingStatus::Ok || ranked.accepted.empty()) {
            continue;
        }
        const auto best_match = use_relaxed_title_bar_limit
            ? std::find_if(ranked.accepted.begin(), ranked.accepted.end(), [](const auto& item) {
                  return item.cost.workAreaBoundaryPenalty == 0;
              })
            : ranked.accepted.begin();
        if (best_match == ranked.accepted.end()) {
            continue;
        }
        const auto& best = *best_match;
        const auto minimum_gain = total_area / kMinimumVisibilityGainDivisor +
            (total_area % kMinimumVisibilityGainDivisor == 0 ? 0 : 1);
        const auto gain = best.cost.hiddenArea < current_hidden
            ? current_hidden - best.cost.hiddenArea
            : 0;
        if (gain < minimum_gain ||
            best.candidate.placementRect ==
                result.snapshot.windows[target_index].placementRect) {
            continue;
        }

        if (!already_moved && result.moves.size() >= policy.limits.maximumMoves) {
            continue;
        }
        MovePlan move;
        move.window = result.snapshot.windows[target_index].key;
        move.from = result.snapshot.windows[target_index].placementRect;
        move.to = best.candidate.placementRect;
        move.cost = best.cost;
        result.snapshot = best.simulatedSnapshot;
        merge_move(result.moves, std::move(move));
        ++result.statesVisited;
    }
    return result;
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

SolveResult best_effort_result(SolveStatus failure_status,
                               const LayoutSnapshot& initial,
                               const LayoutSnapshot& current,
                               std::vector<MovePlan> moves,
                               const std::vector<Violation>& initial_violations,
                               const VisibilityRequirements& requirements,
                               std::uint32_t states_visited,
                               std::uint64_t start,
                               ISolverClock& clock)
{
    if (moves.empty()) {
        return terminal_result(failure_status,
                               initial,
                               {},
                               initial_violations,
                               states_visited,
                               start,
                               clock);
    }

    const auto scan = scan_visibility_violations(current, requirements);
    if (scan.status == ViolationScanStatus::InvalidSnapshot) {
        return terminal_result(SolveStatus::InvalidSnapshot,
                               initial,
                               {},
                               initial_violations,
                               states_visited,
                               start,
                               clock);
    }
    if (scan.status == ViolationScanStatus::GeometryTooComplex) {
        return terminal_result(SolveStatus::GeometryTooComplex,
                               initial,
                               {},
                               initial_violations,
                               states_visited,
                               start,
                               clock);
    }
    return terminal_result(scan.violations.empty() ? SolveStatus::Solved
                                                   : SolveStatus::PartiallySolved,
                           current,
                           std::move(moves),
                           scan.violations,
                           states_visited,
                           start,
                           clock);
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
        auto enhanced = enhance_visibility(
            initial, {}, policy, start, clock, 1);
        const auto status = enhanced.moves.empty()
            ? SolveStatus::NoViolation
            : SolveStatus::Solved;
        return terminal_result(
            status,
            enhanced.snapshot,
            std::move(enhanced.moves),
            {},
            enhanced.statesVisited,
            start,
            clock);
    }

    std::vector<SearchNode> stack;
    stack.push_back({initial, {}});
    std::vector<LayoutHash> visited = {hash_layout(initial)};
    std::vector<CandidateRejection> last_rejections;
    bool reached_state_limit = false;
    bool candidate_space_truncated = false;

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
            auto enhanced = enhance_visibility(node.snapshot,
                                                std::move(node.moves),
                                                policy,
                                                start,
                                                clock,
                                                static_cast<std::uint32_t>(visited.size()));
            return terminal_result(SolveStatus::Solved,
                                   enhanced.snapshot,
                                   std::move(enhanced.moves),
                                   {},
                                   enhanced.statesVisited,
                                   start,
                                   clock);
        }
        if (node.moves.size() >= policy.limits.maximumMoves) {
            continue;
        }

        const auto selected = select_priority_violation(node.snapshot, scan.violations);
        if (selected == scan.violations.end()) {
            return terminal_result(SolveStatus::InvalidSnapshot,
                                   initial,
                                   {},
                                   {},
                                   static_cast<std::uint32_t>(visited.size()),
                                   start,
                                   clock);
        }
        const auto& violation = *selected;
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
            if (generated.status == CandidateGenerationStatus::Truncated) {
                candidate_space_truncated = true;
            } else {
                return terminal_result(SolveStatus::InvalidSnapshot,
                                       initial,
                                       {},
                                       scan.violations,
                                       static_cast<std::uint32_t>(visited.size()),
                                       start,
                                       clock);
            }
        }
        auto ranking_policy = policy.ranking;
        ranking_policy.requireStableLayout = false;
        // The current Z-order layer is fixed without asking lower windows to
        // vote on its position. Any lower window obscured by this move is
        // repaired when its own layer is processed.
        ranking_policy.collectRemainingViolations = false;
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
            if (children.size() >= kMaximumSearchBranchesPerState) {
                break;
            }
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

    auto result = terminal_result(reached_state_limit || candidate_space_truncated
                                      ? SolveStatus::Timeout
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

        const auto violation = select_priority_violation(current, scan.violations);
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
            return best_effort_result(status,
                                      initial,
                                      current,
                                      std::move(moves),
                                      initial_scan.violations,
                                      policy.ranking.visibility,
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
            return best_effort_result(SolveStatus::Timeout,
                                      initial,
                                      current,
                                      std::move(moves),
                                      initial_scan.violations,
                                      policy.ranking.visibility,
                                      states_visited,
                                      start,
                                      clock);
        }
        visited.push_back(state);
    }

    return best_effort_result(SolveStatus::Unsatisfiable,
                              initial,
                              current,
                              std::move(moves),
                              initial_scan.violations,
                              policy.ranking.visibility,
                              states_visited,
                              start,
                              clock);
}

SolveResult solve_layout_prioritized(const LayoutSnapshot& initial,
                                     const SolverPolicy& policy,
                                     std::size_t active_window_index,
                                     ISolverClock* supplied_clock)
{
    if (policy.ranking.visibility.goal == VisibilityGoal::TitleBarLeftHalf) {
        return solve_title_bar_layout(initial, policy, active_window_index, supplied_clock);
    }
    SteadySolverClock default_clock;
    ISolverClock& clock = supplied_clock == nullptr ? static_cast<ISolverClock&>(default_clock)
                                                    : *supplied_clock;
    const auto start = clock.now_ms();
    if (active_window_index >= initial.windows.size() ||
        policy.limits.maximumMoves == 0 || policy.limits.maximumStates == 0 ||
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
    const bool force_preferred_staircase = policy.forcePreferredStaircase &&
        policy.ranking.visibility.goal == VisibilityGoal::AnyRecognizableEdge;
    if (initial_scan.violations.empty() && !force_preferred_staircase) {
        return terminal_result(
            SolveStatus::NoViolation, initial, {}, {}, 1, start, clock);
    }

    std::vector<std::size_t> targets;
    targets.reserve(initial.windows.size());
    for (std::size_t index = 0; index < initial.windows.size(); ++index) {
        const auto& window = initial.windows[index];
        if (index != active_window_index && window.managed && window.visible &&
            window.currentDesktop) {
            targets.push_back(index);
        }
    }
    std::stable_sort(targets.begin(), targets.end(), [&initial](std::size_t left,
                                                                std::size_t right) {
        return target_priority_less(initial, left, right);
    });

    const std::array top_directions = {
        StaircaseDirection::TopLeft,
        StaircaseDirection::TopRight,
    };
    const std::array all_directions = {
        StaircaseDirection::TopLeft,
        StaircaseDirection::TopRight,
        StaircaseDirection::Left,
        StaircaseDirection::Right,
        StaircaseDirection::Bottom,
    };
    const auto directions = policy.ranking.visibility.goal == VisibilityGoal::TopAndSide
        ? std::span<const StaircaseDirection>{top_directions}
        : std::span<const StaircaseDirection>{all_directions};

    const auto initial_priority = select_priority_violation(
        initial, initial_scan.violations);
    const auto improves_priority = [&](const StaircaseAttempt& attempt) {
        if (attempt.moves.empty()) {
            return false;
        }
        const bool highest_initial_violation_resolved =
            initial_priority != initial_scan.violations.end() &&
            std::none_of(attempt.violations.begin(),
                         attempt.violations.end(),
                         [initial_priority](const auto& violation) {
                             return violation.targetIndex ==
                                 initial_priority->targetIndex;
                         });
        return highest_initial_violation_resolved ||
            attempt.violations.size() < initial_scan.violations.size();
    };
    std::uint32_t states_visited = 1;
    if (states_visited >= policy.limits.maximumStates ||
        elapsed_since(start, clock.now_ms()) >= policy.limits.maximumElapsedMs) {
        return terminal_result(SolveStatus::Timeout,
                               initial,
                               {},
                               initial_scan.violations,
                               states_visited,
                               start,
                               clock);
    }
    auto attempt = attempt_staircase(initial,
                                     policy,
                                     active_window_index,
                                     targets,
                                     initial_scan.violations,
                                     directions,
                                     start,
                                     clock,
                                     policy.limits.maximumStates - states_visited,
                                     force_preferred_staircase);
    states_visited += attempt.statesVisited;
    if (attempt.scanStatus == ViolationScanStatus::InvalidSnapshot) {
        return terminal_result(SolveStatus::InvalidSnapshot,
                               initial,
                               {},
                               {},
                               states_visited,
                               start,
                               clock);
    }
    if (attempt.scanStatus == ViolationScanStatus::GeometryTooComplex) {
        return terminal_result(SolveStatus::GeometryTooComplex,
                               initial,
                               {},
                               {},
                               states_visited,
                               start,
                               clock);
    }
    if (attempt.complete) {
        return terminal_result(SolveStatus::Solved,
                               attempt.snapshot,
                               std::move(attempt.moves),
                               {},
                               states_visited,
                               start,
                               clock);
    }
    if (improves_priority(attempt)) {
        return terminal_result(SolveStatus::PartiallySolved,
                               attempt.snapshot,
                               std::move(attempt.moves),
                               std::move(attempt.violations),
                               states_visited,
                               start,
                               clock);
    }
    const auto timed_out = states_visited >= policy.limits.maximumStates ||
        elapsed_since(start, clock.now_ms()) >= policy.limits.maximumElapsedMs;
    return terminal_result(timed_out ? SolveStatus::Timeout
                                     : SolveStatus::Unsatisfiable,
                           initial,
                           {},
                           initial_scan.violations,
                           states_visited,
                           start,
                           clock);
}

} // namespace stage_manager::solver
