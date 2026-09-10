#include "solver/layout_solver.h"
#include <cstdio>
#include <string_view>
namespace s = stage_manager::solver;
namespace g = stage_manager::geometry;
#define CHECK(x) do { if (!(x)) { std::fprintf(stderr, "failed: %s line %d\n", #x, __LINE__); return 1; } } while (false)
s::LayoutWindow make_window(unsigned id, g::Rect rect) {
    s::LayoutWindow w;
    w.key = {id, id, id};
    w.placementRect = w.visualRect = w.lastStableRect = rect;
    w.workArea = {0, 0, 1920, 1040};
    w.titleBarHeight = 32; w.dpi = 96; w.zIndex = static_cast<int>(id);
    w.monitor = 1;
    w.managed = w.movable = w.visible = w.currentDesktop = w.blocksVisibility = true;
    return w;
}
class Clock final : public s::ISolverClock {
    unsigned tick = 0;
public: std::uint64_t now_ms() override { return tick++; }
};
int main() {
    s::LayoutSnapshot before{{}, {
        make_window(1, {500, 500, 1000, 900}),
        make_window(2, {500, 500, 1100, 900}),
        make_window(3, {500, 500, 2700, 1700})}};
    s::SolverPolicy p;
    p.ranking.visibility.goal = s::VisibilityGoal::TitleBarLeftHalf;
    p.optimizeTitleBarLayout = true;
    p.limits.maximumElapsedMs = 1;
    Clock clock;
    const auto result = s::solve_title_bar_layout(before, p, 0, &clock);
    CHECK(result.status == s::SolveStatus::PartiallySolved);
    CHECK(result.moves.size() == 1);
    CHECK(result.moves.front().window == before.windows[1].key);
    CHECK(result.finalSnapshot.windows[1].placementRect == (g::Rect{452, 452, 1052, 852}));
    CHECK(result.finalSnapshot.windows[1].managed);
    CHECK(result.finalSnapshot.windows[2].placementRect == before.windows[2].placementRect);
    CHECK(result.finalSnapshot.windows[0].placementRect == before.windows[0].placementRect);
    CHECK(s::analyze_title_bar_exposure(result.finalSnapshot, 1, p.ranking.visibility).satisfied());
    auto roomy = before;
    roomy.windows[2] = make_window(3, {452, 452, 1052, 852});
    p.limits.maximumElapsedMs = 1000;
    const auto staircase = s::solve_title_bar_layout(roomy, p, 0);
    CHECK(staircase.status == s::SolveStatus::Solved);
    CHECK(staircase.finalSnapshot.windows[1].visualRect.left == 452);
    CHECK(staircase.finalSnapshot.windows[1].visualRect.top == 452);
    CHECK(staircase.finalSnapshot.windows[2].visualRect.left == 404);
    CHECK(staircase.finalSnapshot.windows[2].visualRect.top == 404);
    const auto stable = s::solve_title_bar_layout(staircase.finalSnapshot, p, 0);
    CHECK(stable.moves.empty());
    roomy.windows.push_back(make_window(4, {500, 500, 2700, 1700}));
    p.limits.maximumElapsedMs = 1;
    Clock suffix_clock;
    const auto two_prefix = s::solve_title_bar_layout(roomy, p, 0, &suffix_clock);
    CHECK(two_prefix.status == s::SolveStatus::PartiallySolved);
    CHECK(two_prefix.moves.size() == 2);
    CHECK(two_prefix.moves[0].window == roomy.windows[1].key);
    CHECK(two_prefix.moves[1].window == roomy.windows[2].key);
    CHECK(two_prefix.finalSnapshot.windows[2].visualRect.left == 404);
    CHECK(two_prefix.finalSnapshot.windows[3].placementRect == roomy.windows[3].placementRect);
    // A lower window's old visible title must not veto the higher prefix.
    s::LayoutSnapshot priority{{}, {
        make_window(1, {500, 500, 1100, 900}),
        make_window(2, {500, 500, 1100, 900}),
        make_window(3, {452, 452, 1052, 852})}};
    p.limits.maximumElapsedMs = 1000;
    const auto prioritized = s::solve_title_bar_layout(priority, p, 0);
    CHECK(prioritized.finalSnapshot.windows[1].visualRect.left == 452);
    CHECK(prioritized.finalSnapshot.windows[1].visualRect.top == 452);
    CHECK(prioritized.finalSnapshot.windows[2].visualRect.left == 404);
    CHECK(prioritized.finalSnapshot.windows[2].visualRect.top == 404);
    CHECK(prioritized.violations.empty());
    // Every output geometry must come from replaying precisely the returned moves.
    auto replay = priority;
    for (const auto& move : prioritized.moves) {
        for (auto& w : replay.windows) {
            if (w.key != move.window) continue;
            CHECK(w.placementRect == move.from);
            w.visualRect = *w.visualRect.translated(move.to.left - move.from.left,
                move.to.top - move.from.top);
            w.placementRect = move.to;
        }
    }
    for (std::size_t i = 0; i < replay.windows.size(); ++i) {
        CHECK(replay.windows[i].placementRect == prioritized.finalSnapshot.windows[i].placementRect);
        CHECK(replay.windows[i].visualRect == prioritized.finalSnapshot.windows[i].visualRect);
    }
    auto distant = priority;
    distant.windows[1] = make_window(2, {900, 900, 1500, 1300});
    const auto relocated = s::solve_title_bar_layout(distant, p, 0);
    CHECK(relocated.finalSnapshot.windows[1].visualRect.left == 452);
    CHECK(relocated.finalSnapshot.windows[1].visualRect.top == 452);
    CHECK(relocated.finalSnapshot.windows[2].visualRect.left == 404);
    CHECK(relocated.finalSnapshot.windows[2].visualRect.top == 404);
    CHECK(relocated.violations.empty());
    // Already visible peers must also join the same chain, including reconcile.
    auto visible = priority;
    visible.windows[1] = make_window(2, {100, 100, 700, 450});
    visible.windows[2] = make_window(3, {800, 100, 1400, 450});
    p.optimizeTitleBarLayout = false;
    CHECK(s::scan_visibility_violations(visible, p.ranking.visibility).violations.empty());
    const auto unified = s::solve_title_bar_layout(visible, p, 0);
    CHECK(unified.finalSnapshot.windows[1].visualRect.left == 452);
    CHECK(unified.finalSnapshot.windows[2].visualRect.left == 404);
    CHECK(s::solve_title_bar_layout(unified.finalSnapshot, p, 0).moves.empty());
    auto obstructed = priority;
    auto blocker = make_window(9, {0, 0, 1200, 490});
    blocker.zIndex = 0;
    blocker.managed = blocker.movable = false;
    obstructed.windows.push_back(blocker);
    const auto blocked = s::solve_title_bar_layout(obstructed, p, 0);
    CHECK(blocked.moves.empty());
    CHECK(std::string_view(blocked.stopReason) == "staircase_blocked");
    CHECK(blocked.finalSnapshot.windows[1].placementRect == obstructed.windows[1].placementRect);
    CHECK(s::solve_title_bar_layout(blocked.finalSnapshot, p, 0).moves.empty());
    auto edge = priority;
    edge.windows[0] = make_window(1, {20, 20, 620, 420});
    const auto outside = s::solve_title_bar_layout(edge, p, 0);
    CHECK(outside.moves.empty());
    CHECK(std::string_view(outside.stopReason) == "staircase_outside_work_area");
    CHECK(outside.finalSnapshot.windows[1].placementRect == edge.windows[1].placementRect);
    auto scaled = priority;
    for (auto& w : scaled.windows) w.dpi = 144;
    p.ranking.visibility.top.depthDip = 48;
    const auto scaled_result = s::solve_title_bar_layout(scaled, p, 0);
    CHECK(scaled_result.violations.empty());
    CHECK(scaled_result.finalSnapshot.windows[1].visualRect.top == 452);
    CHECK(scaled_result.finalSnapshot.windows[2].visualRect.top == 404);
    std::puts("prefix survives suffix timeout; lower and active windows unchanged");
}
