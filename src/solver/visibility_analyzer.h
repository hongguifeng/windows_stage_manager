#pragma once

#include "geometry/interaction_zones.h"
#include "solver/layout_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <array>
#include <optional>
#include <vector>

namespace stage_manager::solver {

enum class ViolationScanStatus : std::uint8_t {
    Ok,
    InvalidSnapshot,
    GeometryTooComplex,
};

struct EdgeAffordanceRule {
    std::uint32_t minimumLengthDip = 120;
    std::uint32_t maximumLengthDip = 240;
    std::uint32_t depthDip = 32;
    std::uint32_t lengthPercent = 25;

    constexpr bool operator==(const EdgeAffordanceRule&) const noexcept = default;
};

enum class VisibilityGoal : std::uint8_t {
    AnyRecognizableEdge,
    TopAndSide,
};

struct VisibilityRequirements {
    EdgeAffordanceRule top{120, 240, 32, 25};
    EdgeAffordanceRule left{120, 240, 40, 25};
    EdgeAffordanceRule right{160, 300, 64, 30};
    EdgeAffordanceRule bottom{180, 360, 64, 35};
    std::size_t maximumRegionRectangles = 1024;
    VisibilityGoal goal = VisibilityGoal::AnyRecognizableEdge;
};

struct EdgeVisibility {
    bool left = false;
    bool right = false;
    bool top = false;
    bool bottom = false;
    bool topLeft = false;
    bool topRight = false;

    constexpr bool any_edge() const noexcept
    {
        return left || right || top || bottom;
    }

    constexpr bool top_and_side() const noexcept
    {
        return topLeft || topRight;
    }
};

struct PixelEdgeAffordance {
    std::uint64_t length = 0;
    std::uint64_t depth = 0;
};

struct Violation {
    std::size_t targetIndex = 0;
    std::vector<std::size_t> blockerIndices;
    std::vector<geometry::Edge> failedEdges;
};

struct ViolationScanResult {
    ViolationScanStatus status = ViolationScanStatus::Ok;
    std::vector<Violation> violations;
};

struct WindowVisibilityResult {
    ViolationScanStatus status = ViolationScanStatus::Ok;
    std::optional<EdgeVisibility> visibility;
};

ViolationScanResult scan_visibility_violations(
    const LayoutSnapshot& snapshot, const VisibilityRequirements& requirements);

std::optional<EdgeVisibility> analyze_window_visibility(
    const LayoutSnapshot& snapshot,
    std::size_t target_index,
    const VisibilityRequirements& requirements);

WindowVisibilityResult analyze_window_visibility_with_status(
    const LayoutSnapshot& snapshot,
    std::size_t target_index,
    const VisibilityRequirements& requirements);

std::array<PixelEdgeAffordance, 4> resolve_edge_affordances(
    const LayoutWindow& target, const VisibilityRequirements& requirements);

} // namespace stage_manager::solver
