#include "solver/candidate_ranker.h"

#include "geometry/work_area.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <tuple>

namespace stage_manager::solver {
namespace {

std::uint64_t unsigned_distance(std::int64_t left, std::int64_t right) noexcept
{
    return left >= right
        ? static_cast<std::uint64_t>(left) - static_cast<std::uint64_t>(right)
        : static_cast<std::uint64_t>(right) - static_cast<std::uint64_t>(left);
}

std::uint64_t saturating_add(std::uint64_t left, std::uint64_t right) noexcept
{
    return left > std::numeric_limits<std::uint64_t>::max() - right
        ? std::numeric_limits<std::uint64_t>::max()
        : left + right;
}

std::uint64_t point_distance(const geometry::Rect& left, const geometry::Rect& right) noexcept
{
    return saturating_add(unsigned_distance(left.left, right.left),
                          unsigned_distance(left.top, right.top));
}

std::uint64_t boundary_distance(const geometry::Rect& rectangle,
                                const geometry::Rect& work_area) noexcept
{
    const auto horizontal = std::min(unsigned_distance(rectangle.left, work_area.left),
                                     unsigned_distance(rectangle.right, work_area.right));
    const auto vertical = std::min(unsigned_distance(rectangle.top, work_area.top),
                                   unsigned_distance(rectangle.bottom, work_area.bottom));
    return saturating_add(horizontal, vertical);
}

std::uint32_t direction_change_penalty(
    const PlacementCandidate& candidate, std::optional<geometry::Edge> preferred_edge) noexcept
{
    if (!preferred_edge || (candidate.deltaX == 0 && candidate.deltaY == 0)) {
        return 0;
    }
    const bool follows_preference =
        (*preferred_edge == geometry::Edge::Left && candidate.deltaX < 0) ||
        (*preferred_edge == geometry::Edge::Right && candidate.deltaX > 0) ||
        (*preferred_edge == geometry::Edge::Top && candidate.deltaY < 0) ||
        (*preferred_edge == geometry::Edge::Bottom && candidate.deltaY > 0);
    return follows_preference ? 0u : 1u;
}

CandidateCost calculate_cost(const LayoutWindow& target,
                             const PlacementCandidate& candidate,
                             std::optional<geometry::Edge> preferred_edge)
{
    const auto& stable_rect = target.lastStableRect.empty()
        ? target.placementRect
        : target.lastStableRect;
    CandidateCost cost;
    cost.movedWindowCount = candidate.deltaX == 0 && candidate.deltaY == 0 ? 0u : 1u;
    cost.manhattanDistance = saturating_add(unsigned_distance(candidate.deltaX, 0),
                                            unsigned_distance(candidate.deltaY, 0));
    cost.stableDistance = point_distance(candidate.placementRect, stable_rect);
    cost.boundaryDistance = boundary_distance(candidate.placementRect, target.workArea);
    cost.directionChangePenalty = direction_change_penalty(candidate, preferred_edge);
    cost.windowHandle = target.key.hwnd;
    cost.instanceGeneration = target.key.instanceGeneration;
    cost.left = candidate.placementRect.left;
    cost.top = candidate.placementRect.top;
    return cost;
}

bool less_cost(const RankedCandidate& left, const RankedCandidate& right)
{
    const auto& left_cost = left.cost;
    const auto& right_cost = right.cost;
    return std::tie(left_cost.movedWindowCount,
                    left_cost.manhattanDistance,
                    left_cost.stableDistance,
                    left_cost.boundaryDistance,
                    left_cost.directionChangePenalty,
                    left_cost.windowHandle,
                    left_cost.instanceGeneration,
                    left_cost.left,
                    left_cost.top,
                    left.originalIndex) <
        std::tie(right_cost.movedWindowCount,
                 right_cost.manhattanDistance,
                 right_cost.stableDistance,
                 right_cost.boundaryDistance,
                 right_cost.directionChangePenalty,
                 right_cost.windowHandle,
                 right_cost.instanceGeneration,
                 right_cost.left,
                 right_cost.top,
                 right.originalIndex);
}

bool contains_target_violation(std::span<const Violation> violations, std::size_t target_index)
{
    return std::any_of(violations.begin(), violations.end(), [target_index](const auto& violation) {
        return violation.targetIndex == target_index;
    });
}

} // namespace

CandidateRankingResult rank_candidates(const LayoutSnapshot& snapshot,
                                       const Violation& violation,
                                       std::span<const PlacementCandidate> candidates,
                                       const CandidateRankingPolicy& policy)
{
    CandidateRankingResult result;
    if (violation.targetIndex >= snapshot.windows.size() ||
        policy.minimumOnscreenWidth == 0 || policy.minimumOnscreenHeight == 0 ||
        policy.minimumOnscreenWidth >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        policy.minimumOnscreenHeight >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
        (policy.activeWindowIndex && *policy.activeWindowIndex >= snapshot.windows.size())) {
        result.status = CandidateRankingStatus::InvalidInput;
        return result;
    }

    const auto& target = snapshot.windows[violation.targetIndex];
    for (std::size_t candidate_index = 0; candidate_index < candidates.size(); ++candidate_index) {
        const auto& candidate = candidates[candidate_index];
        const auto reject = [&result, candidate_index](HardConstraintFailure failure) {
            result.rejected.push_back({candidate_index, failure});
        };

        if (policy.activeWindowIndex && *policy.activeWindowIndex == violation.targetIndex) {
            reject(HardConstraintFailure::ActiveWindow);
            continue;
        }
        if (!target.managed || !target.movable || !target.visible ||
            !target.currentDesktop || target.monitor == 0) {
            reject(HardConstraintFailure::UnavailableWindow);
            continue;
        }
        if (candidate.placementRect.width() != target.placementRect.width() ||
            candidate.placementRect.height() != target.placementRect.height()) {
            reject(HardConstraintFailure::SizeChanged);
            continue;
        }
        const auto expected_placement = target.placementRect.translated(
            candidate.deltaX, candidate.deltaY);
        const auto expected_visual = target.visualRect.translated(candidate.deltaX, candidate.deltaY);
        if (!expected_placement || !expected_visual ||
            *expected_placement != candidate.placementRect) {
            reject(HardConstraintFailure::InconsistentDelta);
            continue;
        }
        if (!geometry::preserves_minimum_onscreen(
                candidate.placementRect,
                target.workArea,
                static_cast<std::int64_t>(policy.minimumOnscreenWidth),
                static_cast<std::int64_t>(policy.minimumOnscreenHeight))) {
            reject(HardConstraintFailure::OutsideWorkArea);
            continue;
        }

        LayoutSnapshot simulated = snapshot;
        simulated.windows[violation.targetIndex].placementRect = candidate.placementRect;
        simulated.windows[violation.targetIndex].visualRect = *expected_visual;
        const auto scan = scan_visibility_violations(simulated, policy.visibility);
        if (scan.status == ViolationScanStatus::GeometryTooComplex) {
            result.status = CandidateRankingStatus::GeometryTooComplex;
            result.accepted.clear();
            result.rejected.clear();
            return result;
        }
        if (scan.status != ViolationScanStatus::Ok) {
            result.status = CandidateRankingStatus::InvalidInput;
            result.accepted.clear();
            result.rejected.clear();
            return result;
        }
        if (contains_target_violation(scan.violations, violation.targetIndex)) {
            reject(HardConstraintFailure::TargetStillViolated);
            continue;
        }
        if (policy.requireStableLayout && !scan.violations.empty()) {
            reject(HardConstraintFailure::OtherManagedWindowViolated);
            continue;
        }

        RankedCandidate ranked;
        ranked.originalIndex = candidate_index;
        ranked.candidate = candidate;
        ranked.cost = calculate_cost(target, candidate, policy.preferredEdge);
        ranked.simulatedSnapshot = std::move(simulated);
        ranked.remainingViolations = scan.violations;
        result.accepted.push_back(std::move(ranked));
    }

    std::sort(result.accepted.begin(), result.accepted.end(), &less_cost);
    return result;
}

} // namespace stage_manager::solver
