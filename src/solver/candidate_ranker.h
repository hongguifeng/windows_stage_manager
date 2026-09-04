#pragma once

#include "solver/candidate_generator.h"
#include "solver/layout_snapshot.h"
#include "solver/visibility_analyzer.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace stage_manager::solver {

enum class HardConstraintFailure : std::uint8_t {
    None,
    ActiveWindow,
    UnavailableWindow,
    SizeChanged,
    InconsistentDelta,
    OutsideWorkArea,
    TargetStillViolated,
    OtherManagedWindowViolated,
};

enum class CandidateRankingStatus : std::uint8_t {
    Ok,
    InvalidInput,
    GeometryTooComplex,
};

struct CandidateRankingPolicy {
    VisibilityRequirements visibility;
    std::uint64_t minimumOnscreenWidth = 100;
    std::uint64_t minimumOnscreenHeight = 100;
    std::optional<std::size_t> activeWindowIndex;
    std::optional<geometry::Edge> preferredEdge;
    bool requireStableLayout = true;
};

struct CandidateCost {
    std::uint32_t movedWindowCount = 0;
    std::uint64_t manhattanDistance = 0;
    std::uint64_t stableDistance = 0;
    std::uint64_t boundaryDistance = 0;
    std::uint32_t directionChangePenalty = 0;
    std::uintptr_t windowHandle = 0;
    std::uint64_t instanceGeneration = 0;
    std::int64_t left = 0;
    std::int64_t top = 0;

    constexpr bool operator==(const CandidateCost&) const noexcept = default;
};

struct RankedCandidate {
    std::size_t originalIndex = 0;
    PlacementCandidate candidate;
    CandidateCost cost;
    LayoutSnapshot simulatedSnapshot;
    std::vector<Violation> remainingViolations;
};

struct RejectedCandidate {
    std::size_t originalIndex = 0;
    HardConstraintFailure failure = HardConstraintFailure::None;
};

struct CandidateRankingResult {
    CandidateRankingStatus status = CandidateRankingStatus::Ok;
    std::vector<RankedCandidate> accepted;
    std::vector<RejectedCandidate> rejected;
};

CandidateRankingResult rank_candidates(const LayoutSnapshot& snapshot,
                                       const Violation& violation,
                                       std::span<const PlacementCandidate> candidates,
                                       const CandidateRankingPolicy& policy);

} // namespace stage_manager::solver
