#pragma once

#include "geometry/rect.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace stage_manager::geometry {

enum class RegionStatus : std::uint8_t {
    Ok,
    InvalidInput,
    TooComplex,
};

class Region final {
public:
    Region() = default;

    static std::optional<Region> from_disjoint(
        std::span<const Rect> rectangles, std::size_t maximum_rectangles = 1024);

    const std::vector<Rect>& rectangles() const noexcept;
    bool empty() const noexcept;
    std::size_t size() const noexcept;
    std::uint64_t area() const noexcept;
    std::optional<Rect> bounds() const noexcept;
    bool contains(Point point) const noexcept;

private:
    std::vector<Rect> rectangles_;
};

struct RegionOperationResult {
    RegionStatus status = RegionStatus::Ok;
    Region region;

    constexpr bool succeeded() const noexcept
    {
        return status == RegionStatus::Ok;
    }
};

RegionOperationResult unite(const Region& left,
                            const Region& right,
                            std::size_t maximum_rectangles = 1024);
RegionOperationResult subtract(const Region& source,
                               const Region& blockers,
                               std::size_t maximum_rectangles = 1024);
RegionOperationResult intersect(const Region& left,
                                const Region& right,
                                std::size_t maximum_rectangles = 1024);

} // namespace stage_manager::geometry
