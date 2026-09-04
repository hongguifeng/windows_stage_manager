#pragma once

#include "geometry/interaction_zones.h"
#include "solver/layout_snapshot.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace stage_manager::solver {

enum class ViolationScanStatus : std::uint8_t {
    Ok,
    InvalidSnapshot,
    GeometryTooComplex,
};

struct VisibilityRequirements {
    std::uint64_t minimumExposedLength = 48;
    std::uint64_t minimumExposedDepth = 24;
    std::size_t maximumRegionRectangles = 1024;
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

ViolationScanResult scan_visibility_violations(
    const LayoutSnapshot& snapshot, const VisibilityRequirements& requirements);

} // namespace stage_manager::solver
