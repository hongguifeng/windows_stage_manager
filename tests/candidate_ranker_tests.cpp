#include "solver/candidate_generator.h"
#include "solver/candidate_ranker.h"
#include "solver/visibility_analyzer.h"

#include <algorithm>
#include <cstdio>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

stage_manager::solver::LayoutWindow make_window(std::uintptr_t hwnd,
                                                 stage_manager::geometry::Rect rectangle,
                                                 std::int32_t z_index,
                                                 bool managed)
{
    stage_manager::solver::LayoutWindow window;
    window.key = {hwnd, static_cast<std::uint32_t>(hwnd), hwnd};
    window.placementRect = rectangle;
    window.visualRect = rectangle;
    window.workArea = {0, 0, 500, 500};
    window.lastStableRect = rectangle;
    window.monitor = 1;
    window.zIndex = z_index;
    window.managed = managed;
    window.movable = managed;
    window.visible = true;
    window.blocksVisibility = true;
    window.currentDesktop = true;
    return window;
}

bool rejected_for(const stage_manager::solver::CandidateRankingResult& result,
                  std::size_t original_index,
                  stage_manager::solver::HardConstraintFailure failure)
{
    return std::any_of(result.rejected.begin(), result.rejected.end(),
                       [original_index, failure](const auto& rejected) {
                           return rejected.originalIndex == original_index &&
                               rejected.failure == failure;
                       });
}

} // namespace

int main()
{
    using stage_manager::geometry::Edge;
    using stage_manager::geometry::Rect;
    using stage_manager::solver::CandidateGenerationStatus;
    using stage_manager::solver::CandidateRankingPolicy;
    using stage_manager::solver::CandidateRankingStatus;
    using stage_manager::solver::CandidateSource;
    using stage_manager::solver::HardConstraintFailure;
    using stage_manager::solver::LayoutSnapshot;
    using stage_manager::solver::PlacementCandidate;
    using stage_manager::solver::VisibilityRequirements;
    using stage_manager::solver::generate_candidates;
    using stage_manager::solver::rank_candidates;
    using stage_manager::solver::scan_visibility_violations;

    LayoutSnapshot snapshot;
    snapshot.version = 10;
    snapshot.windows = {
        make_window(1, {100, 100, 300, 300}, 0, false),
        make_window(2, {100, 100, 300, 300}, 1, true),
    };
    const VisibilityRequirements requirements{48, 24, 128};
    const auto violations = scan_visibility_violations(snapshot, requirements);
    CHECK(violations.violations.size() == 1);
    const auto generated = generate_candidates(snapshot, violations.violations[0], 64, 128);
    CHECK(generated.status == CandidateGenerationStatus::Ok);

    CandidateRankingPolicy policy;
    policy.visibility = requirements;
    policy.minimumOnscreenWidth = 100;
    policy.minimumOnscreenHeight = 100;
    policy.activeWindowIndex = 0;
    policy.preferredEdge = Edge::Left;
    policy.requireStableLayout = true;

    const auto ranked = rank_candidates(
        snapshot, violations.violations[0], generated.candidates, policy);
    CHECK(ranked.status == CandidateRankingStatus::Ok);
    CHECK(!ranked.accepted.empty());
    CHECK((ranked.accepted.front().candidate.placementRect == Rect{36, 100, 236, 300}));
    CHECK(ranked.accepted.front().cost.manhattanDistance == 64);
    CHECK(ranked.accepted.front().cost.stableDistance == 64);
    CHECK(ranked.accepted.front().cost.directionChangePenalty == 0);
    CHECK(ranked.accepted.front().remainingViolations.empty());
    CHECK(rejected_for(ranked, 0, HardConstraintFailure::TargetStillViolated));

    auto top_preferred_policy = policy;
    top_preferred_policy.preferredEdge = Edge::Top;
    const auto top_preferred = rank_candidates(
        snapshot, violations.violations[0], generated.candidates, top_preferred_policy);
    CHECK(!top_preferred.accepted.empty());
    CHECK((top_preferred.accepted.front().candidate.placementRect ==
           Rect{100, 36, 300, 236}));

    auto no_preference_policy = policy;
    no_preference_policy.preferredEdge.reset();
    const auto coordinate_tie_break = rank_candidates(
        snapshot, violations.violations[0], generated.candidates, no_preference_policy);
    CHECK(!coordinate_tie_break.accepted.empty());
    CHECK((coordinate_tie_break.accepted.front().candidate.placementRect ==
           Rect{36, 100, 236, 300}));

    const auto repeated = rank_candidates(
        snapshot, violations.violations[0], generated.candidates, policy);
    CHECK(repeated.status == CandidateRankingStatus::Ok);
    CHECK(repeated.accepted.size() == ranked.accepted.size());
    CHECK(repeated.rejected.size() == ranked.rejected.size());
    for (std::size_t index = 0; index < ranked.accepted.size(); ++index) {
        CHECK(repeated.accepted[index].originalIndex == ranked.accepted[index].originalIndex);
        CHECK(repeated.accepted[index].cost == ranked.accepted[index].cost);
        CHECK(repeated.accepted[index].candidate.placementRect ==
              ranked.accepted[index].candidate.placementRect);
    }

    const std::vector<PlacementCandidate> invalid_candidates = {
        {{36, 100, 235, 300}, -64, 0, CandidateSource::BlockerEdge},
        {{36, 100, 236, 300}, -63, 0, CandidateSource::BlockerEdge},
        {{-1000, 100, -800, 300}, -1100, 0, CandidateSource::BlockerEdge},
    };
    const auto invalid_ranked = rank_candidates(
        snapshot, violations.violations[0], invalid_candidates, policy);
    CHECK(invalid_ranked.accepted.empty());
    CHECK(rejected_for(invalid_ranked, 0, HardConstraintFailure::SizeChanged));
    CHECK(rejected_for(invalid_ranked, 1, HardConstraintFailure::InconsistentDelta));
    CHECK(rejected_for(invalid_ranked, 2, HardConstraintFailure::OutsideWorkArea));

    auto active_target_policy = policy;
    active_target_policy.activeWindowIndex = 1;
    const auto active_target = rank_candidates(
        snapshot, violations.violations[0], generated.candidates, active_target_policy);
    CHECK(active_target.accepted.empty());
    CHECK(active_target.rejected.size() == generated.candidates.size());
    CHECK(rejected_for(active_target, 0, HardConstraintFailure::ActiveWindow));

    auto unavailable_snapshot = snapshot;
    unavailable_snapshot.windows[1].currentDesktop = false;
    const auto unavailable = rank_candidates(
        unavailable_snapshot, violations.violations[0], generated.candidates, policy);
    CHECK(unavailable.accepted.empty());
    CHECK(rejected_for(unavailable, 0, HardConstraintFailure::UnavailableWindow));

    LayoutSnapshot chain = snapshot;
    chain.windows.push_back(make_window(3, {36, 100, 236, 300}, 2, true));
    const PlacementCandidate left_candidate{
        {36, 100, 236, 300}, -64, 0, CandidateSource::BlockerEdge};
    const std::vector<PlacementCandidate> chain_candidates = {left_candidate};
    const auto stable_chain = rank_candidates(
        chain, violations.violations[0], chain_candidates, policy);
    CHECK(stable_chain.accepted.empty());
    CHECK(rejected_for(
        stable_chain, 0, HardConstraintFailure::OtherManagedWindowViolated));

    auto intermediate_policy = policy;
    intermediate_policy.requireStableLayout = false;
    const auto intermediate_chain = rank_candidates(
        chain, violations.violations[0], chain_candidates, intermediate_policy);
    CHECK(intermediate_chain.accepted.size() == 1);
    CHECK(intermediate_chain.accepted[0].remainingViolations.size() == 1);
    CHECK(intermediate_chain.accepted[0].remainingViolations[0].targetIndex == 2);

    auto invalid_policy = policy;
    invalid_policy.minimumOnscreenWidth = 0;
    CHECK(rank_candidates(snapshot,
                          violations.violations[0],
                          generated.candidates,
                          invalid_policy)
              .status == CandidateRankingStatus::InvalidInput);

    auto complex_snapshot = snapshot;
    complex_snapshot.windows[0].placementRect = {100, 175, 124, 225};
    complex_snapshot.windows[0].visualRect = complex_snapshot.windows[0].placementRect;
    auto complex_policy = policy;
    complex_policy.visibility.maximumRegionRectangles = 1;
    const std::vector<PlacementCandidate> unchanged_candidate = {
        {{100, 100, 300, 300}, 0, 0, CandidateSource::Current},
    };
    CHECK(rank_candidates(complex_snapshot,
                          violations.violations[0],
                          unchanged_candidate,
                          complex_policy)
              .status == CandidateRankingStatus::GeometryTooComplex);
    return 0;
}
