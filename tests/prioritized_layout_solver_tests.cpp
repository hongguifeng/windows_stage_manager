#include "solver/layout_solver.h"
#include "solver/visibility_analyzer.h"

#include <algorithm>
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
    bool managed = true,
    bool movable = true)
{
    stage_manager::solver::LayoutWindow window;
    window.key = {hwnd, static_cast<std::uint32_t>(hwnd), hwnd};
    window.placementRect = rectangle;
    window.visualRect = rectangle;
    window.workArea = {0, 0, 500, 500};
    window.lastStableRect = rectangle;
    window.monitor = 1;
    window.dpi = 96;
    window.titleBarHeight = 24;
    window.zIndex = z_index;
    window.managed = managed;
    window.movable = movable;
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
    policy.ranking.visibility.goal =
        stage_manager::solver::VisibilityGoal::AnyRecognizableEdge;
    policy.ranking.minimumOnscreenWidth = 100;
    policy.ranking.minimumOnscreenHeight = 100;
    policy.ranking.activeWindowIndex = 0;
    policy.limits.maximumMoves = 8;
    policy.limits.maximumStates = 512;
    policy.limits.maximumElapsedMs = 1000;
    policy.limits.maximumCandidatesPerViolation = 128;
    return policy;
}

class IncrementingClock final : public stage_manager::solver::ISolverClock {
public:
    explicit IncrementingClock(std::uint64_t step) : step_(step) {}

    std::uint64_t now_ms() override
    {
        const auto result = current_;
        current_ += step_;
        return result;
    }

private:
    std::uint64_t current_ = 0;
    std::uint64_t step_ = 0;
};

} // namespace

int main()
{
    using stage_manager::geometry::Rect;
    using stage_manager::solver::LayoutSnapshot;
    using stage_manager::solver::SolveStatus;
    using stage_manager::solver::scan_visibility_violations;
    using stage_manager::solver::solve_layout_prioritized;

    const auto policy = make_policy();

    LayoutSnapshot upper_succeeds;
    upper_succeeds.windows = {
        make_window(1, {100, 100, 300, 300}, 0, true, false),
        make_window(2, {100, 100, 300, 300}, 1),
        make_window(90, {0, 0, 500, 500}, 2, false, false),
        make_window(3, {100, 100, 300, 300}, 3),
    };
    const auto upper_result = solve_layout_prioritized(upper_succeeds, policy, 0);
    CHECK(upper_result.status == SolveStatus::PartiallySolved);
    CHECK(upper_result.moves.size() == 1);
    CHECK(upper_result.moves[0].window.hwnd == 2);
    CHECK(upper_result.violations.size() == 1);
    CHECK(upper_result.finalSnapshot.windows[upper_result.violations[0].targetIndex].key.hwnd == 3);
    for (std::size_t index = 0; index < upper_succeeds.windows.size(); ++index) {
        CHECK(upper_result.finalSnapshot.windows[index].zIndex ==
              upper_succeeds.windows[index].zIndex);
    }

    LayoutSnapshot skip_impossible_upper;
    skip_impossible_upper.windows = {
        make_window(10, {100, 100, 300, 300}, 0, true, false),
        make_window(11, {100, 100, 300, 300}, 1, true, false),
        make_window(12, {100, 100, 300, 300}, 2),
    };
    const auto skip_result = solve_layout_prioritized(skip_impossible_upper, policy, 0);
    CHECK(skip_result.status == SolveStatus::Unsatisfiable);
    CHECK(skip_result.moves.empty());
    CHECK(skip_result.violations.size() == 2);

    LayoutSnapshot preserve_upper;
    preserve_upper.windows = {
        make_window(20, {100, 100, 300, 300}, 0, true, false),
        make_window(21, {320, 100, 480, 300}, 1),
        make_window(22, {100, 100, 300, 300}, 2),
    };
    const auto preserved_rect = preserve_upper.windows[1].placementRect;
    const auto preserve_result = solve_layout_prioritized(preserve_upper, policy, 0);
    CHECK(preserve_result.status == SolveStatus::Solved);
    CHECK(preserve_result.finalSnapshot.windows[1].placementRect == preserved_rect);
    CHECK(std::none_of(preserve_result.moves.begin(),
                       preserve_result.moves.end(),
                       [](const auto& move) { return move.window.hwnd == 21; }));

    LayoutSnapshot multiple_lower;
    multiple_lower.windows = {
        make_window(30, {100, 200, 300, 400}, 0, true, false),
        make_window(31, {100, 200, 300, 400}, 1),
        make_window(32, {100, 200, 300, 400}, 2),
        make_window(33, {100, 200, 300, 400}, 3),
    };
    const auto multiple_result = solve_layout_prioritized(multiple_lower, policy, 0);
    CHECK(multiple_result.status == SolveStatus::Solved);
    CHECK(multiple_result.moves.size() >= 2);
    CHECK(scan_visibility_violations(
              multiple_result.finalSnapshot, policy.ranking.visibility).violations.empty());

    auto staircase_policy = policy;
    staircase_policy.ranking.visibility.goal =
        stage_manager::solver::VisibilityGoal::TopAndSide;
    LayoutSnapshot deterministic_staircase;
    deterministic_staircase.windows = {
        make_window(60, {400, 300, 700, 600}, 0, true, false),
        make_window(61, {100, 100, 400, 400}, 1),
        make_window(62, {650, 100, 950, 400}, 2),
        make_window(63, {300, 350, 600, 650}, 3),
    };
    for (auto& window : deterministic_staircase.windows) {
        window.workArea = {0, 0, 1000, 700};
    }
    const auto staircase_result = solve_layout_prioritized(
        deterministic_staircase, staircase_policy, 0);
    CHECK(staircase_result.status == SolveStatus::Solved);
    CHECK(staircase_result.moves.size() == 3);
    CHECK((staircase_result.moves[0].to == Rect{376, 264, 676, 564}));
    CHECK((staircase_result.moves[1].to == Rect{352, 228, 652, 528}));
    CHECK((staircase_result.moves[2].to == Rect{328, 192, 628, 492}));
    for (const auto& move : staircase_result.moves) {
        CHECK(move.cost.placementDirection ==
              stage_manager::solver::PlacementDirectionRank::TopLeft);
    }
    CHECK(scan_visibility_violations(staircase_result.finalSnapshot,
                                     staircase_policy.ranking.visibility).violations.empty());

    LayoutSnapshot right_staircase;
    right_staircase.windows = {
        make_window(70, {20, 100, 220, 300}, 0, true, false),
        make_window(71, {20, 100, 220, 300}, 1),
    };
    for (auto& window : right_staircase.windows) {
        window.workArea = {0, 0, 500, 500};
    }
    const auto right_result = solve_layout_prioritized(
        right_staircase, staircase_policy, 0);
    CHECK(right_result.status == SolveStatus::Solved);
    CHECK(right_result.moves.size() == 1);
    CHECK((right_result.moves[0].to == Rect{44, 64, 244, 264}));
    CHECK(right_result.moves[0].cost.placementDirection ==
          stage_manager::solver::PlacementDirectionRank::TopRight);

    auto already_valid = deterministic_staircase;
    already_valid.windows = staircase_result.finalSnapshot.windows;
    const auto unchanged = solve_layout_prioritized(
        already_valid, staircase_policy, 0);
    CHECK(unchanged.status == SolveStatus::NoViolation);
    CHECK(unchanged.moves.empty());
    for (std::size_t index = 0; index < already_valid.windows.size(); ++index) {
        CHECK(unchanged.finalSnapshot.windows[index].placementRect ==
              already_valid.windows[index].placementRect);
    }

    LayoutSnapshot upper_creates_lower_work;
    upper_creates_lower_work.windows = {
        make_window(34, {200, 200, 400, 400}, 0, true, false),
        make_window(35, {200, 200, 400, 400}, 1),
        make_window(36, {100, 100, 300, 300}, 2),
    };
    for (auto& window : upper_creates_lower_work.windows) {
        window.workArea = {0, 0, 600, 600};
    }
    const auto layered_result = solve_layout_prioritized(
        upper_creates_lower_work, policy, 0);
    CHECK(layered_result.status == SolveStatus::Solved);
    CHECK(!layered_result.moves.empty());
    CHECK(layered_result.moves[0].window.hwnd == 35);
    CHECK(layered_result.moves[0].cost.placementDirection ==
          stage_manager::solver::PlacementDirectionRank::TopLeft);
    CHECK(layered_result.moves[0].from.top - layered_result.moves[0].to.top <=
          static_cast<std::int64_t>(
              upper_creates_lower_work.windows[1].titleBarHeight * 3));
    CHECK(scan_visibility_violations(
              layered_result.finalSnapshot, policy.ranking.visibility).violations.empty());

    auto state_limited_policy = policy;
    state_limited_policy.limits.maximumStates = 3;
    const auto state_limited = solve_layout_prioritized(
        multiple_lower, state_limited_policy, 0);
    CHECK(state_limited.status == SolveStatus::Solved);
    CHECK(state_limited.moves.size() == 3);
    CHECK(state_limited.violations.empty());

    LayoutSnapshot no_improvement;
    no_improvement.windows = {
        make_window(40, {100, 100, 300, 300}, 0, true, false),
        make_window(41, {100, 100, 300, 300}, 1, true, false),
    };
    const auto failed = solve_layout_prioritized(no_improvement, policy, 0);
    CHECK(failed.status == SolveStatus::Unsatisfiable);
    CHECK(failed.moves.empty());
    CHECK(failed.finalSnapshot.windows[1].placementRect ==
          no_improvement.windows[1].placementRect);

    IncrementingClock timeout_clock(10);
    auto timeout_policy = policy;
    timeout_policy.limits.maximumElapsedMs = 5;
    const auto timeout = solve_layout_prioritized(
        skip_impossible_upper, timeout_policy, 0, &timeout_clock);
    CHECK(timeout.status == SolveStatus::Timeout);
    CHECK(timeout.moves.empty());
    CHECK(timeout.finalSnapshot.windows[2].placementRect ==
          skip_impossible_upper.windows[2].placementRect);

    return 0;
}
