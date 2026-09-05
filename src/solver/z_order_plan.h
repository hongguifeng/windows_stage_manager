#pragma once

#include "window/window_snapshot.h"

#include <cstdint>

namespace stage_manager::solver {

struct ZOrderPlan {
    window::WindowKey window;
    window::WindowKey insertAfter;
    std::int32_t fromZIndex = -1;
    std::int32_t toZIndex = -1;
};

} // namespace stage_manager::solver
