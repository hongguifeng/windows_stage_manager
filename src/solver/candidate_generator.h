#pragma once

#include "solver/layout_snapshot.h"
#include "solver/visibility_analyzer.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stage_manager::solver {

enum class CandidateSource : std::uint32_t {
    None = 0,
    Current = 1u << 0,
    BlockerEdge = 1u << 1,
    WorkAreaEdge = 1u << 2,
    CombinedAxes = 1u << 3,
    TopLeftChannel = 1u << 4,
    TopRightChannel = 1u << 5,
    BlockerClearance = 1u << 6,
    AdaptiveSpread = 1u << 7,
    TitleBarStep = 1u << 8,
};

constexpr CandidateSource operator|(CandidateSource left, CandidateSource right) noexcept
{
    return static_cast<CandidateSource>(
        static_cast<std::uint32_t>(left) | static_cast<std::uint32_t>(right));
}

enum class CandidateGenerationStatus : std::uint8_t {
    Ok,
    Truncated,
    InvalidInput,
    TooComplex,
};

struct PlacementCandidate {
    geometry::Rect placementRect;
    std::int64_t deltaX = 0;
    std::int64_t deltaY = 0;
    CandidateSource sources = CandidateSource::None;
};

struct CandidateGenerationResult {
    CandidateGenerationStatus status = CandidateGenerationStatus::Ok;
    std::vector<PlacementCandidate> candidates;
};

CandidateGenerationResult generate_candidates(const LayoutSnapshot& snapshot,
                                               const Violation& violation,
                                               const VisibilityRequirements& requirements,
                                               std::size_t maximum_candidates = 512);

} // namespace stage_manager::solver
