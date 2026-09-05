#include "solver/visibility_analyzer.h"

#include "geometry/region.h"

#include <algorithm>
#include <array>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace stage_manager::solver {
namespace {

std::optional<geometry::Region> region_of(const geometry::Rect& rectangle,
                                          std::size_t maximum_rectangles)
{
    const std::array rectangles = {rectangle};
    return geometry::Region::from_disjoint(rectangles, maximum_rectangles);
}

bool relevant_blocker(const LayoutWindow& target, const LayoutWindow& blocker)
{
    return blocker.visible && blocker.blocksVisibility && blocker.currentDesktop &&
        blocker.zIndex >= 0 && blocker.zIndex < target.zIndex &&
        (target.monitor == 0 || blocker.monitor == target.monitor);
}

} // namespace

ViolationScanResult scan_visibility_violations(
    const LayoutSnapshot& snapshot, const VisibilityRequirements& requirements)
{
    ViolationScanResult result;
    if (requirements.minimumExposedLength == 0 ||
        requirements.minimumExposedDepth == 0 ||
        requirements.maximumRegionRectangles == 0 ||
        requirements.minimumExposedEdges == 0 ||
        requirements.minimumExposedEdges > 4) {
        result.status = ViolationScanStatus::InvalidSnapshot;
        return result;
    }

    for (std::size_t target_index = 0; target_index < snapshot.windows.size(); ++target_index) {
        const auto& target = snapshot.windows[target_index];
        if (!target.managed || !target.visible || !target.currentDesktop) {
            continue;
        }
        if (target.zIndex < 0 || target.visualRect.empty() || target.workArea.empty()) {
            result.status = ViolationScanStatus::InvalidSnapshot;
            result.violations.clear();
            return result;
        }

        Violation violation;
        violation.targetIndex = target_index;
        std::uint32_t exposed_edge_count = 0;
        const auto zones = geometry::make_interaction_zones(
            target.visualRect, requirements.minimumExposedDepth);
        for (const auto& zone : zones) {
            const auto zone_region = region_of(zone.bounds, requirements.maximumRegionRectangles);
            const auto work_region = region_of(
                target.workArea, requirements.maximumRegionRectangles);
            if (!zone_region || !work_region) {
                result.status = ViolationScanStatus::InvalidSnapshot;
                result.violations.clear();
                return result;
            }
            const auto clipped_zone = geometry::intersect(
                *zone_region, *work_region, requirements.maximumRegionRectangles);
            if (!clipped_zone.succeeded()) {
                result.status = ViolationScanStatus::GeometryTooComplex;
                result.violations.clear();
                return result;
            }

            geometry::Region blocker_region;
            for (std::size_t blocker_index = 0; blocker_index < snapshot.windows.size();
                 ++blocker_index) {
                if (blocker_index == target_index) {
                    continue;
                }
                const auto& blocker = snapshot.windows[blocker_index];
                if (!relevant_blocker(target, blocker) ||
                    !blocker.visualRect.intersects(zone.bounds)) {
                    continue;
                }
                const auto single_blocker = region_of(
                    blocker.visualRect, requirements.maximumRegionRectangles);
                if (!single_blocker) {
                    result.status = ViolationScanStatus::InvalidSnapshot;
                    result.violations.clear();
                    return result;
                }
                auto combined = geometry::unite(
                    blocker_region, *single_blocker, requirements.maximumRegionRectangles);
                if (!combined.succeeded()) {
                    result.status = ViolationScanStatus::GeometryTooComplex;
                    result.violations.clear();
                    return result;
                }
                blocker_region = std::move(combined.region);
                if (std::find(violation.blockerIndices.begin(),
                              violation.blockerIndices.end(),
                              blocker_index) == violation.blockerIndices.end()) {
                    violation.blockerIndices.push_back(blocker_index);
                }
            }

            const auto exposed = geometry::subtract(
                clipped_zone.region, blocker_region, requirements.maximumRegionRectangles);
            if (!exposed.succeeded()) {
                result.status = ViolationScanStatus::GeometryTooComplex;
                result.violations.clear();
                return result;
            }
            if (!geometry::has_exposed_edge_segment(exposed.region,
                                                    zone,
                                                    requirements.minimumExposedLength,
                                                    requirements.minimumExposedDepth)) {
                violation.failedEdges.push_back(zone.edge);
            } else {
                ++exposed_edge_count;
            }
        }

        if (exposed_edge_count < requirements.minimumExposedEdges) {
            std::sort(violation.blockerIndices.begin(),
                      violation.blockerIndices.end(),
                      [&snapshot](std::size_t left, std::size_t right) {
                          return snapshot.windows[left].zIndex < snapshot.windows[right].zIndex;
                      });
            result.violations.push_back(std::move(violation));
        }
    }
    return result;
}

} // namespace stage_manager::solver
