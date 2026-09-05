#include "solver/visibility_analyzer.h"
#include "solver/z_order_solver.h"

#include <cstdio>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

stage_manager::solver::LayoutWindow make_window(
    std::uintptr_t hwnd,
    stage_manager::geometry::Rect rectangle,
    std::int32_t z_index,
    bool managed = true)
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
    window.movable = true;
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
    policy.ranking.minimumOnscreenWidth = 500;
    policy.ranking.minimumOnscreenHeight = 500;
    policy.ranking.activeWindowIndex = 0;
    policy.limits.maximumMoves = 8;
    policy.limits.maximumStates = 512;
    policy.limits.maximumElapsedMs = 1000;
    policy.limits.maximumCandidatesPerViolation = 128;
    return policy;
}

} // namespace

int main()
{
    using stage_manager::solver::LayoutSnapshot;
    using stage_manager::solver::SolveStatus;
    using stage_manager::solver::scan_visibility_violations;
    using stage_manager::solver::solve_layout;
    using stage_manager::solver::solve_z_order_fallback;

    const auto policy = make_policy();

    LayoutSnapshot bottom_succeeds;
    bottom_succeeds.windows = {
        make_window(1, {200, 200, 300, 300}, 0),
        make_window(3, {350, 350, 450, 450}, 1),
        make_window(2, {0, 0, 500, 500}, 2, false),
        make_window(4, {0, 0, 500, 500}, 3),
    };
    bottom_succeeds.windows[3].blocksVisibility = false;
    CHECK(solve_layout(bottom_succeeds, policy).status == SolveStatus::Unsatisfiable);
    const auto bottom_result = solve_z_order_fallback(bottom_succeeds, policy, 0);
    CHECK(bottom_result.status == SolveStatus::Solved);
    CHECK(bottom_result.candidatesTried == 1);
    CHECK(bottom_result.reorders.size() == 1);
    CHECK(bottom_result.reorders[0].window == bottom_succeeds.windows[3].key);
    CHECK(bottom_result.reorders[0].insertAfter == bottom_succeeds.windows[1].key);
    CHECK(bottom_result.reorders[0].fromZIndex == 3);
    CHECK(bottom_result.reorders[0].toZIndex == 2);
    CHECK(bottom_result.positionSolve.status == SolveStatus::NoViolation);
    CHECK(scan_visibility_violations(
              bottom_result.finalSnapshot, policy.ranking.visibility).violations.empty());
    CHECK(bottom_result.finalSnapshot.windows[0].zIndex == 0);
    CHECK(bottom_result.finalSnapshot.windows[1].zIndex == 1);
    CHECK(bottom_result.finalSnapshot.windows[3].zIndex == 2);
    CHECK(bottom_result.finalSnapshot.windows[2].zIndex == 3);

    auto gapped_z_order = bottom_succeeds;
    gapped_z_order.windows[0].zIndex = 5;
    gapped_z_order.windows[1].zIndex = 6;
    gapped_z_order.windows[2].zIndex = 8;
    gapped_z_order.windows[3].zIndex = 10;
    const auto gapped_result = solve_z_order_fallback(gapped_z_order, policy, 0);
    CHECK(gapped_result.status == SolveStatus::Solved);
    CHECK(gapped_result.reorders[0].insertAfter == gapped_z_order.windows[1].key);
    CHECK(gapped_result.reorders[0].toZIndex == 7);
    CHECK(gapped_result.finalSnapshot.windows[0].zIndex == 5);
    CHECK(gapped_result.finalSnapshot.windows[1].zIndex == 6);
    CHECK(gapped_result.finalSnapshot.windows[3].zIndex == 7);
    CHECK(gapped_result.finalSnapshot.windows[2].zIndex == 9);

    LayoutSnapshot next_candidate_succeeds;
    next_candidate_succeeds.windows = {
        make_window(10, {400, 400, 480, 480}, 0),
        make_window(11, {100, 100, 300, 300}, 1),
        make_window(12, {100, 100, 300, 300}, 2),
        make_window(13, {350, 0, 450, 100}, 3),
    };
    next_candidate_succeeds.windows[2].blocksVisibility = false;
    next_candidate_succeeds.windows[3].blocksVisibility = false;
    const auto next_result = solve_z_order_fallback(next_candidate_succeeds, policy, 0);
    CHECK(next_result.status == SolveStatus::Solved);
    CHECK(next_result.candidatesTried == 3);
    CHECK(next_result.reorders.size() == 1);
    CHECK(next_result.reorders[0].window == next_candidate_succeeds.windows[2].key);
    CHECK(next_result.reorders[0].insertAfter == next_candidate_succeeds.windows[0].key);
    CHECK(next_result.finalSnapshot.windows[0].zIndex == 0);

    auto topmost_bottom = bottom_succeeds;
    topmost_bottom.windows[3].topmost = true;
    const auto excludes_topmost = solve_z_order_fallback(topmost_bottom, policy, 0);
    CHECK(excludes_topmost.status == SolveStatus::Unsatisfiable);
    CHECK(excludes_topmost.reorders.empty());
    CHECK(excludes_topmost.candidatesTried == 0);

    auto all_fail = next_candidate_succeeds;
    all_fail.windows[1].visualRect = {0, 0, 500, 500};
    all_fail.windows[1].placementRect = all_fail.windows[1].visualRect;
    const auto failed = solve_z_order_fallback(all_fail, policy, 0);
    CHECK(failed.status == SolveStatus::Unsatisfiable);
    CHECK(failed.reorders.empty());
    CHECK(failed.finalSnapshot.windows[0].zIndex == 0);

    auto active_topmost = bottom_succeeds;
    active_topmost.windows[0].topmost = true;
    const auto topmost_active = solve_z_order_fallback(active_topmost, policy, 0);
    CHECK(topmost_active.status == SolveStatus::Unsatisfiable);
    CHECK(topmost_active.reorders.empty());
    CHECK(topmost_active.candidatesTried == 0);

    auto duplicate_z = bottom_succeeds;
    duplicate_z.windows[3].zIndex = 2;
    CHECK(solve_z_order_fallback(duplicate_z, policy, 0).status ==
          SolveStatus::InvalidSnapshot);
    return 0;
}
