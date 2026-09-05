#include "solver/candidate_ranker.h"

#include "geometry/work_area.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
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

std::uint64_t center_distance(const geometry::Rect& rectangle,
                              const geometry::Rect& work_area) noexcept
{
    const auto rectangle_center_x = std::midpoint(rectangle.left, rectangle.right);
    const auto rectangle_center_y = std::midpoint(rectangle.top, rectangle.bottom);
    const auto work_area_center_x = std::midpoint(work_area.left, work_area.right);
    const auto work_area_center_y = std::midpoint(work_area.top, work_area.bottom);
    const auto work_width = static_cast<std::uint64_t>(work_area.width());
    const auto work_height = static_cast<std::uint64_t>(work_area.height());
    if (work_width == 0 || work_height == 0) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    const auto reference_extent = std::min(work_width, work_height);
    const auto scale_axis = [reference_extent](std::uint64_t distance,
                                                std::uint64_t axis_extent) {
        if (distance == 0 || reference_extent == axis_extent) {
            return distance;
        }
        if (distance <= std::numeric_limits<std::uint64_t>::max() / reference_extent) {
            return distance * reference_extent / axis_extent;
        }
        const auto scaled = static_cast<long double>(distance) *
            static_cast<long double>(reference_extent) /
            static_cast<long double>(axis_extent);
        return scaled >= static_cast<long double>(
                             std::numeric_limits<std::uint64_t>::max())
            ? std::numeric_limits<std::uint64_t>::max()
            : static_cast<std::uint64_t>(scaled);
    };
    return saturating_add(
        scale_axis(unsigned_distance(rectangle_center_x, work_area_center_x), work_width),
        scale_axis(unsigned_distance(rectangle_center_y, work_area_center_y), work_height));
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
                             VisibilityPreferenceRank visibility_preference,
                             std::optional<geometry::Edge> preferred_edge,
                             std::uint32_t channel_imbalance,
                             std::uint32_t channel_alternation_penalty)
{
    const auto& stable_rect = target.lastStableRect.empty()
        ? target.placementRect
        : target.lastStableRect;
    CandidateCost cost;
    cost.visibilityPreference = visibility_preference;
    cost.channelImbalance = channel_imbalance;
    cost.channelAlternationPenalty = channel_alternation_penalty;
    cost.centerDistance = center_distance(candidate.placementRect, target.workArea);
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
    const auto visibility_tier = [](VisibilityPreferenceRank rank) {
        return rank == VisibilityPreferenceRank::TopRight
            ? VisibilityPreferenceRank::TopLeft
            : rank;
    };
    return std::make_tuple(visibility_tier(left_cost.visibilityPreference),
                    left_cost.channelImbalance,
                    left_cost.channelAlternationPenalty,
                    left_cost.centerDistance,
                    left_cost.movedWindowCount,
                    left_cost.manhattanDistance,
                    left_cost.stableDistance,
                    left_cost.boundaryDistance,
                    left_cost.directionChangePenalty,
                    left_cost.windowHandle,
                    left_cost.instanceGeneration,
                    left_cost.left,
                    left_cost.top,
                    left.originalIndex) <
        std::make_tuple(visibility_tier(right_cost.visibilityPreference),
                 right_cost.channelImbalance,
                 right_cost.channelAlternationPenalty,
                 right_cost.centerDistance,
                 right_cost.movedWindowCount,
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

std::pair<std::uint32_t, std::uint32_t> channel_loads(
    const LayoutSnapshot& snapshot,
    const VisibilityRequirements& requirements,
    std::size_t target_index,
    std::optional<std::size_t> active_index)
{
    std::uint32_t left = 0;
    std::uint32_t right = 0;
    for (std::size_t index = 0; index < snapshot.windows.size(); ++index) {
        if (index == target_index || (active_index && index == *active_index) ||
            !snapshot.windows[index].managed ||
            snapshot.windows[index].zIndex >= snapshot.windows[target_index].zIndex) {
            continue;
        }
        const auto visibility = analyze_window_visibility(snapshot, index, requirements);
        if (!visibility) {
            continue;
        }
        if (visibility->topLeft && !visibility->topRight) {
            ++left;
        } else if (visibility->topRight && !visibility->topLeft) {
            ++right;
        }
    }
    return {left, right};
}

} // namespace

std::optional<VisibilityPreferenceRank> visibility_preference_rank(
    const LayoutSnapshot& snapshot,
    std::size_t target_index,
    const VisibilityRequirements& requirements)
{
    if (target_index >= snapshot.windows.size()) {
        return std::nullopt;
    }
    const auto& target = snapshot.windows[target_index];
    if (!target.visible || !target.currentDesktop || target.visualRect.empty()) {
        return std::nullopt;
    }

    const auto visibility = analyze_window_visibility(snapshot, target_index, requirements);
    if (!visibility) {
        return std::nullopt;
    }

    if (visibility->topLeft) {
        return VisibilityPreferenceRank::TopLeft;
    }
    if (visibility->topRight) {
        return VisibilityPreferenceRank::TopRight;
    }
    if (visibility->top) {
        return VisibilityPreferenceRank::TopOnly;
    }
    if (visibility->left) {
        return VisibilityPreferenceRank::LeftOnly;
    }
    if (visibility->right) {
        return VisibilityPreferenceRank::RightOnly;
    }
    if (visibility->bottom) {
        return VisibilityPreferenceRank::BottomOnly;
    }
    return VisibilityPreferenceRank::Unrecognized;
}

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
    const auto base_channel_loads = channel_loads(
        snapshot, policy.visibility, violation.targetIndex, policy.activeWindowIndex);
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

        const auto visibility_preference = visibility_preference_rank(
            simulated, violation.targetIndex, policy.visibility);
        if (!visibility_preference) {
            result.status = CandidateRankingStatus::InvalidInput;
            result.accepted.clear();
            result.rejected.clear();
            return result;
        }

        auto [left_load, right_load] = base_channel_loads;
        const bool target_left = *visibility_preference == VisibilityPreferenceRank::TopLeft;
        const bool target_right = *visibility_preference == VisibilityPreferenceRank::TopRight;
        const bool prefer_left = left_load == right_load
            ? target.zIndex % 2 != 0
            : left_load < right_load;
        if (target_left) {
            ++left_load;
        } else if (target_right) {
            ++right_load;
        }
        const auto channel_imbalance = left_load >= right_load
            ? left_load - right_load
            : right_load - left_load;
        const auto alternation_penalty = (target_left || target_right) &&
                target_left != prefer_left
            ? 1u
            : 0u;

        RankedCandidate ranked;
        ranked.originalIndex = candidate_index;
        ranked.candidate = candidate;
        ranked.cost = calculate_cost(
            target,
            candidate,
            *visibility_preference,
            policy.preferredEdge,
            channel_imbalance,
            alternation_penalty);
        ranked.simulatedSnapshot = std::move(simulated);
        ranked.remainingViolations = scan.violations;
        result.accepted.push_back(std::move(ranked));
    }

    std::sort(result.accepted.begin(), result.accepted.end(), &less_cost);
    return result;
}

} // namespace stage_manager::solver
