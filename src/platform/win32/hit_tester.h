#pragma once

#ifdef _WIN32

#include "geometry/hit_testing.h"

namespace stage_manager::platform::win32 {

class Win32RootWindowHitTester final : public geometry::IRootWindowHitTester {
public:
    geometry::HitTarget root_window_at(geometry::Point screen_point) const override;
};

} // namespace stage_manager::platform::win32

#endif
