#include "solver/candidate_ranker.h"
#include "solver/layout_hash.h"
#include "solver/layout_solver.h"
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
    window.titleBarHeight = 24;
    window.zIndex = z_index;
    window.managed = managed;
    window.movable = managed;
    window.visible = true;
    window.blocksVisibility = true;
    window.currentDesktop = true;
    return window;
}

stage_manager::solver::SolverPolicy make_policy()
{
    stage_manager::solver::SolverPolicy policy;
    const stage_manager::solver::EdgeAffordanceRule edge{48, 48, 24, 100};
    policy.ranking.visibility.top = edge;
    policy.ranking.visibility.left = edge;
    policy.ranking.visibility.right = edge;
    policy.ranking.visibility.bottom = edge;
    policy.ranking.visibility.maximumRegionRectangles = 128;
    policy.ranking.minimumOnscreenWidth = 100;
    policy.ranking.minimumOnscreenHeight = 100;
    policy.ranking.activeWindowIndex = 0;
    policy.ranking.preferredEdge = stage_manager::geometry::Edge::Left;
    policy.limits.maximumMoves = 8;
    policy.limits.maximumStates = 512;
    policy.limits.maximumElapsedMs = 1000;
    policy.limits.maximumCandidatesPerViolation = 128;
    return policy;
}

class IncrementingClock final : public stage_manager::solver::ISolverClock {
public:
    explicit IncrementingClock(std::uint64_t step)
        : step_(step)
    {
    }

    std::uint64_t now_ms() override
    {
        const auto value = current_;
        current_ += step_;
        return value;
    }

private:
    std::uint64_t current_ = 0;
    std::uint64_t step_ = 0;
};

} // namespace

int main()
{
    using stage_manager::geometry::Rect;
    using stage_manager::solver::CandidateSource;
    using stage_manager::solver::HardConstraintFailure;
    using stage_manager::solver::LayoutSnapshot;
    using stage_manager::solver::PlacementCandidate;
    using stage_manager::solver::SolveStatus;
    using stage_manager::solver::hash_layout;
    using stage_manager::solver::analyze_window_visibility;
    using stage_manager::solver::rank_candidates;
    using stage_manager::solver::scan_visibility_violations;
    using stage_manager::solver::solve_layout;
    using stage_manager::solver::solve_layout_incrementally;

    auto policy = make_policy();

    LayoutSnapshot valid;
    valid.version = 1;
    valid.windows = {
        make_window(1, {100, 100, 300, 300}, 0, false),
        make_window(2, {320, 100, 480, 300}, 1, true),
    };
    const auto no_violation = solve_layout(valid, policy);
    CHECK(no_violation.status == SolveStatus::NoViolation);
    CHECK(no_violation.moves.empty());
    CHECK(no_violation.statesVisited == 1);
    CHECK(no_violation.finalState == hash_layout(valid));

    LayoutSnapshot minimally_exposed;
    minimally_exposed.version = 2;
    minimally_exposed.windows = {
        make_window(1, {100, 100, 300, 300}, 0, false),
        make_window(2, {76, 76, 276, 276}, 1, true),
    };
    const auto expanded = solve_layout(minimally_exposed, policy);
    CHECK(expanded.status == SolveStatus::Solved);
    CHECK(expanded.moves.size() == 1);
    CHECK((expanded.moves[0].from == Rect{76, 76, 276, 276}));
    CHECK((expanded.moves[0].to == Rect{38, 38, 238, 238}));
    CHECK(expanded.moves[0].cost.placementDirection ==
          stage_manager::solver::PlacementDirectionRank::TopLeft);

    LayoutSnapshot covered;
    covered.version = 3;
    covered.windows = {
        make_window(1, {100, 100, 300, 300}, 0, false),
        make_window(2, {100, 100, 300, 300}, 1, true),
    };
    const auto solved = solve_layout(covered, policy);
    CHECK(solved.status == SolveStatus::Solved);
    CHECK(solved.moves.size() == 1);
    CHECK(solved.moves[0].window == covered.windows[1].key);
    CHECK((solved.moves[0].from == Rect{100, 100, 300, 300}));
    CHECK((solved.moves[0].to == Rect{38, 38, 238, 238}));
    CHECK(scan_visibility_violations(solved.finalSnapshot, policy.ranking.visibility)
              .violations.empty());

    const auto repeated = solve_layout(covered, policy);
    CHECK(repeated.status == solved.status);
    CHECK(repeated.finalState == solved.finalState);
    CHECK(repeated.moves.size() == solved.moves.size());
    CHECK(repeated.moves[0].to == solved.moves[0].to);

    LayoutSnapshot chain = covered;
    chain.windows.push_back(make_window(3, {36, 100, 236, 300}, 2, true));
    const auto chain_result = solve_layout(chain, policy);
    CHECK(chain_result.status == SolveStatus::Solved);
    CHECK(chain_result.moves.size() == 2);
    CHECK(std::any_of(chain_result.moves.begin(), chain_result.moves.end(), [&chain](const auto& move) {
        return move.window == chain.windows[1].key;
    }));
    CHECK(std::any_of(chain_result.moves.begin(), chain_result.moves.end(), [&chain](const auto& move) {
        return move.window == chain.windows[2].key;
    }));
    CHECK(scan_visibility_violations(chain_result.finalSnapshot, policy.ranking.visibility)
              .violations.empty());

    LayoutSnapshot stacked;
    stacked.version = 3;
    stacked.windows = {
        make_window(10, {100, 100, 400, 400}, 0, false),
        make_window(11, {100, 100, 400, 400}, 1, true),
        make_window(12, {100, 100, 400, 400}, 2, true),
        make_window(13, {100, 100, 400, 400}, 3, true),
    };
    auto incremental_policy = policy;
    incremental_policy.ranking.visibility.goal =
        stage_manager::solver::VisibilityGoal::TopAndSide;
    const auto incremental = solve_layout_incrementally(stacked, incremental_policy, 0);
    CHECK(incremental.status == SolveStatus::Solved);
    CHECK(incremental.moves.size() == 3);
    CHECK(incremental.finalSnapshot.windows[1].managed);
    CHECK(incremental.finalSnapshot.windows[2].managed);
    CHECK(incremental.finalSnapshot.windows[3].managed);
    CHECK(scan_visibility_violations(
              incremental.finalSnapshot, incremental_policy.ranking.visibility)
              .violations.empty());
    std::uint32_t left_channel_count = 0;
    std::uint32_t right_channel_count = 0;
    for (std::size_t index = 1; index < incremental.finalSnapshot.windows.size(); ++index) {
        const auto visibility = analyze_window_visibility(
            incremental.finalSnapshot, index, incremental_policy.ranking.visibility);
        CHECK(visibility.has_value());
        left_channel_count += visibility->topLeft ? 1u : 0u;
        right_channel_count += visibility->topRight ? 1u : 0u;
    }
    CHECK(left_channel_count > 0);
    CHECK(right_channel_count > 0);
    CHECK(left_channel_count <= right_channel_count + 1);
    CHECK(right_channel_count <= left_channel_count + 1);

    LayoutSnapshot incremental_partial;
    incremental_partial.version = 4;
    incremental_partial.windows = {
        make_window(20, {100, 100, 300, 300}, 0, false),
        make_window(21, {100, 100, 300, 300}, 1, true),
        make_window(90, {0, 0, 500, 500}, 2, false),
        make_window(22, {100, 100, 300, 300}, 3, true),
    };
    auto partial_policy = policy;
    partial_policy.ranking.visibility.goal =
        stage_manager::solver::VisibilityGoal::AnyRecognizableEdge;
    const auto partial = solve_layout_incrementally(
        incremental_partial, partial_policy, 0);
    CHECK(partial.status == SolveStatus::PartiallySolved);
    CHECK(partial.moves.size() == 1);
    CHECK(partial.moves[0].window.hwnd == 21);
    CHECK(partial.violations.size() == 1);
    CHECK(partial.finalSnapshot.windows[partial.violations[0].targetIndex].key.hwnd == 22);

    LayoutSnapshot z_order_priority;
    z_order_priority.version = 5;
    z_order_priority.windows = {
        make_window(30, {100, 100, 300, 300}, 0, false),
        make_window(31, {100, 100, 300, 300}, 1, true),
        make_window(32, {100, 100, 300, 300}, 2, true),
        make_window(33, {100, 100, 300, 300}, 3, true),
    };
    auto z_order_policy = partial_policy;
    z_order_policy.limits.maximumMoves = 1;
    const auto z_order_result = solve_layout_prioritized(
        z_order_priority, z_order_policy, 0);
    CHECK(z_order_result.status == SolveStatus::PartiallySolved);
    CHECK(z_order_result.moves.size() == 1);
    CHECK(z_order_result.moves[0].window.hwnd == 31);
    CHECK(z_order_result.moves[0].cost.placementDirection ==
          stage_manager::solver::PlacementDirectionRank::TopLeft);

    auto active_target_policy = policy;
    active_target_policy.ranking.activeWindowIndex = 1;
    const auto unsatisfiable = solve_layout(covered, active_target_policy);
    CHECK(unsatisfiable.status == SolveStatus::Unsatisfiable);
    CHECK(unsatisfiable.moves.empty());
    CHECK(!unsatisfiable.violations.empty());
    CHECK(!unsatisfiable.candidateRejections.empty());
    for (const auto& rejection : unsatisfiable.candidateRejections) {
        CHECK(rejection.window == covered.windows[1].key);
        CHECK(rejection.failure == HardConstraintFailure::ActiveWindow);
    }
    CHECK(unsatisfiable.finalState == hash_layout(covered));

    auto state_limited_policy = policy;
    state_limited_policy.limits.maximumStates = 1;
    const auto state_limited = solve_layout(covered, state_limited_policy);
    CHECK(state_limited.status == SolveStatus::Timeout);
    CHECK(state_limited.statesVisited == 1);
    CHECK(state_limited.moves.empty());
    CHECK(state_limited.finalState == hash_layout(covered));

    IncrementingClock timeout_clock(10);
    auto timeout_policy = policy;
    timeout_policy.limits.maximumElapsedMs = 5;
    const auto timed_out = solve_layout(covered, timeout_policy, &timeout_clock);
    CHECK(timed_out.status == SolveStatus::Timeout);
    CHECK(timed_out.elapsedMs >= timeout_policy.limits.maximumElapsedMs);
    CHECK(timed_out.moves.empty());
    CHECK(timed_out.finalState == hash_layout(covered));

    auto invalid_policy = policy;
    invalid_policy.limits.maximumMoves = 0;
    CHECK(solve_layout(covered, invalid_policy).status == SolveStatus::InvalidSnapshot);

    auto complex = covered;
    complex.windows[0].placementRect = {100, 175, 124, 225};
    complex.windows[0].visualRect = complex.windows[0].placementRect;
    auto complex_policy = policy;
    complex_policy.ranking.visibility.maximumRegionRectangles = 1;
    const auto too_complex = solve_layout(complex, complex_policy);
    CHECK(too_complex.status == SolveStatus::GeometryTooComplex);
    CHECK(too_complex.moves.empty());
    CHECK(too_complex.finalState == hash_layout(complex));

    LayoutSnapshot offscreen = covered;
    offscreen.windows[0].placementRect = {0, 0, 200, 200};
    offscreen.windows[0].visualRect = offscreen.windows[0].placementRect;
    offscreen.windows[1].placementRect = {0, 0, 200, 200};
    offscreen.windows[1].visualRect = offscreen.windows[1].placementRect;
    offscreen.windows[1].lastStableRect = offscreen.windows[1].placementRect;
    const auto offscreen_violations = scan_visibility_violations(
        offscreen, policy.ranking.visibility);
    CHECK(offscreen_violations.violations.size() == 1);
    const std::vector<PlacementCandidate> offscreen_only = {
        {{-64, 0, 136, 200}, -64, 0, CandidateSource::BlockerEdge},
    };
    auto intermediate_policy = policy.ranking;
    intermediate_policy.requireStableLayout = false;
    const auto clipped_edge = rank_candidates(offscreen,
                                              offscreen_violations.violations[0],
                                              offscreen_only,
                                              intermediate_policy);
    CHECK(clipped_edge.accepted.empty());
    CHECK(clipped_edge.rejected.size() == 1);
    CHECK(clipped_edge.rejected[0].failure == HardConstraintFailure::OutsideWorkArea);

    LayoutSnapshot oversized;
    oversized.version = 6;
    oversized.windows = {
        make_window(40, {0, 0, 500, 500}, 0, false),
        make_window(41, {-50, 100, 550, 300}, 1, true),
    };
    const auto oversized_result = solve_layout(oversized, policy);
    CHECK(oversized_result.status == SolveStatus::Unsatisfiable);
    CHECK(oversized_result.moves.empty());
    CHECK((oversized_result.finalSnapshot.windows[1].placementRect ==
           Rect{-50, 100, 550, 300}));

    const auto original_hash = hash_layout(covered);
    auto moved = covered;
    moved.windows[1].placementRect = {101, 100, 301, 300};
    moved.windows[1].visualRect = moved.windows[1].placementRect;
    CHECK(hash_layout(moved) != original_hash);
    CHECK(hash_layout(covered) == original_hash);
    return 0;
}
