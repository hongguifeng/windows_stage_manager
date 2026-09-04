#pragma once

#include "geometry/rect.h"

#include <cstdint>

namespace stage_manager::geometry {

using HitTarget = std::uintptr_t;

class IRootWindowHitTester {
public:
    virtual ~IRootWindowHitTester() = default;

    virtual HitTarget root_window_at(Point screen_point) const = 0;
};

bool point_belongs_to_window(const IRootWindowHitTester& hit_tester,
                             Point screen_point,
                             HitTarget expected_root);

} // namespace stage_manager::geometry
