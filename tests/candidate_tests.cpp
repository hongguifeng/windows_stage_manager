#include "solver/candidate_generator.h"
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
    window.monitor = 1;
    window.zIndex = z_index;
    window.managed = managed;
    window.movable = managed;
    window.visible = true;
    window.blocksVisibility = true;
    window.currentDesktop = true;
    return window;
}

bool has_candidate(const stage_manager::solver::CandidateGenerationResult& result,
                   stage_manager::geometry::Rect rectangle)
{
    return std::any_of(result.candidates.begin(), result.candidates.end(),
                       [&rectangle](const auto& candidate) {
                           return candidate.placementRect == rectangle;
                       });
}

} // namespace

int main()
{
    using stage_manager::geometry::Rect;
    using stage_manager::solver::CandidateGenerationStatus;
    using stage_manager::solver::LayoutSnapshot;
    using stage_manager::solver::ViolationScanStatus;
    using stage_manager::solver::VisibilityRequirements;
    using stage_manager::solver::generate_candidates;
    using stage_manager::solver::scan_visibility_violations;

    LayoutSnapshot covered;
    covered.version = 1;
    covered.windows = {
        make_window(1, {100, 100, 300, 300}, 0, false),
        make_window(2, {100, 100, 300, 300}, 1, true),
    };
    const VisibilityRequirements requirements{48, 24, 128};
    const auto violations = scan_visibility_violations(covered, requirements);
    CHECK(violations.status == ViolationScanStatus::Ok);
    CHECK(violations.violations.size() == 1);
    CHECK(violations.violations[0].targetIndex == 1);
    CHECK(violations.violations[0].blockerIndices == std::vector<std::size_t>{0});
    CHECK(violations.violations[0].failedEdges.size() == 4);

    const auto candidates = generate_candidates(covered, violations.violations[0], 64, 128);
    CHECK(candidates.status == CandidateGenerationStatus::Ok);
    CHECK(!candidates.candidates.empty());
    CHECK((candidates.candidates.front().placementRect == Rect{100, 100, 300, 300}));
    CHECK(has_candidate(candidates, {36, 100, 236, 300}));
    CHECK(has_candidate(candidates, {164, 100, 364, 300}));
    CHECK(has_candidate(candidates, {100, 36, 300, 236}));
    CHECK(has_candidate(candidates, {100, 164, 300, 364}));
    CHECK(has_candidate(candidates, {0, 0, 200, 200}));
    CHECK(has_candidate(candidates, {164, 164, 364, 364}));

    const auto repeated_candidates = generate_candidates(
        covered, violations.violations[0], 64, 128);
    CHECK(repeated_candidates.status == CandidateGenerationStatus::Ok);
    CHECK(repeated_candidates.candidates.size() == candidates.candidates.size());
    for (std::size_t index = 0; index < candidates.candidates.size(); ++index) {
        CHECK(repeated_candidates.candidates[index].placementRect ==
              candidates.candidates[index].placementRect);
        CHECK(repeated_candidates.candidates[index].sources ==
              candidates.candidates[index].sources);
    }

    auto partly_exposed = covered;
    partly_exposed.windows[0].visualRect = {100, 100, 250, 250};
    partly_exposed.windows[0].placementRect = partly_exposed.windows[0].visualRect;
    const auto no_violation = scan_visibility_violations(partly_exposed, requirements);
    CHECK(no_violation.status == ViolationScanStatus::Ok);
    CHECK(no_violation.violations.empty());

    LayoutSnapshot one_edge_exposed;
    one_edge_exposed.windows = {
        make_window(10, {100, 100, 300, 124}, 0, false),
        make_window(11, {100, 276, 300, 300}, 1, false),
        make_window(12, {100, 124, 276, 276}, 2, false),
        make_window(13, {100, 100, 300, 300}, 3, true),
    };
    const auto one_edge_allowed = scan_visibility_violations(
        one_edge_exposed, VisibilityRequirements{48, 24, 128, 1});
    CHECK(one_edge_allowed.status == ViolationScanStatus::Ok);
    CHECK(one_edge_allowed.violations.empty());
    const auto two_edges_required = scan_visibility_violations(
        one_edge_exposed, VisibilityRequirements{48, 24, 128, 2});
    CHECK(two_edges_required.status == ViolationScanStatus::Ok);
    CHECK(two_edges_required.violations.size() == 1);
    CHECK(two_edges_required.violations[0].targetIndex == 3);
    CHECK(two_edges_required.violations[0].failedEdges.size() == 3);

    auto unmanaged_target = covered;
    unmanaged_target.windows[1].managed = false;
    CHECK(scan_visibility_violations(unmanaged_target, requirements).violations.empty());

    auto other_monitor = covered;
    other_monitor.windows[0].monitor = 2;
    CHECK(scan_visibility_violations(other_monitor, requirements).violations.empty());

    VisibilityRequirements fragment_limited{48, 24, 1};
    auto split_edge = covered;
    split_edge.windows[0].visualRect = {100, 175, 124, 225};
    split_edge.windows[0].placementRect = split_edge.windows[0].visualRect;
    CHECK(scan_visibility_violations(split_edge, fragment_limited).status ==
          ViolationScanStatus::GeometryTooComplex);

    CHECK(generate_candidates(covered, violations.violations[0], 64, 1).status ==
          CandidateGenerationStatus::TooComplex);
    auto invalid_violation = violations.violations[0];
    invalid_violation.targetIndex = 99;
    CHECK(generate_candidates(covered, invalid_violation, 64).status ==
          CandidateGenerationStatus::InvalidInput);
    CHECK(scan_visibility_violations(covered, VisibilityRequirements{48, 24, 0}).status ==
          ViolationScanStatus::InvalidSnapshot);
    CHECK(scan_visibility_violations(covered, VisibilityRequirements{0, 24, 128}).status ==
          ViolationScanStatus::InvalidSnapshot);
    CHECK(scan_visibility_violations(covered, VisibilityRequirements{48, 24, 128, 0}).status ==
          ViolationScanStatus::InvalidSnapshot);
    CHECK(scan_visibility_violations(covered, VisibilityRequirements{48, 24, 128, 5}).status ==
          ViolationScanStatus::InvalidSnapshot);
    auto fixed_target = covered;
    fixed_target.windows[1].movable = false;
    CHECK(generate_candidates(fixed_target, violations.violations[0], 64).status ==
          CandidateGenerationStatus::InvalidInput);
    auto self_blocked = violations.violations[0];
    self_blocked.blockerIndices.push_back(self_blocked.targetIndex);
    CHECK(generate_candidates(covered, self_blocked, 64).status ==
          CandidateGenerationStatus::InvalidInput);
    return 0;
}
