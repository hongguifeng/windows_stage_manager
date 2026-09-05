#include "geometry/work_area.h"
#include "solver/layout_hash.h"
#include "solver/layout_solver.h"
#include "solver/visibility_analyzer.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <span>
#include <vector>

namespace {

constexpr std::uint64_t kSeed = 0x8'02'20'26ULL;
constexpr std::uint32_t kCases = 19;

#define CHECK_CASE(condition, case_index)                                                \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::fprintf(stderr,                                                         \
                         "solver property failed: %s (seed=%llu, case=%u, line=%d)\n", \
                         #condition,                                                     \
                         static_cast<unsigned long long>(kSeed),                         \
                         static_cast<unsigned>(case_index),                              \
                         __LINE__);                                                      \
            return 1;                                                                    \
        }                                                                                \
    } while (false)

stage_manager::solver::LayoutWindow make_window(
    std::uintptr_t hwnd,
    stage_manager::geometry::Rect rectangle,
    std::int32_t z_index)
{
    stage_manager::solver::LayoutWindow window;
    window.key = {hwnd, static_cast<std::uint32_t>(1000 + hwnd), hwnd};
    window.placementRect = rectangle;
    window.visualRect = rectangle;
    window.workArea = {0, 0, 1200, 800};
    window.lastStableRect = rectangle;
    window.monitor = 1;
    window.zIndex = z_index;
    window.managed = true;
    window.movable = true;
    window.visible = true;
    window.blocksVisibility = true;
    window.currentDesktop = true;
    return window;
}

stage_manager::solver::LayoutSnapshot random_layout(std::mt19937_64& random,
                                                     std::uint32_t case_index)
{
    std::uniform_int_distribution<std::int64_t> width_distribution(140, 320);
    std::uniform_int_distribution<std::int64_t> height_distribution(120, 280);
    std::uniform_int_distribution<std::int64_t> x_distribution(0, 880);
    std::uniform_int_distribution<std::int64_t> y_distribution(0, 520);
    std::bernoulli_distribution reuse_previous(0.35);

    stage_manager::solver::LayoutSnapshot snapshot;
    snapshot.version = case_index + 1;
    const auto count = 2 + case_index % 19;
    snapshot.windows.reserve(count);
    for (std::uint32_t index = 0; index < count; ++index) {
        stage_manager::geometry::Rect rectangle;
        if (index != 0 && reuse_previous(random)) {
            rectangle = snapshot.windows[index - 1].placementRect;
        } else {
            const auto width = width_distribution(random);
            const auto height = height_distribution(random);
            const auto left = std::min(x_distribution(random), 1200 - width);
            const auto top = std::min(y_distribution(random), 800 - height);
            rectangle = {left, top, left + width, top + height};
        }
        snapshot.windows.push_back(make_window(
            10'000 + case_index * 20 + index,
            rectangle,
            static_cast<std::int32_t>(index)));
    }
    return snapshot;
}

stage_manager::solver::SolverPolicy property_policy()
{
    stage_manager::solver::SolverPolicy policy;
    const stage_manager::solver::EdgeAffordanceRule edge{48, 48, 24, 100};
    policy.ranking.visibility.top = edge;
    policy.ranking.visibility.left = edge;
    policy.ranking.visibility.right = edge;
    policy.ranking.visibility.bottom = edge;
    policy.ranking.visibility.maximumRegionRectangles = 256;
    policy.ranking.minimumOnscreenWidth = 100;
    policy.ranking.minimumOnscreenHeight = 100;
    policy.ranking.activeWindowIndex = 0;
    policy.limits.maximumMoves = 32;
    policy.limits.maximumStates = 32;
    policy.limits.maximumElapsedMs = 10'000;
    policy.limits.maximumCandidatesPerViolation = 64;
    policy.repairTargetLength = 64;
    return policy;
}

bool same_moves(std::span<const stage_manager::solver::MovePlan> left,
                std::span<const stage_manager::solver::MovePlan> right)
{
    if (left.size() != right.size()) {
        return false;
    }
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (left[index].window != right[index].window ||
            left[index].from != right[index].from ||
            left[index].to != right[index].to) {
            return false;
        }
    }
    return true;
}

} // namespace

int main()
{
    using stage_manager::geometry::preserves_minimum_onscreen;
    using stage_manager::solver::SolveStatus;
    using stage_manager::solver::hash_layout;
    using stage_manager::solver::scan_visibility_violations;
    using stage_manager::solver::solve_layout;

    std::mt19937_64 random(kSeed);
    const auto policy = property_policy();
    for (std::uint32_t case_index = 0; case_index < kCases; ++case_index) {
        const auto initial = random_layout(random, case_index);
        const auto original_hash = hash_layout(initial);
        const auto result = solve_layout(initial, policy);
        const auto replay = solve_layout(initial, policy);

        CHECK_CASE(result.status == replay.status, case_index);
        CHECK_CASE(result.finalState == replay.finalState, case_index);
        CHECK_CASE(result.statesVisited == replay.statesVisited, case_index);
        CHECK_CASE(same_moves(result.moves, replay.moves), case_index);
        CHECK_CASE(result.statesVisited <= policy.limits.maximumStates, case_index);
        CHECK_CASE(result.moves.size() <= policy.limits.maximumMoves, case_index);
        CHECK_CASE(hash_layout(initial) == original_hash, case_index);

        auto simulated = initial;
        std::vector<stage_manager::solver::LayoutHash> states = {original_hash};
        for (const auto& move : result.moves) {
            const auto target = std::find_if(
                simulated.windows.begin(), simulated.windows.end(), [&move](const auto& window) {
                    return window.key == move.window;
                });
            CHECK_CASE(target != simulated.windows.end(), case_index);
            CHECK_CASE(target->placementRect == move.from, case_index);
            CHECK_CASE(move.from != move.to, case_index);
            CHECK_CASE(move.to.width() == move.from.width(), case_index);
            CHECK_CASE(move.to.height() == move.from.height(), case_index);
            const auto delta_x = move.to.left - move.from.left;
            const auto delta_y = move.to.top - move.from.top;
            const auto translated_visual = target->visualRect.translated(delta_x, delta_y);
            CHECK_CASE(translated_visual.has_value(), case_index);
            target->placementRect = move.to;
            target->visualRect = *translated_visual;
            const auto state = hash_layout(simulated);
            CHECK_CASE(std::find(states.begin(), states.end(), state) == states.end(),
                       case_index);
            states.push_back(state);
        }

        if (result.status == SolveStatus::Solved ||
            result.status == SolveStatus::NoViolation) {
            CHECK_CASE(hash_layout(simulated) == result.finalState, case_index);
            const auto scan = scan_visibility_violations(
                result.finalSnapshot, policy.ranking.visibility);
            CHECK_CASE(scan.status ==
                           stage_manager::solver::ViolationScanStatus::Ok,
                       case_index);
            CHECK_CASE(scan.violations.empty(), case_index);
            CHECK_CASE(result.finalSnapshot.windows[0].placementRect ==
                           initial.windows[0].placementRect,
                       case_index);
            for (std::size_t index = 0; index < initial.windows.size(); ++index) {
                const auto& before = initial.windows[index];
                const auto& after = result.finalSnapshot.windows[index];
                CHECK_CASE(after.key == before.key, case_index);
                CHECK_CASE(after.placementRect.width() == before.placementRect.width(),
                           case_index);
                CHECK_CASE(after.placementRect.height() == before.placementRect.height(),
                           case_index);
                CHECK_CASE(after.monitor == before.monitor, case_index);
                CHECK_CASE(after.zIndex == before.zIndex, case_index);
                CHECK_CASE(after.currentDesktop == before.currentDesktop, case_index);
                CHECK_CASE(preserves_minimum_onscreen(
                               after.placementRect,
                               after.workArea,
                               static_cast<std::int64_t>(policy.ranking.minimumOnscreenWidth),
                               static_cast<std::int64_t>(policy.ranking.minimumOnscreenHeight)),
                           case_index);
            }
        } else {
            CHECK_CASE(result.moves.empty(), case_index);
            CHECK_CASE(result.finalState == original_hash, case_index);
        }
    }
    return 0;
}
