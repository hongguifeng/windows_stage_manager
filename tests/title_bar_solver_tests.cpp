#include "solver/layout_solver.h"

#include <cstdio>
#include <random>

#define CHECK(condition) do { if (!(condition)) { \
    std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); return 1; } } while (false)

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
    value.ranking.visibility.left = {96, 96, 24, 25};
    value.ranking.visibility.right = {96, 96, 24, 25};
    value.limits.maximumElapsedMs = 2000;
    return value;
}

class Clock final : public solver::ISolverClock {
public:
    std::uint64_t now_ms() override { return tick++; }
    std::uint64_t tick = 0;
};

bool invariant(const solver::LayoutSnapshot& before, const solver::SolveResult& result, std::size_t active = 0)
{
    if (result.finalSnapshot.windows[active].placementRect != before.windows[active].placementRect) return false;
    for (std::size_t i = 0; i < before.windows.size(); ++i) {
        const auto& a = before.windows[i];
        const auto& b = result.finalSnapshot.windows[i];
        if (a.zIndex != b.zIndex || a.placementRect.width() != b.placementRect.width() ||
            a.placementRect.height() != b.placementRect.height()) return false;
        if (a.placementRect != b.placementRect && !b.workArea.contains(b.placementRect)) return false;
    }
    return true;
}
}

#include "crowded_desktop_fixture.h"
#include "distant_crowded_desktop_fixture.h"

int main()
{
    auto replay_settings = policy();
    replay_settings.ranking.visibility.top = {240, 240, 48, 30};
    replay_settings.ranking.visibility.left = {120, 240, 40, 25};
    replay_settings.ranking.visibility.right = {160, 300, 64, 30};
    replay_settings.limits.maximumElapsedMs = 100;
    const auto desktop = crowded_desktop_fixture();
    const auto replay = solver::solve_layout_prioritized(desktop, replay_settings, 0);
    std::printf("captured desktop: %zu moves, %zu violations, %llu ms\n", replay.moves.size(),
        replay.violations.size(), static_cast<unsigned long long>(replay.elapsedMs));
    CHECK(invariant(desktop, replay));
    CHECK(replay.status == solver::SolveStatus::Solved ||
          replay.status == solver::SolveStatus::PartiallySolved);
    std::printf("captured nearest title: (%lld,%lld), active: (%lld,%lld)\n",
        replay.finalSnapshot.windows[1].visualRect.left, replay.finalSnapshot.windows[1].visualRect.top,
        desktop.windows[0].visualRect.left, desktop.windows[0].visualRect.top);
    CHECK(std::abs(replay.finalSnapshot.windows[1].visualRect.top - 263) <= 2);
    CHECK(std::abs(replay.finalSnapshot.windows[1].visualRect.left - 716) <= 60);
    CHECK(replay.moves.size() > 0);
    auto replay_state = replay.finalSnapshot;
    for (int round = 0; round < 4; ++round) {
        const auto followup = solver::solve_layout_prioritized(replay_state, replay_settings, 0);
        std::printf("captured followup: %zu moves, %zu violations, %llu ms\n", followup.moves.size(),
            followup.violations.size(), static_cast<unsigned long long>(followup.elapsedMs));
        CHECK(invariant(replay_state, followup));
        replay_state = followup.finalSnapshot;
        if (followup.moves.empty() || followup.violations.empty()) break;
    }
    CHECK(solver::scan_visibility_violations(replay_state, replay_settings.ranking.visibility).violations.empty());
    // Aborted joint searches must not publish a temporary layout that hides
    // titles which were recognizable in the captured desktop.
    for (const auto budget : {1u, 4u, 16u}) {
        auto bounded = replay_settings;
        bounded.limits.maximumStates = budget;
        bounded.limits.maximumMoves = 2;
        const auto partial_replay = solver::solve_layout_prioritized(desktop, bounded, 0);
        CHECK(invariant(desktop, partial_replay));
        CHECK(partial_replay.moves.size() <= 2);
        for (std::size_t i = 0; i < desktop.windows.size(); ++i) {
            if (solver::analyze_title_bar_exposure(desktop, i, bounded.ranking.visibility).satisfied())
                CHECK(solver::analyze_title_bar_exposure(partial_replay.finalSnapshot, i, bounded.ranking.visibility).satisfied());
        }
    }
    const auto settings = policy();
    auto crowded_optimize = replay_settings;
    crowded_optimize.optimizeTitleBarLayout = true;
    const auto distant_desktop = distant_crowded_desktop_fixture();
    const auto crowded_nearby = solver::solve_layout_prioritized(distant_desktop, crowded_optimize, 0);
    std::printf("19-window nearest: (%lld,%lld), %zu violations, %llu ms\n",
        crowded_nearby.finalSnapshot.windows[1].visualRect.left,
        crowded_nearby.finalSnapshot.windows[1].visualRect.top, crowded_nearby.violations.size(),
        static_cast<unsigned long long>(crowded_nearby.elapsedMs));
    CHECK(invariant(distant_desktop, crowded_nearby));
    CHECK(crowded_nearby.violations.empty());
    CHECK(crowded_nearby.finalSnapshot.windows[1].visualRect.top == 254);
    CHECK(std::abs(crowded_nearby.finalSnapshot.windows[1].visualRect.left - 841) <= 40);
    const auto activation_desktop = activation_crowded_desktop_fixture();
    const auto activation_nearby = solver::solve_layout_prioritized(activation_desktop, crowded_optimize, 0);
    std::printf("19-window activation nearest: (%lld,%lld), %zu violations, %llu ms\n",
        activation_nearby.finalSnapshot.windows[1].visualRect.left,
        activation_nearby.finalSnapshot.windows[1].visualRect.top, activation_nearby.violations.size(),
        static_cast<unsigned long long>(activation_nearby.elapsedMs));
    CHECK(invariant(activation_desktop, activation_nearby));
    CHECK(activation_nearby.violations.empty());
    CHECK(activation_nearby.finalSnapshot.windows[1].visualRect.top == 254);
    CHECK(std::abs(activation_nearby.finalSnapshot.windows[1].visualRect.left - 841) <= 40);
    CHECK(solver::solve_layout_prioritized(activation_nearby.finalSnapshot, replay_settings, 0).moves.empty());
    for (const auto budget : {1u, 4u, 16u}) {
        auto bounded = crowded_optimize;
        bounded.limits.maximumStates = budget;
        bounded.limits.maximumMoves = 2;
        const auto safe_nearby = solver::solve_layout_prioritized(distant_desktop, bounded, 0);
        CHECK(invariant(distant_desktop, safe_nearby));
        CHECK(safe_nearby.violations.empty());
        CHECK(safe_nearby.moves.size() <= 2);
    }
    const auto& visibility = settings.ranking.visibility;
    // The right half and the left half are not interchangeable.
    solver::LayoutSnapshot halves{{}, {
        window(1, {0, 0, 500, 300}, 0), window(2, {0, 0, 1000, 700}, 1)}};
    CHECK(!solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());
    halves.windows[0].visualRect = {500, 0, 1000, 300};
    CHECK(solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());
    halves.windows[0].visualRect = {499, 31, 1000, 300};
    CHECK(!solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());
    halves.windows[0].visualRect = {0, 47, 1000, 300};
    CHECK(!solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());
    halves.windows[0].visualRect = {0, 48, 1000, 300};
    CHECK(solver::analyze_title_bar_exposure(halves, 1, visibility).satisfied());

    auto height_rules = visibility;
    height_rules.top.depthDip = 48;
    halves.windows[1].titleBarHeight = 31;
    halves.windows[1].titleBarHeightSource = stage_manager::window::TitleBarHeightSource::SystemEstimate;
    CHECK(solver::analyze_title_bar_exposure(halves, 1, height_rules).protectedHeight == 48);
    halves.windows[1].titleBarHeight = 32;
    CHECK(solver::analyze_title_bar_exposure(halves, 1, height_rules).protectedHeight == 48);
    halves.windows[1].dpi = 144;
    halves.windows[1].titleBarHeight = 48;
    CHECK(solver::analyze_title_bar_exposure(halves, 1, height_rules).protectedHeight == 72);
    halves.windows[1].titleBarHeight = 0;
    CHECK(solver::analyze_title_bar_exposure(halves, 1, height_rules).protectedHeight == 72);
    height_rules.top.depthDip = 800;
    CHECK(!solver::analyze_title_bar_exposure(halves, 1, height_rules).satisfied());

    solver::LayoutSnapshot chain;
    for (std::uintptr_t i = 0; i <= 10; ++i)
        chain.windows.push_back(window(i + 1, {460, 500, 1460, 1000}, static_cast<int>(i)));
    const auto arranged = solver::solve_layout_prioritized(chain, settings, 0);
    CHECK(arranged.status == solver::SolveStatus::Solved);
    CHECK(arranged.moves.size() == 10);
    CHECK(invariant(chain, arranged));
    auto previous = chain.windows[0].visualRect;
    for (std::size_t i = 1; i < chain.windows.size(); ++i) {
        const auto& rect = arranged.finalSnapshot.windows[i].visualRect;
        CHECK(solver::analyze_title_bar_exposure(arranged.finalSnapshot, i, visibility).satisfied());
        // Older titles may align vertically: a side strip must not push the
        // whole chain progressively farther from the active title origin.
        CHECK(std::abs(rect.left - chain.windows[0].visualRect.left) <= 24);
        CHECK(rect.top + 48 <= previous.top);
        previous = rect;
    }
    const auto stable = solver::solve_layout_prioritized(arranged.finalSnapshot, settings, 0);
    CHECK(stable.status == solver::SolveStatus::NoViolation);
    CHECK(stable.moves.empty());

    auto optimize = settings;
    optimize.optimizeTitleBarLayout = true;
    // Already readable is not the same as nearby. Width differences must not
    // attract the title to the active window's far right edge.
    solver::LayoutSnapshot distant{{}, {
        window(1, {400, 400, 1600, 950}, 0),
        window(2, {1200, 352, 1800, 852}, 1),
        window(3, {100, 100, 900, 650}, 2)}};
    CHECK(solver::scan_visibility_violations(distant, visibility).violations.empty());
    CHECK(solver::solve_layout_prioritized(distant, settings, 0).moves.empty());
    const auto nearby = solver::solve_layout_prioritized(distant, optimize, 0);
    CHECK(invariant(distant, nearby));
    CHECK(nearby.violations.empty());
    CHECK(nearby.finalSnapshot.windows[1].visualRect.top == 352);
    CHECK(std::abs(nearby.finalSnapshot.windows[1].visualRect.left - 400) <= 24);
    const auto nearby_repeat = solver::solve_layout_prioritized(nearby.finalSnapshot, optimize, 0);
    for (std::size_t i = 1; i < distant.windows.size(); ++i)
        std::printf("nearby rank%zu: (%lld,%lld) -> (%lld,%lld)\n", i,
            nearby.finalSnapshot.windows[i].visualRect.left, nearby.finalSnapshot.windows[i].visualRect.top,
            nearby_repeat.finalSnapshot.windows[i].visualRect.left, nearby_repeat.finalSnapshot.windows[i].visualRect.top);
    CHECK(nearby_repeat.moves.empty());

    // On repeated A/B activation, recenter the newly active window as the
    // coordinator does. Its predecessor must be the closest upper title.
    auto switching = nearby.finalSnapshot;
    for (std::size_t round = 0; round < 8; ++round) {
        const std::size_t active = (round + 1) % 2;
        const std::size_t previous_active = 1 - active;
        for (std::size_t i = 0; i < switching.windows.size(); ++i)
            switching.windows[i].zIndex = i == active ? 0 : i == previous_active ? 1 : 2;
        auto& foreground = switching.windows[active];
        foreground.visualRect = *foreground.visualRect.translated(400 - foreground.visualRect.left,
            400 - foreground.visualRect.top);
        foreground.placementRect = foreground.visualRect;
        const auto switched = solver::solve_layout_prioritized(switching, optimize, active);
        CHECK(invariant(switching, switched, active));
        CHECK(switched.violations.empty());
        CHECK(switched.finalSnapshot.windows[previous_active].visualRect.top == 352);
        CHECK(std::abs(switched.finalSnapshot.windows[previous_active].visualRect.left - 400) <= 24);
        CHECK(solver::solve_layout_prioritized(switched.finalSnapshot, optimize, active).moves.empty());
        switching = switched.finalSnapshot;
    }

    // There is no row above a title at the work-area top. Use an on-screen
    // side opening, not a clipped title or an attractive point down the body.
    solver::LayoutSnapshot top_edge{{}, {
        window(1, {500, 0, 1200, 700}, 0), window(2, {500, 0, 1100, 500}, 1)}};
    const auto beside = solver::solve_layout_prioritized(top_edge, optimize, 0);
    CHECK(invariant(top_edge, beside));
    CHECK(beside.violations.empty());
    CHECK(beside.finalSnapshot.windows[1].visualRect.top == 0);

    // Unmanaged higher blockers can precede the actual active window in the
    // snapshot; array index zero is not an active-window marker.
    solver::LayoutSnapshot nonzero_active{{}, {
        window(3, {0, 0, 100, 80}, 0),
        window(1, {400, 400, 1600, 950}, 1),
        window(2, {1200, 352, 1800, 852}, 2)}};
    nonzero_active.windows[0].managed = nonzero_active.windows[0].movable = false;
    const auto indexed = solver::solve_layout_prioritized(nonzero_active, optimize, 1);
    CHECK(invariant(nonzero_active, indexed, 1));
    CHECK(indexed.violations.empty());
    CHECK(indexed.finalSnapshot.windows[0].placementRect == nonzero_active.windows[0].placementRect);
    CHECK(indexed.finalSnapshot.windows[2].visualRect.top == 352);
    CHECK(std::abs(indexed.finalSnapshot.windows[2].visualRect.left - 400) <= 24);

    // Left edge blocks a left staircase; the right candidate must expose the
    // LEFT title prefix despite its right-hand placement.
    solver::LayoutSnapshot right{{}, {
        window(1, {0, 100, 700, 800}, 0), window(2, {0, 100, 700, 800}, 1)}};
    const auto right_result = solver::solve_layout_prioritized(right, settings, 0);
    CHECK(right_result.status == solver::SolveStatus::Solved);
    CHECK(invariant(right, right_result));
    CHECK(right_result.finalSnapshot.windows[1].visualRect.left > 0);
    CHECK(solver::analyze_title_bar_exposure(right_result.finalSnapshot, 1, visibility).rightSide);

    solver::LayoutSnapshot branches;
    for (std::uintptr_t i = 0; i <= 4; ++i)
        branches.windows.push_back(window(i + 1, {120, 120, 720, 820}, static_cast<int>(i)));
    const auto branched = solver::solve_layout_prioritized(branches, settings, 0);
    CHECK(branched.status == solver::SolveStatus::Solved);
    CHECK(invariant(branches, branched));
    bool found_left = false;
    bool found_right = false;
    long double last_distance = 0;
    for (std::size_t i = 1; i < branches.windows.size(); ++i) {
        const auto& rect = branched.finalSnapshot.windows[i].visualRect;
        CHECK(solver::analyze_title_bar_exposure(branched.finalSnapshot, i, visibility).satisfied());
        found_left = found_left || rect.left < 120;
        found_right = found_right || rect.left > 120;
        const auto dx = std::max(0.0L, std::abs(static_cast<long double>(rect.left) - 120) - 24);
        const auto dy = static_cast<long double>(rect.top) - 120;
        const auto distance = dx * dx + dy * dy;
        CHECK(distance >= last_distance);
        last_distance = distance;
    }
    CHECK(found_left && found_right);

    // An impossible large peer must not prevent an independently placeable
    // small lower peer from becoming recognizable.
    solver::LayoutSnapshot partial{{}, {
        window(1, {0, 0, 1000, 700}, 0),
        window(2, {0, 0, 1920, 1040}, 1),
        window(3, {0, 0, 200, 200}, 2)}};
    partial.windows[1].visualRect = partial.windows[1].placementRect = {0, 0, 1000, 1040};
    partial.windows[1].lastStableRect = partial.windows[1].placementRect;
    partial.windows[1].movable = false;
    const auto partial_result = solver::solve_layout_prioritized(partial, settings, 0);
    CHECK(partial_result.status == solver::SolveStatus::PartiallySolved);
    CHECK(invariant(partial, partial_result));
    CHECK(solver::analyze_title_bar_exposure(partial_result.finalSnapshot, 2, visibility).satisfied());
    CHECK(!solver::analyze_title_bar_exposure(partial_result.finalSnapshot, 1, visibility).satisfied());

    solver::LayoutSnapshot full{{}, {
        window(1, {0, 0, 1920, 1040}, 0), window(2, {100, 100, 1000, 800}, 1)}};
    const auto impossible = solver::solve_layout_prioritized(full, settings, 0);
    CHECK(impossible.status == solver::SolveStatus::Unsatisfiable);
    CHECK(impossible.moves.empty());
    auto no_moves = settings;
    no_moves.limits.maximumMoves = 0;
    CHECK(solver::solve_layout_prioritized(chain, no_moves, 0).moves.empty());
    auto timed = settings;
    timed.limits.maximumElapsedMs = 1;
    Clock clock;
    const auto timeout = solver::solve_layout_prioritized(chain, timed, 0, &clock);
    CHECK(timeout.status == solver::SolveStatus::Timeout);
    CHECK(timeout.moves.empty());

    // A covered low-Z peer must get the only state, not lose the budget to
    // optimizing already recognizable higher windows.
    solver::LayoutSnapshot crowded;
    crowded.windows.push_back(window(1, {500, 400, 1400, 1000}, 0));
    for (int i = 1; i < 10; ++i)
        crowded.windows.push_back(window(i + 1, {(i - 1) * 180, 0, (i - 1) * 180 + 160, 150}, i));
    crowded.windows.push_back(window(11, {500, 400, 1400, 1000}, 10));
    auto limited = settings;
    limited.limits.maximumStates = 1;
    const auto repaired = solver::solve_layout_prioritized(crowded, limited, 0);
    CHECK(repaired.moves.size() == 1);
    CHECK(repaired.moves[0].window.hwnd == 11);
    CHECK(repaired.status == solver::SolveStatus::Solved);
    CHECK(invariant(crowded, repaired));

    limited = settings;
    limited.limits.maximumStates = 1;
    limited.limits.maximumMoves = 1;
    const auto saved = solver::solve_layout_prioritized(chain, limited, 0);
    CHECK(saved.status == solver::SolveStatus::PartiallySolved);
    CHECK(saved.moves.size() == 1);
    CHECK(invariant(chain, saved));
    timed.limits.maximumElapsedMs = 12;
    Clock repair_clock;
    const auto timed_repair = solver::solve_layout_prioritized(chain, timed, 0, &repair_clock);
    CHECK(timed_repair.status == solver::SolveStatus::PartiallySolved);
    CHECK(!timed_repair.moves.empty());
    CHECK(invariant(chain, timed_repair));

    // Different DPI and invisible frame offsets must preserve dimensions and
    // both rectangles' translation, including on a negative-coordinate monitor.
    solver::LayoutSnapshot framed{{}, {
        window(1, {-1000, 300, -200, 1000}, 0), window(2, {-1000, 300, -200, 1000}, 1)}};
    for (auto& item : framed.windows) item.workArea = {-1920, 0, 0, 1040};
    framed.windows[1].placementRect = {-1008, 292, -192, 1008};
    framed.windows[1].dpi = 144;
    framed.windows[1].titleBarHeight = 48;
    const auto frame_result = solver::solve_layout_prioritized(framed, settings, 0);
    CHECK(frame_result.status == solver::SolveStatus::Solved);
    CHECK(invariant(framed, frame_result));
    CHECK(frame_result.finalSnapshot.windows[1].visualRect.left -
          frame_result.finalSnapshot.windows[1].placementRect.left == 8);

    // Reproducible mixed-size arrangements: safety and truthful success even
    // when search finds only a partial layout. Previously visible titles must
    // remain visible after moving higher windows.
    std::mt19937 random(20260906);
    for (int scenario = 0; scenario < 40; ++scenario) {
        solver::LayoutSnapshot mixed;
        for (int i = 0; i < 7; ++i) {
            const auto width = 200 + random() % 900;
            const auto height = 180 + random() % 620;
            const auto x = random() % (1921 - width);
            const auto y = random() % (1041 - height);
            mixed.windows.push_back(window(static_cast<std::uintptr_t>(i + 1),
                {x, y, x + width, y + height}, i));
        }
        const auto solved = solver::solve_layout_prioritized(mixed, settings, 0);
        CHECK(invariant(mixed, solved));
        const auto checked = solver::scan_visibility_violations(solved.finalSnapshot, visibility);
        CHECK(checked.status == solver::ViolationScanStatus::Ok);
        CHECK(checked.violations.size() == solved.violations.size());
        for (std::size_t i = 0; i < mixed.windows.size(); ++i) {
            if (solver::analyze_title_bar_exposure(mixed, i, visibility).satisfied())
                CHECK(solver::analyze_title_bar_exposure(solved.finalSnapshot, i, visibility).satisfied());
        }
        if (solved.status == solver::SolveStatus::Solved) CHECK(checked.violations.empty());
    }
    auto interactive = settings;
    interactive.limits.maximumElapsedMs = 16;
    const auto quick = solver::solve_layout_prioritized(chain, interactive, 0);
    CHECK(invariant(chain, quick));
    std::printf("10-peer scenario: %zu/%zu moves, %llu ms; 16ms budget: %zu moves, %llu ms\n",
        arranged.moves.size(), chain.windows.size() - 1,
        static_cast<unsigned long long>(arranged.elapsedMs), quick.moves.size(),
        static_cast<unsigned long long>(quick.elapsedMs));
    return 0;
}
