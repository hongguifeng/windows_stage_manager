#include "solver/candidate_generator.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <vector>

namespace stage_manager::solver {
namespace {

struct AxisOffset {
    std::int64_t value = 0;
    CandidateSource source = CandidateSource::None;
};

std::optional<std::int64_t> checked_subtract(std::int64_t left, std::int64_t right)
{
    if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right) ||
        (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)) {
        return std::nullopt;
    }
    return left - right;
}

std::optional<std::int64_t> checked_add(std::int64_t left, std::int64_t right)
{
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return std::nullopt;
    }
    return left + right;
}

void add_offset(std::vector<AxisOffset>& offsets,
                std::int64_t value,
                CandidateSource source)
{
    const auto iterator = std::find_if(offsets.begin(), offsets.end(), [value](const auto& offset) {
        return offset.value == value;
    });
    if (iterator == offsets.end()) {
        offsets.push_back({value, source});
    } else {
        iterator->source = iterator->source | source;
    }
}

bool add_candidate(CandidateGenerationResult& result,
                   const LayoutWindow& target,
                   std::int64_t delta_x,
                   std::int64_t delta_y,
                   CandidateSource sources,
                   std::size_t maximum_candidates)
{
    const auto placement = target.placementRect.translated(delta_x, delta_y);
    if (!placement) {
        return true;
    }
    const auto existing = std::find_if(
        result.candidates.begin(), result.candidates.end(), [&placement](const auto& candidate) {
            return candidate.placementRect == *placement;
        });
    if (existing != result.candidates.end()) {
        existing->sources = existing->sources | sources;
        return true;
    }
    if (result.candidates.size() == maximum_candidates) {
        return false;
    }
    result.candidates.push_back({*placement, delta_x, delta_y, sources});
    return true;
}

} // namespace

CandidateGenerationResult generate_candidates(const LayoutSnapshot& snapshot,
                                               const Violation& violation,
                                               const VisibilityRequirements& requirements,
                                               std::size_t maximum_candidates)
{
    CandidateGenerationResult result;
    if (violation.targetIndex >= snapshot.windows.size() || maximum_candidates == 0) {
        result.status = CandidateGenerationStatus::InvalidInput;
        return result;
    }
    const auto& target = snapshot.windows[violation.targetIndex];
    if (!target.managed || !target.movable || target.placementRect.empty() ||
        target.visualRect.empty() || target.workArea.empty()) {
        result.status = CandidateGenerationStatus::InvalidInput;
        return result;
    }
    const auto affordances = resolve_edge_affordances(target, requirements);
    if (std::any_of(affordances.begin(), affordances.end(), [](const auto& affordance) {
            return affordance.depth == 0 ||
                affordance.depth > static_cast<std::uint64_t>(
                    std::numeric_limits<std::int64_t>::max());
        })) {
        result.status = CandidateGenerationStatus::InvalidInput;
        return result;
    }
    const auto left_depth = static_cast<std::int64_t>(affordances[0].depth);
    const auto right_depth = static_cast<std::int64_t>(affordances[1].depth);
    const auto top_depth = static_cast<std::int64_t>(affordances[2].depth);
    const auto bottom_depth = static_cast<std::int64_t>(affordances[3].depth);
    const auto work_left_delta = checked_subtract(
        target.workArea.left, target.placementRect.left);
    const auto work_right_delta = checked_subtract(
        target.workArea.right, target.placementRect.right);
    const auto work_top_delta = checked_subtract(
        target.workArea.top, target.placementRect.top);
    const auto work_bottom_delta = checked_subtract(
        target.workArea.bottom, target.placementRect.bottom);
    std::vector<AxisOffset> x_offsets = {{0, CandidateSource::Current}};
    std::vector<AxisOffset> y_offsets = {{0, CandidateSource::Current}};

    // Keep the normal top-corner search local and predictable. The ranking
    // policy admits one step during the layered pass and may admit the second
    // step only during the final spare-space improvement pass.
    if (target.titleBarHeight > 0) {
        const auto title_bar_step = static_cast<std::int64_t>(target.titleBarHeight);
        if (const auto first_step = checked_subtract(0, title_bar_step)) {
            add_offset(y_offsets, *first_step, CandidateSource::TitleBarStep);
            if (const auto second_step = checked_subtract(*first_step, title_bar_step)) {
                add_offset(y_offsets, *second_step, CandidateSource::TitleBarStep);
            }
        }
    }

    if (!add_candidate(result, target, 0, 0, CandidateSource::Current, maximum_candidates)) {
        result.status = CandidateGenerationStatus::Truncated;
        return result;
    }

    for (const auto blocker_index : violation.blockerIndices) {
        if (blocker_index >= snapshot.windows.size() ||
            blocker_index == violation.targetIndex) {
            result.status = CandidateGenerationStatus::InvalidInput;
            return result;
        }
        const auto& blocker = snapshot.windows[blocker_index].visualRect;
        const auto left_edge = checked_subtract(blocker.left, left_depth);
        const auto right_edge = checked_add(blocker.right, right_depth);
        const auto top_edge = checked_subtract(blocker.top, top_depth);
        const auto bottom_edge = checked_add(blocker.bottom, bottom_depth);
        // Minimum-exposure offsets are enough to make a window clickable, but
        // they leave an artificial hole in the search space: the solver cannot
        // use a larger free region on the far side of a blocker. Add the four
        // exact non-overlap boundaries as well. Work-area constraints and
        // candidate ranking decide whether the extra travel is worthwhile.
        const auto clear_left = checked_subtract(blocker.left, target.visualRect.right);
        const auto clear_right = checked_subtract(blocker.right, target.visualRect.left);
        const auto clear_above = checked_subtract(blocker.top, target.visualRect.bottom);
        const auto clear_below = checked_subtract(blocker.bottom, target.visualRect.top);
        if (left_edge) {
            if (const auto delta = checked_subtract(*left_edge, target.visualRect.left)) {
                add_offset(x_offsets, *delta, CandidateSource::BlockerEdge);
            }
        }
        if (right_edge) {
            if (const auto delta = checked_subtract(*right_edge, target.visualRect.right)) {
                add_offset(x_offsets, *delta, CandidateSource::BlockerEdge);
            }
        }
        if (top_edge) {
            if (const auto delta = checked_subtract(*top_edge, target.visualRect.top)) {
                add_offset(y_offsets, *delta, CandidateSource::BlockerEdge);
            }
        }
        if (bottom_edge) {
            if (const auto delta = checked_subtract(*bottom_edge, target.visualRect.bottom)) {
                add_offset(y_offsets, *delta, CandidateSource::BlockerEdge);
            }
        }
        if (clear_left) {
            add_offset(x_offsets, *clear_left, CandidateSource::BlockerClearance);
        }
        if (clear_right) {
            add_offset(x_offsets, *clear_right, CandidateSource::BlockerClearance);
        }
        if (clear_above) {
            add_offset(y_offsets, *clear_above, CandidateSource::BlockerClearance);
        }
        if (clear_below) {
            add_offset(y_offsets, *clear_below, CandidateSource::BlockerClearance);
        }

        if (left_edge && top_edge) {
            const auto delta_x = checked_subtract(*left_edge, target.visualRect.left);
            const auto delta_y = checked_subtract(*top_edge, target.visualRect.top);
            if (delta_x && delta_y &&
                !add_candidate(result,
                               target,
                               *delta_x,
                               *delta_y,
                               CandidateSource::BlockerEdge |
                                   CandidateSource::CombinedAxes |
                                   CandidateSource::TopLeftChannel,
                               maximum_candidates)) {
                result.status = CandidateGenerationStatus::Truncated;
                return result;
            }
        }
        if (right_edge && top_edge) {
            const auto delta_x = checked_subtract(*right_edge, target.visualRect.right);
            const auto delta_y = checked_subtract(*top_edge, target.visualRect.top);
            if (delta_x && delta_y &&
                !add_candidate(result,
                               target,
                               *delta_x,
                               *delta_y,
                               CandidateSource::BlockerEdge |
                                   CandidateSource::CombinedAxes |
                                   CandidateSource::TopRightChannel,
                               maximum_candidates)) {
                result.status = CandidateGenerationStatus::Truncated;
                return result;
            }
        }

        // Add interior points between the minimum top-corner exposure and each
        // work-area corner. They let ranking use spare screen space without
        // jumping directly from a narrow strip to an exact screen edge.
        const auto add_spread = [&](std::optional<std::int64_t> edge_x,
                                    std::optional<std::int64_t> work_x,
                                    std::optional<std::int64_t> work_y,
                                    CandidateSource channel) {
            if (!edge_x || !work_x || !top_edge || !work_y) {
                return true;
            }
            const auto edge_y = checked_subtract(*top_edge, target.visualRect.top);
            if (!edge_y) {
                return true;
            }
            return add_candidate(result,
                                 target,
                                 std::midpoint(*edge_x, *work_x),
                                 std::midpoint(*edge_y, *work_y),
                                 CandidateSource::BlockerEdge |
                                     CandidateSource::WorkAreaEdge |
                                     CandidateSource::CombinedAxes |
                                     CandidateSource::AdaptiveSpread |
                                     channel,
                                 maximum_candidates);
        };
        const auto left_delta = left_edge
            ? checked_subtract(*left_edge, target.visualRect.left)
            : std::nullopt;
        const auto right_delta = right_edge
            ? checked_subtract(*right_edge, target.visualRect.right)
            : std::nullopt;
        if (!add_spread(left_delta,
                        work_left_delta,
                        work_top_delta,
                        CandidateSource::TopLeftChannel) ||
            !add_spread(left_delta,
                        work_left_delta,
                        work_bottom_delta,
                        CandidateSource::TopLeftChannel) ||
            !add_spread(right_delta,
                        work_right_delta,
                        work_top_delta,
                        CandidateSource::TopRightChannel) ||
            !add_spread(right_delta,
                        work_right_delta,
                        work_bottom_delta,
                        CandidateSource::TopRightChannel)) {
            result.status = CandidateGenerationStatus::Truncated;
            return result;
        }
    }

    if (work_left_delta) {
        add_offset(x_offsets, *work_left_delta, CandidateSource::WorkAreaEdge);
    }
    if (work_right_delta) {
        add_offset(x_offsets, *work_right_delta, CandidateSource::WorkAreaEdge);
    }
    if (work_top_delta) {
        add_offset(y_offsets, *work_top_delta, CandidateSource::WorkAreaEdge);
    }
    if (work_bottom_delta) {
        add_offset(y_offsets, *work_bottom_delta, CandidateSource::WorkAreaEdge);
    }

    for (const auto& x_offset : x_offsets) {
        if (x_offset.value != 0 &&
            !add_candidate(result,
                           target,
                           x_offset.value,
                           0,
                           x_offset.source,
                           maximum_candidates)) {
            result.status = CandidateGenerationStatus::Truncated;
            return result;
        }
    }
    for (const auto& y_offset : y_offsets) {
        if (y_offset.value != 0 &&
            !add_candidate(result,
                           target,
                           0,
                           y_offset.value,
                           y_offset.source,
                           maximum_candidates)) {
            result.status = CandidateGenerationStatus::Truncated;
            return result;
        }
    }
    for (const auto& x_offset : x_offsets) {
        if (x_offset.value == 0) {
            continue;
        }
        for (const auto& y_offset : y_offsets) {
            if (y_offset.value == 0) {
                continue;
            }
            const auto sources = x_offset.source | y_offset.source |
                CandidateSource::CombinedAxes;
            if (!add_candidate(result,
                               target,
                               x_offset.value,
                               y_offset.value,
                               sources,
                               maximum_candidates)) {
                result.status = CandidateGenerationStatus::Truncated;
                return result;
            }
        }
    }
    return result;
}

} // namespace stage_manager::solver
