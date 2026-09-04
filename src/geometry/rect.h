#pragma once

#include <cstdint>
#include <optional>

namespace stage_manager::geometry {

struct Point {
    std::int64_t x = 0;
    std::int64_t y = 0;

    constexpr bool operator==(const Point&) const noexcept = default;
};

struct Rect {
    std::int64_t left = 0;
    std::int64_t top = 0;
    std::int64_t right = 0;
    std::int64_t bottom = 0;

    constexpr bool operator==(const Rect&) const noexcept = default;

    constexpr bool valid() const noexcept
    {
        return left <= right && top <= bottom;
    }

    constexpr bool empty() const noexcept
    {
        return !valid() || left == right || top == bottom;
    }

    constexpr std::uint64_t width() const noexcept
    {
        return valid() ? static_cast<std::uint64_t>(right) -
                static_cast<std::uint64_t>(left)
                       : 0;
    }

    constexpr std::uint64_t height() const noexcept
    {
        return valid() ? static_cast<std::uint64_t>(bottom) -
                static_cast<std::uint64_t>(top)
                       : 0;
    }

    bool contains(Point point) const noexcept;
    bool contains(const Rect& other) const noexcept;
    bool intersects(const Rect& other) const noexcept;
    std::optional<Rect> intersection(const Rect& other) const noexcept;
    std::optional<Rect> translated(std::int64_t delta_x, std::int64_t delta_y) const noexcept;
};

} // namespace stage_manager::geometry
