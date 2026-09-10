#include "solver/layout_solver.h"

#include "geometry/dpi.h"
#include "geometry/work_area.h"

#include <algorithm>
#include <chrono>
#include <limits>

namespace stage_manager::solver {
namespace {

class TitleClock final : public ISolverClock {
public:
    std::uint64_t now_ms() override
    {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
};

// Windows coordinates fit LONG. Validate before doing candidate arithmetic;
// int64 intermediates then safely cover offsets, widths and border differences.
bool native_rect(const geometry::Rect& rect)
{
    return !rect.empty() && rect.left >= INT32_MIN && rect.top >= INT32_MIN &&
        rect.right <= INT32_MAX && rect.bottom <= INT32_MAX &&
        rect.width() <= INT32_MAX && rect.height() <= INT32_MAX;
}

} // namespace

SolveResult solve_title_bar_layout(const LayoutSnapshot& initial, const SolverPolicy& policy,
                                  std::size_t active, ISolverClock* supplied_clock)
{
    TitleClock default_clock;
    auto& clock = supplied_clock ? *supplied_clock : static_cast<ISolverClock&>(default_clock);
    const auto start = clock.now_ms();
    SolveResult result;
    result.finalSnapshot = initial;
    const auto finish = [&]() {
        result.finalState = hash_layout(result.finalSnapshot);
        const auto now = clock.now_ms();
        result.elapsedMs = now >= start ? now - start : 0;
        return result;
    };
    if (active >= initial.windows.size() || policy.limits.maximumStates == 0 ||
        policy.limits.maximumElapsedMs == 0 || policy.limits.maximumCandidatesPerViolation == 0 ||
        policy.ranking.visibility.goal != VisibilityGoal::TitleBarLeftHalf ||
        std::any_of(initial.windows.begin(), initial.windows.end(), [](const auto& window) {
            return !native_rect(window.visualRect) || !native_rect(window.placementRect) ||
                   !native_rect(window.workArea);
        })) return finish();
    const auto scan = scan_visibility_violations(initial, policy.ranking.visibility);
    if (scan.status != ViolationScanStatus::Ok) return finish();
    result.violations = scan.violations;
    std::vector<std::size_t> targets;
    for (std::size_t i = 0; i < initial.windows.size(); ++i) {
        const auto& window = initial.windows[i];
        if (i != active && window.managed && window.visible && window.currentDesktop)
            targets.push_back(i);
    }
    std::stable_sort(targets.begin(), targets.end(), [&](auto a, auto b) {
        return initial.windows[a].zIndex < initial.windows[b].zIndex;
    });
    {
        auto& arranged = result.finalSnapshot;
        auto anchor = initial.windows[active].visualRect;
        for (const auto index : targets) {
            const auto original = arranged.windows[index];
            result.firstUnresolved = original.key;
            result.stopReason = "candidate_exhausted";
            const auto step = (3 * static_cast<std::int64_t>(
                std::max(1u, original.titleBarHeight)) + 1) / 2;
            const auto desired = original.placementRect.translated(
                anchor.left - step - original.visualRect.left,
                anchor.top - step - original.visualRect.top);
            const auto accept = [&](const geometry::Rect& placement) {
                if (!original.workArea.contains(placement) ||
                    (!original.movable && placement != original.placementRect)) return false;
                auto& candidate = arranged.windows[index];
                candidate = original;
                candidate.placementRect = placement;
                candidate.visualRect = *original.visualRect.translated(
                    placement.left - original.placementRect.left,
                    placement.top - original.placementRect.top);
                bool safe = analyze_title_bar_exposure(arranged, index,
                    policy.ranking.visibility).satisfied();
                // Higher windows and the active window are immutable blockers.
                for (std::size_t other = 0; other < arranged.windows.size(); ++other) {
                    if (other == index || !arranged.windows[other].managed ||
                        arranged.windows[other].zIndex > original.zIndex) continue;
                    if (!analyze_title_bar_exposure(arranged, other, policy.ranking.visibility).satisfied())
                        safe = false;
                }
                if (!safe) candidate = original;
                return safe;
            };
            // Spacing is relative to the preceding target, never to this
            // window's old position. Reconcile uses exactly the same chain.
            if (!desired || !original.workArea.contains(*desired)) {
                result.stopReason = "staircase_outside_work_area";
                break;
            }
            if (!accept(*desired)) {
                result.stopReason = original.movable ? "staircase_blocked" : "window_fixed";
                break;
            }
            const auto& placed = arranged.windows[index];
            if (placed.placementRect != original.placementRect) {
                if (result.moves.size() >= policy.limits.maximumMoves) {
                    arranged.windows[index] = original;
                    result.stopReason = "move_limit";
                    break;
                }
                result.moves.push_back({original.key, original.placementRect, placed.placementRect, {}});
            }
            anchor = placed.visualRect;
            ++result.acceptedPrefixCount;
            result.firstUnresolved.reset();
            result.stopReason = "none";
        }
        result.violations = scan_visibility_violations(arranged, policy.ranking.visibility).violations;
        result.status = result.violations.empty()
            ? (result.moves.empty() ? SolveStatus::NoViolation : SolveStatus::Solved)
            : (result.moves.empty() ? SolveStatus::Unsatisfiable : SolveStatus::PartiallySolved);
        return finish();
    }
}

} // namespace stage_manager::solver
