#include "solver/layout_solver.h"

#include "geometry/dpi.h"
#include "geometry/work_area.h"

#include <algorithm>
#include <chrono>
#include <functional>
#include <limits>
#include <numeric>
#include <tuple>

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

struct Quality {
    std::size_t satisfied = 0;
    std::vector<bool> recognized;
    std::size_t inversions = 0;
    std::size_t sides = 0;
    std::vector<unsigned> directions;
    std::vector<long double> distances;
    std::uint64_t extraTitle = 0;
    std::size_t moves = 0;
    long double travel = 0;
};

bool better(const Quality& a, const Quality& b)
{
    if (a.satisfied != b.satisfied) return a.satisfied > b.satisfied;
    if (a.recognized != b.recognized) return a.recognized > b.recognized;
    // Lexicographic real Z-order: do not send the most recent peer away just
    // to improve aggregate side exposure or the ordering of older peers.
    if (a.distances != b.distances) return a.distances < b.distances;
    if (a.inversions != b.inversions) return a.inversions < b.inversions;
    if (a.sides != b.sides) return a.sides > b.sides;
    if (a.directions != b.directions) return a.directions < b.directions;
    if (a.extraTitle != b.extraTitle) return a.extraTitle > b.extraTitle;
    if (a.moves != b.moves) return a.moves < b.moves;
    return a.travel < b.travel;
}

long double title_distance(const LayoutWindow& target, const geometry::Rect& active,
                           const VisibilityRequirements& requirements)
{
    const auto rules = resolve_edge_affordances(target, requirements);
    // Compare title origins, not prefix centres to the active window's BODY.
    // A one-strip horizontal offset is free, preserving a useful left/right
    // cue without preferring a far-away right edge on unequal-width windows.
    const auto dx = std::max(0.0L, std::abs(static_cast<long double>(target.visualRect.left) -
        active.left) - rules[0].depth);
    const auto dy = static_cast<long double>(target.visualRect.top) - active.top;
    return dx * dx + dy * dy;
}

Quality quality(const LayoutSnapshot& snapshot, const LayoutSnapshot& initial,
                const std::vector<std::size_t>& targets, std::size_t count,
                std::size_t active, const VisibilityRequirements& requirements)
{
    Quality result;
    for (std::size_t rank = 0; rank < count; ++rank) {
        const auto index = targets[rank];
        const auto& window = snapshot.windows[index];
        const auto exposure = analyze_title_bar_exposure(snapshot, index, requirements);
        const bool satisfied = exposure.satisfied();
        result.recognized.push_back(satisfied);
        result.satisfied += satisfied ? 1u : 0u;
        result.sides += satisfied && (exposure.leftSide || exposure.rightSide) ? 1u : 0u;
        const bool above = window.visualRect.top < snapshot.windows[active].visualRect.top;
        result.directions.push_back(!satisfied ? 6u : above && exposure.leftSide ? 0u :
            above && exposure.rightSide ? 1u : above ? 2u : exposure.leftSide ? 3u :
            exposure.rightSide ? 4u : 5u);
        const auto distance = title_distance(window, snapshot.windows[active].visualRect,
                                              requirements);
        for (std::size_t prior = 0; prior < rank; ++prior) {
            if (satisfied && result.recognized[prior] &&
                result.distances[prior] > distance) ++result.inversions;
        }
        result.distances.push_back(distance);
        result.extraTitle += exposure.visibleWidth;
        const auto& from = initial.windows[index].placementRect;
        if (from != window.placementRect) {
            ++result.moves;
            result.travel += std::abs(static_cast<long double>(from.left) - window.placementRect.left) +
                             std::abs(static_cast<long double>(from.top) - window.placementRect.top);
        }
    }
    return result;
}

bool higher_blocker(const LayoutWindow& target, const LayoutWindow& blocker)
{
    return blocker.visible && blocker.currentDesktop && blocker.blocksVisibility &&
        blocker.zIndex >= 0 && blocker.zIndex < target.zIndex &&
        (target.monitor == 0 || target.monitor == blocker.monitor);
}

// Windows coordinates fit LONG. Validate before doing candidate arithmetic;
// int64 intermediates then safely cover offsets, widths and border differences.
bool native_rect(const geometry::Rect& rect)
{
    return !rect.empty() && rect.left >= INT32_MIN && rect.top >= INT32_MIN &&
        rect.right <= INT32_MAX && rect.bottom <= INT32_MAX &&
        rect.width() <= INT32_MAX && rect.height() <= INT32_MAX;
}

std::vector<geometry::Rect> placements(const LayoutSnapshot& snapshot, std::size_t index,
                                      std::size_t active, const SolverPolicy& policy, bool row_pack = false,
                                      const std::function<bool()>& expired = {})
{
    const auto& target = snapshot.windows[index];
    const auto& visual = target.visualRect;
    const auto& work = target.workArea;
    const auto rules = resolve_edge_affordances(target, policy.ranking.visibility);
    const auto width = static_cast<std::int64_t>(visual.width());
    const auto height = static_cast<std::int64_t>(rules[2].depth);
    const auto prefix = static_cast<std::int64_t>(std::max(
        visual.width() / 2 + visual.width() % 2, rules[2].length));
    const auto left_step = static_cast<std::int64_t>(rules[0].depth);
    const auto right_step = static_cast<std::int64_t>(rules[1].depth);
    const auto min_x = work.left + visual.left - target.placementRect.left;
    const auto max_x = work.right - static_cast<std::int64_t>(target.placementRect.width()) +
        visual.left - target.placementRect.left;
    const auto min_y = work.top + visual.top - target.placementRect.top;
    const auto max_y = work.bottom - static_cast<std::int64_t>(target.placementRect.height()) +
        visual.top - target.placementRect.top;
    std::vector<geometry::Rect> result{target.placementRect};
    if (!target.movable || min_x > max_x || min_y > max_y) return result;
    const auto add = [&](std::int64_t x, std::int64_t y) {
        if (x < min_x || x > max_x || y < min_y || y > max_y) return;
        if (row_pack) {
            const geometry::Rect title{x, y, x + prefix, y + height};
            // A dense grid may have hundreds of impossible lower-row points.
            // Do not let those consume the cap before reaching usable upper
            // rows. Full exposure and global safety are still checked later.
            if (std::any_of(snapshot.windows.begin(), snapshot.windows.end(), [&](const auto& blocker) {
                return higher_blocker(target, blocker) && title.intersects(blocker.visualRect);
            })) return;
        }
        const auto placement = target.placementRect.translated(x - visual.left, y - visual.top);
        if (placement && geometry::fully_within_work_area(*placement, work) &&
            std::find(result.begin(), result.end(), *placement) == result.end()) {
            result.push_back(*placement);
        }
    };
    add(target.lastStableRect.left + visual.left - target.placementRect.left,
        target.lastStableRect.top + visual.top - target.placementRect.top);
    std::vector<std::int64_t> xs{min_x, max_x,
        std::clamp(visual.left, min_x, max_x)};
    std::vector<std::int64_t> ys{min_y, max_y,
        std::clamp(visual.top, min_y, max_y)};
    const auto append_unique = [](auto& values, auto value) {
        if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
    };
    const auto anchor_candidates = [&](const geometry::Rect& anchor) {
        add(anchor.left - left_step, anchor.top - height);
        add(anchor.left, anchor.top - height);
        add(anchor.left + left_step, anchor.top - height);
        add(anchor.right + right_step - width, anchor.top - height);
        // Exact forbidden-rectangle boundaries, including side/bottom exits.
        for (const auto x : {anchor.left - prefix, anchor.left, anchor.right,
                             anchor.left - left_step, anchor.left + left_step,
                             anchor.right + right_step - width}) {
            if (x >= min_x && x <= max_x) append_unique(xs, x);
        }
        for (const auto y : {anchor.top - height, anchor.top, anchor.bottom}) {
            if (y >= min_y && y <= max_y) append_unique(ys, y);
        }
    };
    anchor_candidates(snapshot.windows[active].visualRect);
    for (const auto& blocker : snapshot.windows) {
        if (higher_blocker(target, blocker)) anchor_candidates(blocker.visualRect);
    }
    // A higher window must also be able to stop exactly beside a lower
    // window's protected title. Higher-blocker boundaries alone miss these.
    // Row reconstruction will place the lower windows afterwards. Their old
    // title boundaries must not crowd the useful higher-window grid out of
    // the candidate cap. Local repair still needs those protection contacts;
    // rebuilt layouts retain the same final global safety validation.
    for (std::size_t other = 0; !row_pack && other < snapshot.windows.size(); ++other) {
        const auto& lower = snapshot.windows[other];
        if (!lower.managed || !higher_blocker(lower, target)) continue;
        const auto exposure = analyze_title_bar_exposure(snapshot, other, policy.ranking.visibility);
        if (!exposure.satisfied()) continue;
        for (const auto x : {lower.visualRect.left - width,
                lower.visualRect.left + static_cast<std::int64_t>(exposure.requiredWidth)})
            if (x >= min_x && x <= max_x) append_unique(xs, x);
        for (const auto y : {lower.visualRect.top - static_cast<std::int64_t>(visual.height()),
                lower.visualRect.top + static_cast<std::int64_t>(exposure.protectedHeight)})
            if (y >= min_y && y <= max_y) append_unique(ys, y);
    }
    // Cross branch boundaries, rather than forcing the right branch outside
    // the bounding box of every earlier window. Each is verified below.
    const auto cap = std::min<std::size_t>(policy.limits.maximumCandidatesPerViolation, 512);
    if (row_pack) {
        std::sort(xs.begin(), xs.end(), std::greater<>{});
        std::sort(ys.begin(), ys.end(), std::greater<>{});
    }
    if (result.size() > cap) result.resize(cap);
    std::size_t grid_points = 0;
    for (const auto y : ys) {
        for (const auto x : xs) {
            if (++grid_points % 64 == 0 && expired && expired()) return result;
            if (result.size() >= cap) return result;
            add(x, y);
        }
    }
    if (result.size() > cap) result.resize(cap);
    return result;
}

struct Node {
    LayoutSnapshot snapshot;
    Quality score;
};

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
    if (scan.violations.empty() && !policy.optimizeTitleBarLayout) {
        result.status = SolveStatus::NoViolation;
        return finish();
    }
    const auto baseline = quality(initial, initial, targets, targets.size(), active,
                                   policy.ranking.visibility);
    auto best = baseline;
    std::vector<Node> beam{{initial, {}}};
    bool exhausted = false;
    const auto out_of_time = [&] {
        const auto now = clock.now_ms();
        return now >= start && now - start >= policy.limits.maximumElapsedMs;
    };

    // Repair first: only inspect violated windows, with hidden title prefixes
    // first, and commit each safe move immediately to the in-memory result.
    // A later expensive search cannot erase this fallback on timeout.
    auto repair = initial;
    auto repair_order = targets;
    std::stable_sort(repair_order.begin(), repair_order.end(), [&](auto a, auto b) {
        return analyze_title_bar_exposure(initial, a, policy.ranking.visibility).visibleWidth == 0 &&
               analyze_title_bar_exposure(initial, b, policy.ranking.visibility).visibleWidth != 0;
    });
    std::size_t repair_moves = 0;
    for (const auto index : repair_order) {
        const auto& requirements = policy.ranking.visibility;
        if (analyze_title_bar_exposure(repair, index, requirements).satisfied()) continue;
        const auto now = clock.now_ms();
        if (result.statesVisited >= policy.limits.maximumStates ||
            now - start >= std::max<std::uint64_t>(1, policy.limits.maximumElapsedMs / 2)) break;
        if (repair_moves >= policy.limits.maximumMoves) break;
        ++result.statesVisited;
        const auto original = repair.windows[index];
        const auto candidates = placements(repair, index, active, policy);
        std::vector<geometry::Rect> protected_rects;
        for (std::size_t other = 0; other < repair.windows.size(); ++other) {
            const auto& lower = repair.windows[other];
            if (!lower.managed || !higher_blocker(lower, original)) continue;
            const auto exposure = analyze_title_bar_exposure(repair, other, requirements);
            if (exposure.satisfied()) protected_rects.push_back({lower.visualRect.left,
                lower.visualRect.top, lower.visualRect.left + static_cast<std::int64_t>(exposure.requiredWidth),
                lower.visualRect.top + static_cast<std::int64_t>(exposure.protectedHeight)});
        }
        std::optional<LayoutWindow> chosen;
        std::tuple<long double, unsigned, long double> chosen_cost;
        for (const auto& placement : candidates) {
            if (out_of_time()) { exhausted = true; break; }
            if (placement == original.placementRect) continue;
            auto& candidate = repair.windows[index];
            candidate = original;
            candidate.placementRect = placement;
            candidate.visualRect = *original.visualRect.translated(
                placement.left - original.placementRect.left, placement.top - original.placementRect.top);
            const auto exposure = analyze_title_bar_exposure(repair, index, requirements);
            if (!exposure.satisfied() || std::any_of(protected_rects.begin(), protected_rects.end(),
                [&](const auto& rect) { return rect.intersects(candidate.visualRect); })) continue;
            const bool above = candidate.visualRect.top < repair.windows[active].visualRect.top;
            const unsigned direction = above && exposure.leftSide ? 0u :
                above && exposure.rightSide ? 1u : above ? 2u : 3u;
            const auto travel = std::abs(static_cast<long double>(placement.left) - original.placementRect.left) +
                                std::abs(static_cast<long double>(placement.top) - original.placementRect.top);
            const auto cost = std::tuple{
                title_distance(candidate, repair.windows[active].visualRect, requirements), direction, travel};
            if (!chosen || cost < chosen_cost) { chosen = candidate; chosen_cost = cost; }
        }
        repair.windows[index] = chosen ? *chosen : original;
        if (chosen) ++repair_moves;
        if (exhausted) break;
    }
    if (repair_moves != 0) {
        result.finalSnapshot = repair;
        best = quality(repair, initial, targets, targets.size(), active, policy.ranking.visibility);
    }
    // Try a bounded set of near-title anchors for the most recent peer. Lower
    // windows may be rearranged in memory, but only a complete safe layout is
    // allowed to replace an already complete fallback.
    std::vector<geometry::Rect> near_anchors;
    if (!targets.empty()) {
        const auto index = targets.front();
        auto trial = initial;
        const auto original = trial.windows[index];
        const bool overlaps = std::any_of(initial.windows.begin(), initial.windows.end(), [&](const auto& other) {
            return higher_blocker(original, other) && original.visualRect.intersects(other.visualRect);
        });
        struct Anchor { geometry::Rect rect; std::tuple<long double, unsigned, long double> cost; };
        std::vector<Anchor> anchors;
        if (overlaps && original.movable && !out_of_time()) {
            for (const auto& placement : placements(initial, index, active, policy)) {
                if (out_of_time()) break;
                auto& candidate = trial.windows[index];
                candidate = original;
                candidate.placementRect = placement;
                candidate.visualRect = *original.visualRect.translated(
                    placement.left - original.placementRect.left, placement.top - original.placementRect.top);
                const auto e = analyze_title_bar_exposure(trial, index, policy.ranking.visibility);
                if (!e.satisfied() || !candidate.workArea.contains(placement)) continue;
                const bool above = candidate.visualRect.top < initial.windows[active].visualRect.top;
                const unsigned direction = above && e.leftSide ? 0u : above && e.rightSide ? 1u : above ? 2u : 3u;
                anchors.push_back({placement, {title_distance(candidate, initial.windows[active].visualRect,
                    policy.ranking.visibility), direction,
                    std::abs(static_cast<long double>(placement.left) - original.placementRect.left) +
                    std::abs(static_cast<long double>(placement.top) - original.placementRect.top)}});
            }
            std::stable_sort(anchors.begin(), anchors.end(), [](const auto& a, const auto& b) { return a.cost < b.cost; });
            for (std::size_t i = 0; i < std::min<std::size_t>(6, anchors.size()); ++i)
                near_anchors.push_back(anchors[i].rect);
        }
    }
    // Reconstruct the overlapping group from the active window, not just from
    // yesterday's staircase. Small title prefixes can use horizontal lanes,
    // reserving scarce rows above the active window for wider title prefixes.
    for (std::size_t attempt = 0; attempt < 4 + near_anchors.size() * 2; ++attempt) {
        const bool anchored = attempt >= 4;
        const auto variant = static_cast<unsigned>(anchored ? (attempt - 4) % 2 : attempt);
        if (!anchored && best.satisfied == targets.size()) continue;
        if (anchored && best.satisfied == targets.size()) {
            auto candidate = initial.windows[targets.front()];
            const auto& placement = near_anchors[(attempt - 4) / 2];
            candidate.visualRect = *candidate.visualRect.translated(
                placement.left - candidate.placementRect.left, placement.top - candidate.placementRect.top);
            // Equal-distance alternatives still matter: one may leave room
            // for the next-most-recent title where another cannot.
            if (title_distance(candidate, initial.windows[active].visualRect,
                policy.ranking.visibility) > best.distances.front()) continue;
        }
        auto rebuilt = initial;
        bool complete = true;
        for (const auto index : targets) {
            if (result.statesVisited >= policy.limits.maximumStates || out_of_time()) { complete = false; break; }
            ++result.statesVisited;
            const auto original = rebuilt.windows[index];
            const auto& before = initial.windows[index];
            const bool participates = std::any_of(initial.windows.begin(), initial.windows.end(), [&](const auto& other) {
                return (higher_blocker(before, other) || (other.managed && higher_blocker(other, before))) &&
                    before.visualRect.intersects(other.visualRect);
            });
            const auto candidates = anchored && index == targets.front()
                ? std::vector<geometry::Rect>{near_anchors[(attempt - 4) / 2]}
                : participates ? placements(rebuilt, index, active, policy, variant < 2, out_of_time)
                : std::vector<geometry::Rect>{original.placementRect};
            std::optional<LayoutWindow> chosen;
            std::tuple<unsigned, long double, long double, long double> best_cost;
            for (const auto& placement : candidates) {
                if (out_of_time()) { complete = false; break; }
                auto& candidate = rebuilt.windows[index];
                candidate = original;
                candidate.placementRect = placement;
                candidate.visualRect = *original.visualRect.translated(
                    placement.left - original.placementRect.left, placement.top - original.placementRect.top);
                const auto exposure = analyze_title_bar_exposure(rebuilt, index, policy.ranking.visibility);
                if (!exposure.satisfied()) continue;
                const auto& anchor = rebuilt.windows[active].visualRect;
                const bool horizontal = candidate.visualRect.left + static_cast<std::int64_t>(exposure.requiredWidth) <= anchor.left ||
                    candidate.visualRect.left >= anchor.right;
                const bool above = candidate.visualRect.top < anchor.top;
                unsigned direction = above && exposure.leftSide ? 0u : above && exposure.rightSide ? 1u : above ? 2u : 3u;
                if (variant % 2 && direction < 2) direction = 1 - direction;
                const unsigned lane = variant < 2 ? (horizontal ? (above ? 1u : 0u) :
                    (candidate.visualRect.left >= anchor.left ? 2u : 3u)) : 0u;
                const auto travel = std::abs(static_cast<long double>(placement.left) - original.placementRect.left) +
                                    std::abs(static_cast<long double>(placement.top) - original.placementRect.top);
                // Row-packing alternative: rightmost higher window first,
                // lower title prefixes fill the same row to its left before
                // consuming another 1.5-title-height row.
                const auto cost = variant < 2
                    ? std::tuple{horizontal ? 0u : (variant == 1 && candidate.visualRect.left < anchor.left ? 2u : 1u),
                        -static_cast<long double>(candidate.visualRect.top),
                        -static_cast<long double>(candidate.visualRect.left), travel}
                    : std::tuple{lane, title_distance(candidate, anchor, policy.ranking.visibility),
                        static_cast<long double>(direction), travel};
                if (!chosen || cost < best_cost) { chosen = candidate; best_cost = cost; }
            }
            rebuilt.windows[index] = chosen ? *chosen : original;
            if (!complete) break;
        }
        if (!complete) break;
        const auto score = quality(rebuilt, initial, targets, targets.size(), active, policy.ranking.visibility);
        bool safe = score.moves <= policy.limits.maximumMoves;
        for (std::size_t i = 0; i < targets.size(); ++i) safe = safe && (!baseline.recognized[i] || score.recognized[i]);
        safe = safe && (!analyze_title_bar_exposure(initial, active, policy.ranking.visibility).satisfied() ||
            analyze_title_bar_exposure(rebuilt, active, policy.ranking.visibility).satisfied());
        if (safe && (score.satisfied > best.satisfied ||
            (score.satisfied == targets.size() && better(score, best)))) {
            repair = rebuilt; result.finalSnapshot = rebuilt; best = score;
        }
    }
    // Displacement-chain search: a trial may cover recognizable lower titles
    // in memory, provided ALL of them are repaired before publishing a plan.
    // Since Z-order is immutable, dependencies point downwards; failed chains
    // backtrack without applying any of their intermediate positions.
    for (const auto root : repair_order) {
        if (analyze_title_bar_exposure(repair, root, policy.ranking.visibility).satisfied()) continue;
        std::vector<bool> required(initial.windows.size(), false);
        for (const auto index : targets)
            required[index] = analyze_title_bar_exposure(repair, index, policy.ranking.visibility).satisfied();
        required[root] = true;
        const auto chain_limit = std::min<std::uint32_t>(policy.limits.maximumStates,
            result.statesVisited + std::max(1u, policy.limits.maximumStates / 3));
        auto trial = repair;
        const auto complete_chain = [&](auto&& self, std::uint32_t local_limit) -> bool {
            if (result.statesVisited >= local_limit || out_of_time()) return false;
            if (clock.now_ms() - start >= policy.limits.maximumElapsedMs * 3 / 4) return false;
            ++result.statesVisited;
            const auto next = std::find_if(targets.begin(), targets.end(), [&](auto index) {
                return required[index] && !analyze_title_bar_exposure(trial, index, policy.ranking.visibility).satisfied();
            });
            if (next == targets.end()) return true;
            const auto index = *next;
            const auto original = trial.windows[index];
            if (!original.movable) return false;
            struct Choice {
                LayoutWindow window;
                std::tuple<std::size_t, long double, unsigned, long double> cost;
            };
            std::vector<Choice> choices;
            const auto candidates = placements(trial, index, active, policy);
            struct ProtectedTitle { geometry::Rect rect; bool hiddenByOthers; bool fixed; };
            std::vector<ProtectedTitle> titles;
            std::size_t other_moves = 0;
            trial.windows[index].blocksVisibility = false;
            for (const auto other : targets) {
                if (other == index) continue;
                const auto& lower = trial.windows[other];
                other_moves += lower.placementRect != initial.windows[other].placementRect;
                if (!required[other] || lower.zIndex <= original.zIndex) continue;
                const auto e = analyze_title_bar_exposure(trial, other, policy.ranking.visibility);
                titles.push_back({{lower.visualRect.left, lower.visualRect.top,
                    lower.visualRect.left + static_cast<std::int64_t>(e.requiredWidth),
                    lower.visualRect.top + static_cast<std::int64_t>(e.protectedHeight)},
                    !e.satisfied(), !lower.movable});
            }
            trial.windows[index] = original;
            for (const auto& placement : candidates) {
                if (out_of_time()) break;
                if (placement == original.placementRect) continue;
                auto& candidate = trial.windows[index];
                candidate = original;
                candidate.placementRect = placement;
                candidate.visualRect = *original.visualRect.translated(
                    placement.left - original.placementRect.left, placement.top - original.placementRect.top);
                const auto exposure = analyze_title_bar_exposure(trial, index, policy.ranking.visibility);
                if (!exposure.satisfied()) continue;
                std::size_t damage = 0;
                bool fixed_damage = false;
                for (const auto& title : titles) {
                    if (title.hiddenByOthers || title.rect.intersects(candidate.visualRect)) {
                        ++damage;
                        fixed_damage = fixed_damage || title.fixed;
                    }
                }
                const auto moves = other_moves + (placement != initial.windows[index].placementRect);
                if (moves > policy.limits.maximumMoves || fixed_damage) continue;
                const bool above = candidate.visualRect.top < trial.windows[active].visualRect.top;
                const unsigned direction = above && exposure.leftSide ? 0u : above && exposure.rightSide ? 1u : above ? 2u : 3u;
                const auto travel = std::abs(static_cast<long double>(placement.left) - original.placementRect.left) +
                                    std::abs(static_cast<long double>(placement.top) - original.placementRect.top);
                Choice choice{candidate, {damage,
                    title_distance(candidate, trial.windows[active].visualRect, policy.ranking.visibility), direction, travel}};
                const auto where = std::find_if(choices.begin(), choices.end(), [&](const auto& item) {
                    return choice.cost < item.cost;
                });
                choices.insert(where, std::move(choice));
                if (choices.size() > 24) choices.pop_back();
            }
            trial.windows[index] = original;
            for (std::size_t branch = 0; branch < choices.size(); ++branch) {
                const auto& choice = choices[branch];
                trial.windows[index] = choice.window;
                // Reserve nodes for alternatives instead of allowing the first
                // deep dead end to consume the entire chain's search budget.
                const auto remaining = local_limit > result.statesVisited ? local_limit - result.statesVisited : 0;
                const auto share = std::max<std::uint32_t>(1, remaining /
                    static_cast<std::uint32_t>(std::min<std::size_t>(choices.size() - branch, 4)));
                if (self(self, std::min(local_limit, result.statesVisited + share))) return true;
                trial.windows[index] = original;
            }
            return false;
        };
        if (complete_chain(complete_chain, chain_limit)) {
            const auto score = quality(trial, initial, targets, targets.size(), active, policy.ranking.visibility);
            const auto active_before = analyze_title_bar_exposure(initial, active, policy.ranking.visibility);
            const auto active_after = analyze_title_bar_exposure(trial, active, policy.ranking.visibility);
            if (score.moves <= policy.limits.maximumMoves && better(score, best) &&
                (!active_before.satisfied() || active_after.satisfied())) {
                repair = trial;
                result.finalSnapshot = trial;
                best = score;
            }
        }
    }
    // Once all titles are safe, compact in real Z-order without disturbing
    // any other recognizable title. This cheap pass also handles a fully
    // visible but distant peer on foreground changes, without a global beam.
    for (unsigned compact_round = 0; compact_round < 3; ++compact_round) {
        const auto before_compact = hash_layout(result.finalSnapshot);
        for (const auto index : targets) {
            if (best.satisfied != targets.size() || out_of_time() ||
                result.statesVisited >= policy.limits.maximumStates) break;
            ++result.statesVisited;
            auto trial = result.finalSnapshot;
            const auto original = trial.windows[index];
            const bool overlaps = std::any_of(trial.windows.begin(), trial.windows.end(), [&](const auto& other) {
                return higher_blocker(original, other) && original.visualRect.intersects(other.visualRect);
            });
            if (!overlaps || !original.movable) continue;
            std::vector<geometry::Rect> protected_titles;
            for (std::size_t other = 0; other < trial.windows.size(); ++other) {
                const auto& lower = trial.windows[other];
                if ((!lower.managed && other != active) || !higher_blocker(lower, original)) continue;
                const auto e = analyze_title_bar_exposure(trial, other, policy.ranking.visibility);
                if (e.satisfied()) protected_titles.push_back({lower.visualRect.left, lower.visualRect.top,
                    lower.visualRect.left + static_cast<std::int64_t>(e.requiredWidth),
                    lower.visualRect.top + static_cast<std::int64_t>(e.protectedHeight)});
            }
            auto compact_score = best;
            auto chosen = original;
            for (const auto& placement : placements(trial, index, active, policy)) {
                if (out_of_time()) break;
                auto& candidate = trial.windows[index];
                candidate = original;
                candidate.placementRect = placement;
                candidate.visualRect = *original.visualRect.translated(
                    placement.left - original.placementRect.left, placement.top - original.placementRect.top);
                if (title_distance(candidate, trial.windows[active].visualRect, policy.ranking.visibility) >
                    title_distance(chosen, trial.windows[active].visualRect, policy.ranking.visibility)) continue;
                if (!analyze_title_bar_exposure(trial, index, policy.ranking.visibility).satisfied() ||
                    std::any_of(protected_titles.begin(), protected_titles.end(), [&](const auto& rect) {
                        return rect.intersects(candidate.visualRect);
                    })) continue;
                const auto score = quality(trial, initial, targets, targets.size(), active, policy.ranking.visibility);
                if (score.moves <= policy.limits.maximumMoves && better(score, compact_score)) {
                    compact_score = score;
                    chosen = candidate;
                }
            }
            result.finalSnapshot.windows[index] = chosen;
            best = compact_score;
        }
        // Moving a lower peer can make an upper peer's side cue available. Settle
        // those ties here rather than moving it again on the next activation.
        if (hash_layout(result.finalSnapshot) == before_compact) break;
    }
    repair = result.finalSnapshot;
    // Keep safe repair as a seed. A complete layout has already received a
    // bounded proximity pass; reserve the expensive beam for missing titles.
    beam = {{repair, {}}};
    const bool needs_search = best.satisfied < targets.size();
    for (std::size_t rank = 0; needs_search && rank < targets.size() && !exhausted; ++rank) {
        const auto index = targets[rank];
        std::vector<Node> next;
        for (auto& parent : beam) {
            if (result.statesVisited >= policy.limits.maximumStates || out_of_time()) {
                exhausted = true;
                break;
            }
            ++result.statesVisited;
            const auto original = parent.snapshot.windows[index];
            // Fully separate windows already offer all their content. Preserve
            // their location, including during foreground optimization.
            const bool overlaps = std::any_of(parent.snapshot.windows.begin(),
                parent.snapshot.windows.end(), [&](const auto& blocker) {
                    return higher_blocker(original, blocker) &&
                        blocker.visualRect.intersects(original.visualRect);
                });
            const auto candidates = overlaps ? placements(parent.snapshot, index, active, policy)
                : std::vector<geometry::Rect>{original.placementRect};
            std::size_t checked = 0;
            for (const auto& placement : candidates) {
                if (++checked % 16 == 0 && out_of_time()) { exhausted = true; break; }
                auto& window = parent.snapshot.windows[index];
                window = original;
                window.placementRect = placement;
                window.visualRect = *original.visualRect.translated(
                    placement.left - original.placementRect.left,
                    placement.top - original.placementRect.top);
                auto score = quality(parent.snapshot, initial, targets, rank + 1, active,
                                      policy.ranking.visibility);
                if (score.moves > policy.limits.maximumMoves) continue;
                // Keep a small sorted beam while scanning candidates, bounding
                // allocations independently of the candidate grid size.
                const auto where = std::find_if(next.begin(), next.end(), [&](const auto& node) {
                    return better(score, node.score);
                });
                if (where != next.end() || next.size() < 6) {
                    next.insert(where, Node{parent.snapshot, std::move(score)});
                    if (next.size() > 6) next.pop_back();
                }
            }
            parent.snapshot.windows[index] = original;
            if (exhausted) break;
        }
        // Only commit candidates revalidated against ALL windows, including
        // untouched lower windows. Partial improvements must not regress any
        // previously recognizable target, nor the active window.
        for (const auto& node : next) {
            const auto score = quality(node.snapshot, initial, targets, targets.size(), active,
                                        policy.ranking.visibility);
            bool safe = score.moves <= policy.limits.maximumMoves;
            for (std::size_t i = 0; i < targets.size(); ++i)
                safe = safe && (!baseline.recognized[i] || score.recognized[i]);
            const auto active_before = analyze_title_bar_exposure(initial, active, policy.ranking.visibility);
            const auto active_after = analyze_title_bar_exposure(node.snapshot, active, policy.ranking.visibility);
            safe = safe && (!active_before.satisfied() || active_after.satisfied());
            if (safe && (scan.violations.empty() || score.satisfied > baseline.satisfied) &&
                better(score, best)) {
                best = score;
                result.finalSnapshot = node.snapshot;
            }
        }
        if (next.empty()) break;
        beam = std::move(next);
    }
    const auto final_scan = scan_visibility_violations(result.finalSnapshot, policy.ranking.visibility);
    result.violations = final_scan.violations;
    for (const auto index : targets) {
        const auto& from = initial.windows[index];
        const auto& to = result.finalSnapshot.windows[index];
        if (from.placementRect == to.placementRect) continue;
        MovePlan move;
        move.window = from.key;
        move.from = from.placementRect;
        move.to = to.placementRect;
        const auto exposure = analyze_title_bar_exposure(result.finalSnapshot, index, policy.ranking.visibility);
        move.cost.placementDirection = exposure.leftSide ? PlacementDirectionRank::TopLeft :
            exposure.rightSide ? PlacementDirectionRank::TopRight : PlacementDirectionRank::Stationary;
        move.cost.visibilityPreference = exposure.leftSide ? VisibilityPreferenceRank::TopLeft :
            exposure.rightSide ? VisibilityPreferenceRank::TopRight : VisibilityPreferenceRank::TopOnly;
        move.cost.movedWindowCount = 1;
        result.moves.push_back(move);
    }
    result.status = result.violations.empty()
        ? (result.moves.empty() ? SolveStatus::NoViolation : SolveStatus::Solved)
        : !result.moves.empty() ? SolveStatus::PartiallySolved
        : exhausted ? SolveStatus::Timeout : SolveStatus::Unsatisfiable;
    return finish();
}

} // namespace stage_manager::solver
