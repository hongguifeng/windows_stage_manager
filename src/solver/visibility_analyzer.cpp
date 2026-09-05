#include "solver/visibility_analyzer.h"

#include "geometry/dpi.h"
#include "geometry/region.h"

#include <algorithm>
#include <array>
#include <optional>
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

bool valid_rule(const EdgeAffordanceRule& rule) noexcept
{
    return rule.minimumLengthDip > 0 &&
        rule.maximumLengthDip >= rule.minimumLengthDip && rule.depthDip > 0 &&
        rule.lengthPercent > 0 && rule.lengthPercent <= 100;
}

bool valid_requirements(const VisibilityRequirements& requirements) noexcept
{
    return valid_rule(requirements.top) && valid_rule(requirements.left) &&
        valid_rule(requirements.right) && valid_rule(requirements.bottom) &&
        requirements.maximumRegionRectangles > 0;
}

PixelEdgeAffordance pixel_rule(const LayoutWindow& target,
                               const EdgeAffordanceRule& rule,
                               geometry::Edge edge)
{
    const auto dpi = target.dpi == 0 ? 96u : target.dpi;
    const auto axis_length = edge == geometry::Edge::Left || edge == geometry::Edge::Right
        ? static_cast<std::uint64_t>(target.visualRect.height())
        : static_cast<std::uint64_t>(target.visualRect.width());
    const auto minimum = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(rule.minimumLengthDip, dpi));
    const auto maximum = static_cast<std::uint64_t>(
        geometry::scale_dip_ceil(rule.maximumLengthDip, dpi));
    const auto proportional = axis_length * rule.lengthPercent / 100;
    PixelEdgeAffordance result;
    result.length = std::min(axis_length, std::clamp(proportional, minimum, maximum));
    result.depth = edge == geometry::Edge::Top && target.titleBarHeight > 0
        ? target.titleBarHeight
        : static_cast<std::uint64_t>(geometry::scale_dip_ceil(rule.depthDip, dpi));
    const auto perpendicular = edge == geometry::Edge::Left || edge == geometry::Edge::Right
        ? static_cast<std::uint64_t>(target.visualRect.width())
        : static_cast<std::uint64_t>(target.visualRect.height());
    result.depth = std::min(result.depth, perpendicular);
    return result;
}

struct ExposureContext {
    geometry::Region blockerRegion;
    std::vector<std::size_t> blockerIndices;
};

std::optional<ExposureContext> make_exposure_context(
    const LayoutSnapshot& snapshot,
    std::size_t target_index,
    std::size_t maximum_rectangles,
    ViolationScanStatus& status)
{
    ExposureContext context;
    const auto& target = snapshot.windows[target_index];
    for (std::size_t blocker_index = 0; blocker_index < snapshot.windows.size();
         ++blocker_index) {
        if (blocker_index == target_index) {
            continue;
        }
        const auto& blocker = snapshot.windows[blocker_index];
        if (!relevant_blocker(target, blocker) ||
            !blocker.visualRect.intersects(target.visualRect)) {
            continue;
        }
        const auto single_blocker = region_of(blocker.visualRect, maximum_rectangles);
        if (!single_blocker) {
            status = ViolationScanStatus::InvalidSnapshot;
            return std::nullopt;
        }
        auto combined = geometry::unite(
            context.blockerRegion, *single_blocker, maximum_rectangles);
        if (!combined.succeeded()) {
            status = ViolationScanStatus::GeometryTooComplex;
            return std::nullopt;
        }
        context.blockerRegion = std::move(combined.region);
        context.blockerIndices.push_back(blocker_index);
    }
    return context;
}

std::optional<geometry::Region> exposed_zone(const LayoutWindow& target,
                                             const geometry::InteractionZone& zone,
                                             const geometry::Region& blockers,
                                             std::size_t maximum_rectangles,
                                             ViolationScanStatus& status)
{
    if (zone.bounds.empty()) {
        return geometry::Region{};
    }
    const auto zone_region = region_of(zone.bounds, maximum_rectangles);
    const auto work_region = region_of(target.workArea, maximum_rectangles);
    if (!zone_region || !work_region) {
        status = ViolationScanStatus::InvalidSnapshot;
        return std::nullopt;
    }
    const auto clipped = geometry::intersect(*zone_region, *work_region, maximum_rectangles);
    if (!clipped.succeeded()) {
        status = ViolationScanStatus::GeometryTooComplex;
        return std::nullopt;
    }
    const auto exposed = geometry::subtract(clipped.region, blockers, maximum_rectangles);
    if (!exposed.succeeded()) {
        status = ViolationScanStatus::GeometryTooComplex;
        return std::nullopt;
    }
    return exposed.region;
}

bool has_exposed_segment_in_bounds(const geometry::Region& exposed,
                                   const geometry::InteractionZone& zone,
                                   const PixelEdgeAffordance& rule) noexcept
{
    for (const auto& rectangle : exposed.rectangles()) {
        const geometry::Rect clipped{
            std::max(rectangle.left, zone.bounds.left),
            std::max(rectangle.top, zone.bounds.top),
            std::min(rectangle.right, zone.bounds.right),
            std::min(rectangle.bottom, zone.bounds.bottom),
        };
        if (clipped.empty()) {
            continue;
        }
        const bool vertical = zone.edge == geometry::Edge::Left ||
            zone.edge == geometry::Edge::Right;
        const auto length = vertical ? clipped.height() : clipped.width();
        const auto depth = vertical ? clipped.width() : clipped.height();
        const bool touches_edge =
            (zone.edge == geometry::Edge::Left && clipped.left == zone.bounds.left) ||
            (zone.edge == geometry::Edge::Right && clipped.right == zone.bounds.right) ||
            (zone.edge == geometry::Edge::Top && clipped.top == zone.bounds.top) ||
            (zone.edge == geometry::Edge::Bottom && clipped.bottom == zone.bounds.bottom);
        if (touches_edge && length >= rule.length && depth >= rule.depth) {
            return true;
        }
    }
    return false;
}

geometry::InteractionZone trim_shared_corner(geometry::InteractionZone zone,
                                              geometry::Edge adjacent,
                                              std::uint64_t adjacent_depth)
{
    const auto amount = static_cast<std::int64_t>(adjacent_depth);
    if (zone.edge == geometry::Edge::Top && adjacent == geometry::Edge::Left) {
        zone.bounds.left = std::min(zone.bounds.right, zone.bounds.left + amount);
    } else if (zone.edge == geometry::Edge::Top && adjacent == geometry::Edge::Right) {
        zone.bounds.right = std::max(zone.bounds.left, zone.bounds.right - amount);
    } else if (zone.edge == geometry::Edge::Left && adjacent == geometry::Edge::Top) {
        zone.bounds.top = std::min(zone.bounds.bottom, zone.bounds.top + amount);
    } else if (zone.edge == geometry::Edge::Right && adjacent == geometry::Edge::Top) {
        zone.bounds.top = std::min(zone.bounds.bottom, zone.bounds.top + amount);
    }
    return zone;
}

geometry::InteractionZone restrict_to_corner_channel(
    geometry::InteractionZone zone,
    geometry::Edge adjacent,
    std::uint64_t required_length) noexcept
{
    const auto length = static_cast<std::int64_t>(required_length);
    if (zone.edge == geometry::Edge::Top && adjacent == geometry::Edge::Left) {
        zone.bounds.right = std::min(zone.bounds.right, zone.bounds.left + length);
    } else if (zone.edge == geometry::Edge::Top && adjacent == geometry::Edge::Right) {
        zone.bounds.left = std::max(zone.bounds.left, zone.bounds.right - length);
    } else if ((zone.edge == geometry::Edge::Left || zone.edge == geometry::Edge::Right) &&
               adjacent == geometry::Edge::Top) {
        zone.bounds.bottom = std::min(zone.bounds.bottom, zone.bounds.top + length);
    }
    return zone;
}

std::optional<EdgeVisibility> analyze_with_context(
    const LayoutWindow& target,
    const VisibilityRequirements& requirements,
    const geometry::Region& blockers,
    ViolationScanStatus& status)
{
    const auto rules = resolve_edge_affordances(target, requirements);
    std::array<geometry::InteractionZone, 4> zones{};
    zones[0] = geometry::make_interaction_zones(target.visualRect, rules[0].depth)[0];
    zones[1] = geometry::make_interaction_zones(target.visualRect, rules[1].depth)[1];
    zones[2] = geometry::make_interaction_zones(target.visualRect, rules[2].depth)[2];
    zones[3] = geometry::make_interaction_zones(target.visualRect, rules[3].depth)[3];

    EdgeVisibility visibility;
    std::array<bool*, 4> outputs = {
        &visibility.left, &visibility.right, &visibility.top, &visibility.bottom};
    std::array<geometry::Region, 4> exposed_regions;
    for (std::size_t index = 0; index < zones.size(); ++index) {
        const auto exposed = exposed_zone(target,
                                          zones[index],
                                          blockers,
                                          requirements.maximumRegionRectangles,
                                          status);
        if (!exposed) {
            return std::nullopt;
        }
        exposed_regions[index] = *exposed;
        *outputs[index] = has_exposed_segment_in_bounds(
            exposed_regions[index], zones[index], rules[index]);
    }

    const auto top_without_left = restrict_to_corner_channel(
        trim_shared_corner(zones[2], geometry::Edge::Left, rules[0].depth),
        geometry::Edge::Left,
        rules[2].length);
    const auto left_without_top = restrict_to_corner_channel(
        trim_shared_corner(zones[0], geometry::Edge::Top, rules[2].depth),
        geometry::Edge::Top,
        rules[0].length);
    visibility.topLeft = has_exposed_segment_in_bounds(
                             exposed_regions[2], top_without_left, rules[2]) &&
        has_exposed_segment_in_bounds(exposed_regions[0], left_without_top, rules[0]);

    const auto top_without_right = restrict_to_corner_channel(
        trim_shared_corner(zones[2], geometry::Edge::Right, rules[1].depth),
        geometry::Edge::Right,
        rules[2].length);
    const auto right_without_top = restrict_to_corner_channel(
        trim_shared_corner(zones[1], geometry::Edge::Top, rules[2].depth),
        geometry::Edge::Top,
        rules[1].length);
    visibility.topRight = has_exposed_segment_in_bounds(
                              exposed_regions[2], top_without_right, rules[2]) &&
        has_exposed_segment_in_bounds(exposed_regions[1], right_without_top, rules[1]);
    return visibility;
}

} // namespace

std::array<PixelEdgeAffordance, 4> resolve_edge_affordances(
    const LayoutWindow& target, const VisibilityRequirements& requirements)
{
    return {{
        pixel_rule(target, requirements.left, geometry::Edge::Left),
        pixel_rule(target, requirements.right, geometry::Edge::Right),
        pixel_rule(target, requirements.top, geometry::Edge::Top),
        pixel_rule(target, requirements.bottom, geometry::Edge::Bottom),
    }};
}

std::optional<EdgeVisibility> analyze_window_visibility(
    const LayoutSnapshot& snapshot,
    std::size_t target_index,
    const VisibilityRequirements& requirements)
{
    return analyze_window_visibility_with_status(
        snapshot, target_index, requirements).visibility;
}

WindowVisibilityResult analyze_window_visibility_with_status(
    const LayoutSnapshot& snapshot,
    std::size_t target_index,
    const VisibilityRequirements& requirements)
{
    WindowVisibilityResult result;
    if (!valid_requirements(requirements) || target_index >= snapshot.windows.size()) {
        result.status = ViolationScanStatus::InvalidSnapshot;
        return result;
    }
    const auto& target = snapshot.windows[target_index];
    if (!target.visible || !target.currentDesktop || target.zIndex < 0 ||
        target.visualRect.empty() || target.workArea.empty()) {
        result.status = ViolationScanStatus::InvalidSnapshot;
        return result;
    }
    const auto context = make_exposure_context(
        snapshot, target_index, requirements.maximumRegionRectangles, result.status);
    if (!context) {
        return result;
    }
    result.visibility = analyze_with_context(
        target, requirements, context->blockerRegion, result.status);
    return result;
}

ViolationScanResult scan_visibility_violations(
    const LayoutSnapshot& snapshot, const VisibilityRequirements& requirements)
{
    ViolationScanResult result;
    if (!valid_requirements(requirements)) {
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

        auto context = make_exposure_context(
            snapshot, target_index, requirements.maximumRegionRectangles, result.status);
        if (!context) {
            result.violations.clear();
            return result;
        }
        const auto visibility = analyze_with_context(
            target, requirements, context->blockerRegion, result.status);
        if (!visibility) {
            result.violations.clear();
            return result;
        }
        const bool satisfied = requirements.goal == VisibilityGoal::TopAndSide
            ? visibility->top_and_side()
            : visibility->any_edge();
        if (satisfied) {
            continue;
        }

        Violation violation;
        violation.targetIndex = target_index;
        violation.blockerIndices = std::move(context->blockerIndices);
        if (!visibility->left) {
            violation.failedEdges.push_back(geometry::Edge::Left);
        }
        if (!visibility->right) {
            violation.failedEdges.push_back(geometry::Edge::Right);
        }
        if (!visibility->top) {
            violation.failedEdges.push_back(geometry::Edge::Top);
        }
        if (!visibility->bottom) {
            violation.failedEdges.push_back(geometry::Edge::Bottom);
        }
        std::sort(violation.blockerIndices.begin(),
                  violation.blockerIndices.end(),
                  [&snapshot](std::size_t left, std::size_t right) {
                      return snapshot.windows[left].zIndex < snapshot.windows[right].zIndex;
                  });
        result.violations.push_back(std::move(violation));
    }
    return result;
}

} // namespace stage_manager::solver
