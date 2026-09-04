#include "geometry/rect.h"

#include <algorithm>
#include <limits>

namespace stage_manager::geometry {
namespace {

std::optional<std::int64_t> checked_add(std::int64_t value, std::int64_t delta) noexcept
{
    if ((delta > 0 && value > std::numeric_limits<std::int64_t>::max() - delta) ||
        (delta < 0 && value < std::numeric_limits<std::int64_t>::min() - delta)) {
        return std::nullopt;
    }
    return value + delta;
}

} // namespace

bool Rect::contains(Point point) const noexcept
{
    return !empty() && point.x >= left && point.x < right && point.y >= top &&
        point.y < bottom;
}

bool Rect::contains(const Rect& other) const noexcept
{
    return valid() && other.valid() && other.left >= left && other.top >= top &&
        other.right <= right && other.bottom <= bottom;
}

bool Rect::intersects(const Rect& other) const noexcept
{
    return !empty() && !other.empty() && left < other.right && right > other.left &&
        top < other.bottom && bottom > other.top;
}

std::optional<Rect> Rect::intersection(const Rect& other) const noexcept
{
    if (!intersects(other)) {
        return std::nullopt;
    }
    return Rect{
        std::max(left, other.left),
        std::max(top, other.top),
        std::min(right, other.right),
        std::min(bottom, other.bottom),
    };
}

std::optional<Rect> Rect::translated(std::int64_t delta_x, std::int64_t delta_y) const noexcept
{
    const auto translated_left = checked_add(left, delta_x);
    const auto translated_top = checked_add(top, delta_y);
    const auto translated_right = checked_add(right, delta_x);
    const auto translated_bottom = checked_add(bottom, delta_y);
    if (!translated_left || !translated_top || !translated_right || !translated_bottom) {
        return std::nullopt;
    }
    return Rect{*translated_left, *translated_top, *translated_right, *translated_bottom};
}

} // namespace stage_manager::geometry
