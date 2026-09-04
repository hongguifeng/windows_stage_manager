#pragma once

#include "geometry/rect.h"

#include <cstddef>
#include <optional>
#include <span>
#include <vector>

namespace stage_manager::geometry {

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

private:
    std::vector<Rect> rectangles_;
};

} // namespace stage_manager::geometry
