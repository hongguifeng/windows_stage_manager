#include "geometry/interaction_zones.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace stage_manager::geometry {
namespace {

std::int64_t inset_coordinate(std::int64_t start,
                              std::int64_t end,
                              std::uint64_t depth,
                              bool from_start) noexcept
{
    const auto available = static_cast<std::uint64_t>(end) - static_cast<std::uint64_t>(start);
    const auto actual_depth = std::min(depth, available);
    if (actual_depth == available) {
        return from_start ? end : start;
    }
    const auto encoded = from_start
        ? static_cast<std::uint64_t>(start) + actual_depth
        : static_cast<std::uint64_t>(end) - actual_depth;
    constexpr auto kMaximumSigned = static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max());
    if (encoded <= kMaximumSigned) {
        return static_cast<std::int64_t>(encoded);
    }
    return -1 - static_cast<std::int64_t>(std::numeric_limits<std::uint64_t>::max() - encoded);
}

} // namespace

std::array<InteractionZone, 4> make_interaction_zones(
    const Rect& window, std::uint64_t depth) noexcept
{
    if (window.empty()) {
        return {};
    }
    const auto inner_left = inset_coordinate(window.left, window.right, depth, true);
    const auto inner_right = inset_coordinate(window.left, window.right, depth, false);
    const auto inner_top = inset_coordinate(window.top, window.bottom, depth, true);
    const auto inner_bottom = inset_coordinate(window.top, window.bottom, depth, false);
    return {{
        {Edge::Left, {window.left, window.top, inner_left, window.bottom}},
        {Edge::Right, {inner_right, window.top, window.right, window.bottom}},
        {Edge::Top, {window.left, window.top, window.right, inner_top}},
        {Edge::Bottom, {window.left, inner_bottom, window.right, window.bottom}},
    }};
}

bool has_exposed_edge_segment(const Region& exposed,
                              const InteractionZone& zone,
                              std::uint64_t minimum_length,
                              std::uint64_t minimum_depth) noexcept
{
    for (const auto& rectangle : exposed.rectangles()) {
        const bool vertical = zone.edge == Edge::Left || zone.edge == Edge::Right;
        const auto length = vertical ? rectangle.height() : rectangle.width();
        const auto depth = vertical ? rectangle.width() : rectangle.height();
        const bool touches_edge =
            (zone.edge == Edge::Left && rectangle.left == zone.bounds.left) ||
            (zone.edge == Edge::Right && rectangle.right == zone.bounds.right) ||
            (zone.edge == Edge::Top && rectangle.top == zone.bounds.top) ||
            (zone.edge == Edge::Bottom && rectangle.bottom == zone.bounds.bottom);
        if (touches_edge && length >= minimum_length && depth >= minimum_depth) {
            return true;
        }
    }
    return false;
}

} // namespace stage_manager::geometry
