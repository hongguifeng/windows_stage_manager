#pragma once

#include "geometry/rect.h"
#include "geometry/region.h"

#include <array>
#include <cstdint>

namespace stage_manager::geometry {

enum class Edge : std::uint8_t {
    Left,
    Right,
    Top,
    Bottom,
};

struct InteractionZone {
    Edge edge = Edge::Left;
    Rect bounds;
};

std::array<InteractionZone, 4> make_interaction_zones(
    const Rect& window, std::uint64_t depth) noexcept;
bool has_exposed_edge_segment(const Region& exposed,
                              const InteractionZone& zone,
                              std::uint64_t minimum_length,
                              std::uint64_t minimum_depth) noexcept;

} // namespace stage_manager::geometry
