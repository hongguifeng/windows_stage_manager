#include "solver/layout_solver.h"

#include <cstdio>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed: %s (line %d)\\n", #condition, __LINE__); return 1; } \
} while (false)

namespace solver = stage_manager::solver;
namespace geometry = stage_manager::geometry;

namespace {
solver::LayoutWindow window(std::uintptr_t key, geometry::Rect rect, int z)
{
    solver::LayoutWindow item;
    item.key = {key, static_cast<std::uint32_t>(key), key};
    item.placementRect = item.visualRect = item.lastStableRect = rect;
    item.workArea = {0, 0, 1920, 1040};
    item.titleBarHeight = 32;
    item.dpi = 96;
    item.zIndex = z;
    item.monitor = 1;
    item.managed = item.movable = item.visible = item.currentDesktop = item.blocksVisibility = true;
    return item;
}

solver::SolverPolicy policy()
{
    solver::SolverPolicy value;
    value.ranking.visibility.goal = solver::VisibilityGoal::TitleBarLeftHalf;
    value.ranking.visibility.top = {48, 48, 24, 100};
    value.ranking.visibility.left = {48, 48, 24, 100};
    value.ranking.visibility.right = {48, 48, 24, 100};
    return value;
}
} // namespace

int main()
{
    const auto settings = policy();
    const auto& visibility = settings.ranking.visibility;

    // The solver builds a strict left-up chain from the active visual origin.
    solver::LayoutSnapshot chain{{}, {
        window(1, {500, 500, 1100, 900}, 0),
        window(2, {500, 500, 1100, 900}, 1),
        window(3, {500, 500, 1100, 900}, 2)}};
    const auto arranged = solver::solve_title_bar_layout(chain, settings, 0);
    CHECK(arranged.status == solver::SolveStatus::Solved);
    CHECK(arranged.moves.size() == 2);
    CHECK((arranged.finalSnapshot.windows[1].visualRect == geometry::Rect{452, 452, 1052, 852}));
    CHECK((arranged.finalSnapshot.windows[2].visualRect == geometry::Rect{404, 404, 1004, 804}));
    CHECK(arranged.violations.empty());

    // A completed chain is a fixed point.
    const auto stable = solver::solve_title_bar_layout(arranged.finalSnapshot, settings, 0);
    CHECK(stable.status == solver::SolveStatus::NoViolation);
    CHECK(stable.moves.empty());

    // A fixed higher blocker terminates the prefix rather than selecting a
    // distant fallback location.
    auto blocked = chain;
    auto blocker = window(9, {0, 0, 1200, 490}, 0);
    blocker.managed = blocker.movable = false;
    blocked.windows.push_back(blocker);
    const auto stopped = solver::solve_title_bar_layout(blocked, settings, 0);
    CHECK(stopped.moves.empty());
    CHECK(std::string_view(stopped.stopReason) == "staircase_blocked");
    CHECK(stopped.status == solver::SolveStatus::Unsatisfiable);

    // Title visibility is a continuous prefix, not interchangeable halves.
    solver::LayoutSnapshot halves{{}, {
        window(1, {0, 0, 500, 300}, 0), window(2, {0, 0, 1000, 700}, 1)}};
    CHECK(!solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());
    halves.windows[0].visualRect = {500, 0, 1000, 300};
    CHECK(solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());
    halves.windows[0].visualRect = {499, 31, 1000, 300};
    CHECK(!solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());
    return 0;
}
