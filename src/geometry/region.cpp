#include "geometry/region.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace stage_manager::geometry {

std::optional<Region> Region::from_disjoint(
    std::span<const Rect> rectangles, std::size_t maximum_rectangles)
{
    Region region;
    region.rectangles_.reserve(std::min(rectangles.size(), maximum_rectangles));
    for (const auto& rectangle : rectangles) {
        if (!rectangle.valid()) {
            return std::nullopt;
        }
        if (rectangle.empty()) {
            continue;
        }
        if (region.rectangles_.size() == maximum_rectangles) {
            return std::nullopt;
        }
        if (std::any_of(region.rectangles_.begin(), region.rectangles_.end(),
                        [&rectangle](const auto& existing) {
                            return existing.intersects(rectangle);
                        })) {
            return std::nullopt;
        }
        region.rectangles_.push_back(rectangle);
    }

    std::sort(region.rectangles_.begin(), region.rectangles_.end(), [](const auto& left,
                                                                       const auto& right) {
        if (left.top != right.top) {
            return left.top < right.top;
        }
        if (left.left != right.left) {
            return left.left < right.left;
        }
        if (left.bottom != right.bottom) {
            return left.bottom < right.bottom;
        }
        return left.right < right.right;
    });
    return region;
}

const std::vector<Rect>& Region::rectangles() const noexcept
{
    return rectangles_;
}

bool Region::empty() const noexcept
{
    return rectangles_.empty();
}

std::size_t Region::size() const noexcept
{
    return rectangles_.size();
}

std::uint64_t Region::area() const noexcept
{
    std::uint64_t total = 0;
    for (const auto& rectangle : rectangles_) {
        const auto width = static_cast<std::uint64_t>(rectangle.width());
        const auto height = static_cast<std::uint64_t>(rectangle.height());
        if (height != 0 && width > std::numeric_limits<std::uint64_t>::max() / height) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        const auto rectangle_area = width * height;
        if (total > std::numeric_limits<std::uint64_t>::max() - rectangle_area) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        total += rectangle_area;
    }
    return total;
}

std::optional<Rect> Region::bounds() const noexcept
{
    if (rectangles_.empty()) {
        return std::nullopt;
    }
    Rect result = rectangles_.front();
    for (const auto& rectangle : rectangles_) {
        result.left = std::min(result.left, rectangle.left);
        result.top = std::min(result.top, rectangle.top);
        result.right = std::max(result.right, rectangle.right);
        result.bottom = std::max(result.bottom, rectangle.bottom);
    }
    return result;
}

} // namespace stage_manager::geometry
