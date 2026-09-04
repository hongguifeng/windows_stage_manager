#include "solver/candidate_generator.h"

#include <algorithm>
#include <cstdint>
#include <limits>
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
                                               std::uint64_t repair_target_length,
                                               std::size_t maximum_candidates)
{
    CandidateGenerationResult result;
    if (violation.targetIndex >= snapshot.windows.size() || maximum_candidates == 0 ||
        repair_target_length >
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        result.status = CandidateGenerationStatus::InvalidInput;
        return result;
    }
    const auto& target = snapshot.windows[violation.targetIndex];
    if (!target.managed || !target.movable || target.placementRect.empty() ||
        target.visualRect.empty() || target.workArea.empty()) {
        result.status = CandidateGenerationStatus::InvalidInput;
        return result;
    }
    const auto repair = static_cast<std::int64_t>(repair_target_length);
    std::vector<AxisOffset> x_offsets = {{0, CandidateSource::Current}};
    std::vector<AxisOffset> y_offsets = {{0, CandidateSource::Current}};

    for (const auto blocker_index : violation.blockerIndices) {
        if (blocker_index >= snapshot.windows.size() ||
            blocker_index == violation.targetIndex) {
            result.status = CandidateGenerationStatus::InvalidInput;
            return result;
        }
        const auto& blocker = snapshot.windows[blocker_index].visualRect;
        const auto left_edge = checked_subtract(blocker.left, repair);
        const auto right_edge = checked_add(blocker.right, repair);
        const auto top_edge = checked_subtract(blocker.top, repair);
        const auto bottom_edge = checked_add(blocker.bottom, repair);
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
    }

    if (const auto delta = checked_subtract(target.workArea.left, target.placementRect.left)) {
        add_offset(x_offsets, *delta, CandidateSource::WorkAreaEdge);
    }
    if (const auto delta = checked_subtract(target.workArea.right, target.placementRect.right)) {
        add_offset(x_offsets, *delta, CandidateSource::WorkAreaEdge);
    }
    if (const auto delta = checked_subtract(target.workArea.top, target.placementRect.top)) {
        add_offset(y_offsets, *delta, CandidateSource::WorkAreaEdge);
    }
    if (const auto delta = checked_subtract(target.workArea.bottom, target.placementRect.bottom)) {
        add_offset(y_offsets, *delta, CandidateSource::WorkAreaEdge);
    }

    if (!add_candidate(result, target, 0, 0, CandidateSource::Current, maximum_candidates)) {
        result.status = CandidateGenerationStatus::TooComplex;
        result.candidates.clear();
        return result;
    }
    for (const auto& x_offset : x_offsets) {
        if (x_offset.value != 0 &&
            !add_candidate(result,
                           target,
                           x_offset.value,
                           0,
                           x_offset.source,
                           maximum_candidates)) {
            result.status = CandidateGenerationStatus::TooComplex;
            result.candidates.clear();
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
            result.status = CandidateGenerationStatus::TooComplex;
            result.candidates.clear();
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
                result.status = CandidateGenerationStatus::TooComplex;
                result.candidates.clear();
                return result;
            }
        }
    }
    return result;
}

} // namespace stage_manager::solver
