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

stage_manager::solver::VisibilityRequirements test_requirements()
{
    stage_manager::solver::VisibilityRequirements requirements;
    const stage_manager::solver::EdgeAffordanceRule rule{48, 48, 24, 100};
    requirements.top = rule;
    requirements.left = rule;
    requirements.right = rule;
    requirements.bottom = rule;
    requirements.maximumRegionRectangles = 128;
    return requirements;
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
    using stage_manager::solver::PlacementDirectionRank;
    using stage_manager::solver::PlacementCandidate;
    using stage_manager::solver::VisibilityRequirements;
    using stage_manager::solver::VisibilityPreferenceRank;
    using stage_manager::solver::generate_candidates;
    using stage_manager::solver::rank_candidates;
    using stage_manager::solver::scan_visibility_violations;
    using stage_manager::solver::visibility_preference_rank;

    LayoutSnapshot snapshot;
    snapshot.version = 10;
    snapshot.windows = {
        make_window(1, {100, 100, 300, 300}, 0, false),
        make_window(2, {100, 100, 300, 300}, 1, true),
    };
    const auto requirements = test_requirements();
    const auto violations = scan_visibility_violations(snapshot, requirements);
    CHECK(violations.violations.size() == 1);
    const auto generated = generate_candidates(
        snapshot, violations.violations[0], requirements, 128);
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
    // Spare space is still used, but only within the highest-priority
    // top-left direction before considering right, side, or bottom moves.
    CHECK((ranked.accepted.front().candidate.placementRect == Rect{38, 38, 238, 238}));
    CHECK(ranked.accepted.front().cost.hiddenArea == 19044);
    CHECK(ranked.accepted.front().cost.placementDirection ==
          PlacementDirectionRank::TopLeft);
    CHECK(ranked.accepted.front().cost.manhattanDistance == 124);
    CHECK(ranked.accepted.front().cost.visibilityPreference ==
          VisibilityPreferenceRank::TopLeft);
    CHECK(ranked.accepted.front().cost.centerDistance == 224);
    CHECK(ranked.accepted.front().cost.stableDistance == 124);
    CHECK(ranked.accepted.front().cost.directionChangePenalty == 0);
    CHECK(ranked.accepted.front().remainingViolations.empty());
    CHECK(rejected_for(ranked, 0, HardConstraintFailure::TargetStillViolated));

    auto top_preferred_policy = policy;
    top_preferred_policy.preferredEdge = Edge::Top;
    const auto top_preferred = rank_candidates(
        snapshot, violations.violations[0], generated.candidates, top_preferred_policy);
    CHECK(!top_preferred.accepted.empty());
    CHECK(top_preferred.accepted.front().cost.visibilityPreference ==
          VisibilityPreferenceRank::TopLeft);

    const auto preference_layout = [](const Rect& blocker) {
        LayoutSnapshot layout;
        layout.windows = {
            make_window(20, blocker, 0, false),
            make_window(21, {100, 100, 300, 300}, 1, true),
        };
        return layout;
    };
    CHECK(visibility_preference_rank(
              preference_layout({124, 124, 300, 300}), 1, requirements) ==
          VisibilityPreferenceRank::TopLeft);
    CHECK(visibility_preference_rank(
              preference_layout({100, 100, 276, 276}), 1, requirements) ==
          VisibilityPreferenceRank::RightOnly);
    CHECK(visibility_preference_rank(
              preference_layout({100, 124, 300, 300}), 1, requirements) ==
          VisibilityPreferenceRank::TopOnly);
    CHECK(visibility_preference_rank(
              preference_layout({124, 100, 300, 276}), 1, requirements) ==
          VisibilityPreferenceRank::LeftOnly);

    auto no_preference_policy = policy;
    no_preference_policy.preferredEdge.reset();
    const auto coordinate_tie_break = rank_candidates(
        snapshot, violations.violations[0], generated.candidates, no_preference_policy);
    CHECK(!coordinate_tie_break.accepted.empty());
    CHECK((coordinate_tie_break.accepted.front().candidate.placementRect ==
           Rect{38, 38, 238, 238}));

    const std::vector<PlacementCandidate> preference_over_distance_candidates = {
        {{164, 100, 364, 300}, 64, 0, CandidateSource::BlockerEdge},
        {{20, 100, 220, 300}, -80, 0, CandidateSource::BlockerEdge},
    };
    const auto preference_over_distance = rank_candidates(
        snapshot,
        violations.violations[0],
        preference_over_distance_candidates,
        no_preference_policy);
    CHECK(preference_over_distance.accepted.size() == 2);
    CHECK((preference_over_distance.accepted.front().candidate.placementRect ==
           Rect{20, 100, 220, 300}));
    CHECK(preference_over_distance.accepted.front().cost.visibilityPreference ==
          VisibilityPreferenceRank::TopLeft);
    CHECK(preference_over_distance.accepted.front().cost.centerDistance == 180);
    CHECK(preference_over_distance.accepted.front().cost.manhattanDistance == 80);
    CHECK(preference_over_distance.accepted[1].cost.visibilityPreference ==
          VisibilityPreferenceRank::TopOnly);
    CHECK(preference_over_distance.accepted[1].cost.centerDistance == 64);
    CHECK(preference_over_distance.accepted[1].cost.manhattanDistance == 64);

    LayoutSnapshot wide_snapshot;
    wide_snapshot.version = 11;
    wide_snapshot.windows = {
        make_window(30, {450, 200, 550, 300}, 0, false),
        make_window(31, {450, 200, 550, 300}, 1, true),
    };
    for (auto& window : wide_snapshot.windows) {
        window.workArea = {0, 0, 1000, 500};
    }
    const auto wide_violations = scan_visibility_violations(wide_snapshot, requirements);
    CHECK(wide_violations.violations.size() == 1);
    const std::vector<PlacementCandidate> equal_pixel_distance = {
        {{614, 200, 714, 300}, 164, 0, CandidateSource::BlockerEdge},
        {{450, 36, 550, 136}, 0, -164, CandidateSource::BlockerEdge},
    };
    const auto wide_ranked = rank_candidates(wide_snapshot,
                                             wide_violations.violations[0],
                                             equal_pixel_distance,
                                             no_preference_policy);
    CHECK(wide_ranked.accepted.size() == 2);
    CHECK(wide_ranked.accepted[0].originalIndex == 1);
    CHECK(wide_ranked.accepted[0].cost.placementDirection ==
          PlacementDirectionRank::TopLeft);
    CHECK(wide_ranked.accepted[0].cost.centerDistance == 164);
    CHECK(wide_ranked.accepted[1].cost.placementDirection ==
          PlacementDirectionRank::Right);
    CHECK(wide_ranked.accepted[1].cost.centerDistance == 82);

    LayoutSnapshot direction_snapshot;
    direction_snapshot.windows = {
        make_window(40, {200, 200, 400, 400}, 0, false),
        make_window(41, {200, 200, 400, 400}, 1, true),
    };
    for (auto& window : direction_snapshot.windows) {
        window.workArea = {0, 0, 600, 600};
    }
    const auto direction_violations = scan_visibility_violations(
        direction_snapshot, requirements);
    CHECK(direction_violations.violations.size() == 1);
    const std::vector<PlacementCandidate> direction_candidates = {
        {{200, 400, 400, 600}, 0, 200, CandidateSource::BlockerClearance},
        {{400, 200, 600, 400}, 200, 0, CandidateSource::BlockerClearance},
        {{0, 200, 200, 400}, -200, 0, CandidateSource::BlockerClearance},
        {{300, 100, 500, 300}, 100, -100, CandidateSource::AdaptiveSpread},
        {{100, 100, 300, 300}, -100, -100, CandidateSource::AdaptiveSpread},
    };
    const auto direction_ranked = rank_candidates(direction_snapshot,
                                                  direction_violations.violations[0],
                                                  direction_candidates,
                                                  no_preference_policy);
    CHECK(direction_ranked.accepted.size() == direction_candidates.size());
    CHECK(direction_ranked.accepted[0].cost.placementDirection ==
          PlacementDirectionRank::TopLeft);
    CHECK(direction_ranked.accepted[1].cost.placementDirection ==
          PlacementDirectionRank::TopRight);
    CHECK(direction_ranked.accepted[2].cost.placementDirection ==
          PlacementDirectionRank::Left);
    CHECK(direction_ranked.accepted[3].cost.placementDirection ==
          PlacementDirectionRank::Right);
    CHECK(direction_ranked.accepted[4].cost.placementDirection ==
          PlacementDirectionRank::Bottom);

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
        {{-100, 100, 100, 300}, -200, 0, CandidateSource::BlockerClearance},
    };
    const auto invalid_ranked = rank_candidates(
        snapshot, violations.violations[0], invalid_candidates, policy);
    CHECK(invalid_ranked.accepted.empty());
    CHECK(rejected_for(invalid_ranked, 0, HardConstraintFailure::SizeChanged));
    CHECK(rejected_for(invalid_ranked, 1, HardConstraintFailure::InconsistentDelta));
    CHECK(rejected_for(invalid_ranked, 2, HardConstraintFailure::OutsideWorkArea));
    // This candidate retains the old 100-pixel on-screen minimum but is now
    // rejected because part of the window would remain outside the work area.
    CHECK(rejected_for(invalid_ranked, 3, HardConstraintFailure::OutsideWorkArea));

    auto upward_limited_policy = policy;
    upward_limited_policy.maximumUpwardTravel = 24;
    const std::vector<PlacementCandidate> upward_limited_candidates = {
        {{76, 76, 276, 276}, -24, -24, CandidateSource::BlockerEdge},
        {{75, 75, 275, 275}, -25, -25, CandidateSource::AdaptiveSpread},
    };
    const auto upward_limited = rank_candidates(snapshot,
                                                violations.violations[0],
                                                upward_limited_candidates,
                                                upward_limited_policy);
    CHECK(upward_limited.accepted.size() == 1);
    CHECK(upward_limited.accepted[0].originalIndex == 0);
    CHECK(rejected_for(upward_limited, 1, HardConstraintFailure::UpwardTravelLimit));

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
    intermediate_policy.collectRemainingViolations = true;
    const auto intermediate_chain = rank_candidates(
        chain, violations.violations[0], chain_candidates, intermediate_policy);
    CHECK(intermediate_chain.accepted.size() == 1);
    CHECK(intermediate_chain.accepted[0].remainingViolations.size() == 1);
    CHECK(intermediate_chain.accepted[0].remainingViolations[0].targetIndex == 2);

    LayoutSnapshot lower_must_not_redirect_upper;
    lower_must_not_redirect_upper.windows = {
        make_window(50, {200, 200, 400, 400}, 0, false),
        make_window(51, {200, 200, 400, 400}, 1, true),
        make_window(52, {100, 100, 300, 300}, 2, true),
    };
    for (auto& window : lower_must_not_redirect_upper.windows) {
        window.workArea = {0, 0, 600, 600};
    }
    const auto layered_violations = scan_visibility_violations(
        lower_must_not_redirect_upper, requirements);
    const auto upper_violation = std::find_if(
        layered_violations.violations.begin(),
        layered_violations.violations.end(),
        [](const auto& item) { return item.targetIndex == 1; });
    CHECK(upper_violation != layered_violations.violations.end());
    const std::vector<PlacementCandidate> layered_candidates = {
        {{400, 200, 600, 400}, 200, 0, CandidateSource::BlockerClearance},
        {{100, 100, 300, 300}, -100, -100, CandidateSource::AdaptiveSpread},
    };
    const auto layered_ranked = rank_candidates(lower_must_not_redirect_upper,
                                                *upper_violation,
                                                layered_candidates,
                                                intermediate_policy);
    CHECK(layered_ranked.accepted.size() == 2);
    CHECK(layered_ranked.accepted[0].cost.placementDirection ==
          PlacementDirectionRank::TopLeft);
    CHECK(layered_ranked.accepted[0].cost.remainingViolationCount >
          layered_ranked.accepted[1].cost.remainingViolationCount);

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
