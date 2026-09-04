#include "geometry/hit_testing.h"

namespace stage_manager::geometry {

bool point_belongs_to_window(const IRootWindowHitTester& hit_tester,
                             Point screen_point,
                             HitTarget expected_root)
{
    return expected_root != 0 && hit_tester.root_window_at(screen_point) == expected_root;
}

} // namespace stage_manager::geometry
