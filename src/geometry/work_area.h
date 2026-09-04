#pragma once

#include "geometry/rect.h"

#include <cstdint>

namespace stage_manager::geometry {

bool fully_within_work_area(const Rect& window, const Rect& work_area) noexcept;
bool preserves_minimum_onscreen(const Rect& window,
                                const Rect& work_area,
                                std::int64_t minimum_width,
                                std::int64_t minimum_height) noexcept;

} // namespace stage_manager::geometry
