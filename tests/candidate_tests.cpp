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

stage_manager::solver::VisibilityRequirements requirements_for(
    stage_manager::solver::VisibilityGoal goal =
        stage_manager::solver::VisibilityGoal::AnyRecognizableEdge,
    std::size_t maximum_rectangles = 128)
{
    stage_manager::solver::VisibilityRequirements requirements;
    const stage_manager::solver::EdgeAffordanceRule rule{48, 48, 24, 100};
    requirements.top = rule;
    requirements.left = rule;
    requirements.right = rule;
    requirements.bottom = rule;
    requirements.maximumRegionRectangles = maximum_rectangles;
    requirements.goal = goal;
    return requirements;
}

} // namespace

int main()
{
    using stage_manager::geometry::Rect;
    using stage_manager::solver::CandidateGenerationStatus;
    using stage_manager::solver::LayoutSnapshot;
    using stage_manager::solver::ViolationScanStatus;
    using stage_manager::solver::VisibilityGoal;
    using stage_manager::solver::VisibilityRequirements;
    using stage_manager::solver::analyze_window_visibility;
    using stage_manager::solver::generate_candidates;
    using stage_manager::solver::scan_visibility_violations;

    LayoutSnapshot covered;
    covered.version = 1;
    covered.windows = {
        make_window(1, {100, 100, 300, 300}, 0, false),
        make_window(2, {100, 100, 300, 300}, 1, true),
    };
    const auto requirements = requirements_for();
    const auto violations = scan_visibility_violations(covered, requirements);
    CHECK(violations.status == ViolationScanStatus::Ok);
    CHECK(violations.violations.size() == 1);
    CHECK(violations.violations[0].targetIndex == 1);
    CHECK(violations.violations[0].blockerIndices == std::vector<std::size_t>{0});
    CHECK(violations.violations[0].failedEdges.size() == 4);

    const auto candidates = generate_candidates(
        covered, violations.violations[0], requirements, 128);
    CHECK(candidates.status == CandidateGenerationStatus::Ok);
    CHECK(!candidates.candidates.empty());
    CHECK((candidates.candidates.front().placementRect == Rect{100, 100, 300, 300}));
    CHECK(has_candidate(candidates, {76, 100, 276, 300}));
    CHECK(has_candidate(candidates, {124, 100, 324, 300}));
    CHECK(has_candidate(candidates, {100, 76, 300, 276}));
    CHECK(has_candidate(candidates, {100, 124, 300, 324}));
    CHECK(has_candidate(candidates, {76, 76, 276, 276}));
    CHECK(has_candidate(candidates, {124, 76, 324, 276}));
    CHECK(has_candidate(candidates, {0, 0, 200, 200}));
    CHECK(has_candidate(candidates, {124, 124, 324, 324}));

    const auto repeated_candidates = generate_candidates(
        covered, violations.violations[0], requirements, 128);
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
        one_edge_exposed, requirements_for());
    CHECK(one_edge_allowed.status == ViolationScanStatus::Ok);
    CHECK(one_edge_allowed.violations.empty());
    const auto two_edges_required = scan_visibility_violations(
        one_edge_exposed, requirements_for(VisibilityGoal::TopAndSide));
    CHECK(two_edges_required.status == ViolationScanStatus::Ok);
    CHECK(two_edges_required.violations.size() == 1);
    CHECK(two_edges_required.violations[0].targetIndex == 3);
    CHECK(two_edges_required.violations[0].failedEdges.size() == 3);

    LayoutSnapshot single_strip_exposure;
    single_strip_exposure.windows = {
        make_window(20, {100, 100, 300, 300}, 0, false),
        make_window(21, {36, 100, 236, 300}, 1, true),
    };
    const auto single_strip_two_edge_scan = scan_visibility_violations(
        single_strip_exposure, requirements_for(VisibilityGoal::TopAndSide));
    CHECK(single_strip_two_edge_scan.status == ViolationScanStatus::Ok);
    CHECK(single_strip_two_edge_scan.violations.size() == 1);
    CHECK(single_strip_two_edge_scan.violations[0].targetIndex == 1);
    const auto shared_corner_visibility = analyze_window_visibility(
        single_strip_exposure, 1, requirements_for());
    CHECK(shared_corner_visibility.has_value());
    CHECK(shared_corner_visibility->left);
    CHECK(!shared_corner_visibility->topLeft);

    const auto single_strip_one_edge_scan = scan_visibility_violations(
        single_strip_exposure, requirements_for());
    CHECK(single_strip_one_edge_scan.status == ViolationScanStatus::Ok);
    CHECK(single_strip_one_edge_scan.violations.empty());

    auto independent_two_edge_exposure = single_strip_exposure;
    independent_two_edge_exposure.windows[1].placementRect = {36, 36, 236, 236};
    independent_two_edge_exposure.windows[1].visualRect = {36, 36, 236, 236};
    const auto independent_two_edge_scan = scan_visibility_violations(
        independent_two_edge_exposure, requirements_for(VisibilityGoal::TopAndSide));
    CHECK(independent_two_edge_scan.status == ViolationScanStatus::Ok);
    CHECK(independent_two_edge_scan.violations.empty());

    auto title_bar_sensitive = covered;
    title_bar_sensitive.windows[0].placementRect = {100, 132, 300, 300};
    title_bar_sensitive.windows[0].visualRect =
        title_bar_sensitive.windows[0].placementRect;
    title_bar_sensitive.windows[1].titleBarHeight = 32;
    auto asymmetric = requirements_for();
    asymmetric.top = {120, 240, 20, 25};
    asymmetric.left = {120, 240, 40, 25};
    asymmetric.right = {160, 300, 64, 30};
    asymmetric.bottom = {180, 360, 64, 35};
    const auto title_visibility = analyze_window_visibility(
        title_bar_sensitive, 1, asymmetric);
    CHECK(title_visibility.has_value());
    CHECK(title_visibility->top);
    CHECK(!title_visibility->left);
    CHECK(!title_visibility->right);
    CHECK(!title_visibility->bottom);

    auto insufficient_title = title_bar_sensitive;
    insufficient_title.windows[0].placementRect.top = 131;
    insufficient_title.windows[0].visualRect.top = 131;
    const auto insufficient_visibility = analyze_window_visibility(
        insufficient_title, 1, asymmetric);
    CHECK(insufficient_visibility.has_value());
    CHECK(!insufficient_visibility->top);

    auto unmanaged_target = covered;
    unmanaged_target.windows[1].managed = false;
    CHECK(scan_visibility_violations(unmanaged_target, requirements).violations.empty());

    auto other_monitor = covered;
    other_monitor.windows[0].monitor = 2;
    CHECK(scan_visibility_violations(other_monitor, requirements).violations.empty());

    auto fragment_limited = requirements_for(VisibilityGoal::AnyRecognizableEdge, 1);
    auto split_edge = covered;
    split_edge.windows[0].visualRect = {100, 175, 124, 225};
    split_edge.windows[0].placementRect = split_edge.windows[0].visualRect;
    CHECK(scan_visibility_violations(split_edge, fragment_limited).status ==
          ViolationScanStatus::GeometryTooComplex);

    CHECK(generate_candidates(covered, violations.violations[0], requirements, 1).status ==
          CandidateGenerationStatus::TooComplex);
    auto invalid_violation = violations.violations[0];
    invalid_violation.targetIndex = 99;
    CHECK(generate_candidates(covered, invalid_violation, requirements).status ==
          CandidateGenerationStatus::InvalidInput);
    auto invalid_regions = requirements_for();
    invalid_regions.maximumRegionRectangles = 0;
    CHECK(scan_visibility_violations(covered, invalid_regions).status ==
          ViolationScanStatus::InvalidSnapshot);
    auto invalid_length = requirements_for();
    invalid_length.top.minimumLengthDip = 0;
    CHECK(scan_visibility_violations(covered, invalid_length).status ==
          ViolationScanStatus::InvalidSnapshot);
    auto invalid_depth = requirements_for();
    invalid_depth.left.depthDip = 0;
    CHECK(scan_visibility_violations(covered, invalid_depth).status ==
          ViolationScanStatus::InvalidSnapshot);
    auto invalid_percent = requirements_for();
    invalid_percent.right.lengthPercent = 101;
    CHECK(scan_visibility_violations(covered, invalid_percent).status ==
          ViolationScanStatus::InvalidSnapshot);
    auto fixed_target = covered;
    fixed_target.windows[1].movable = false;
    CHECK(generate_candidates(fixed_target, violations.violations[0], requirements).status ==
          CandidateGenerationStatus::InvalidInput);
    auto self_blocked = violations.violations[0];
    self_blocked.blockerIndices.push_back(self_blocked.targetIndex);
    CHECK(generate_candidates(covered, self_blocked, requirements).status ==
          CandidateGenerationStatus::InvalidInput);
    return 0;
}
