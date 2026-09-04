#include "geometry/work_area.h"

namespace stage_manager::geometry {

bool fully_within_work_area(const Rect& window, const Rect& work_area) noexcept
{
    return !window.empty() && !work_area.empty() && work_area.contains(window);
}

bool preserves_minimum_onscreen(const Rect& window,
                                const Rect& work_area,
                                std::int64_t minimum_width,
                                std::int64_t minimum_height) noexcept
{
    if (minimum_width < 0 || minimum_height < 0) {
        return false;
    }
    const auto overlap = window.intersection(work_area);
    return overlap.has_value() &&
        overlap->width() >= static_cast<std::uint64_t>(minimum_width) &&
        overlap->height() >= static_cast<std::uint64_t>(minimum_height);
}

} // namespace stage_manager::geometry
