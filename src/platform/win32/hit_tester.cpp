#include "platform/win32/hit_tester.h"

#ifdef _WIN32

#include <windows.h>

#include <limits>

namespace stage_manager::platform::win32 {

geometry::HitTarget Win32RootWindowHitTester::root_window_at(
    geometry::Point screen_point) const
{
    if (screen_point.x < std::numeric_limits<LONG>::min() ||
        screen_point.x > std::numeric_limits<LONG>::max() ||
        screen_point.y < std::numeric_limits<LONG>::min() ||
        screen_point.y > std::numeric_limits<LONG>::max()) {
        return 0;
    }
    const POINT point{
        static_cast<LONG>(screen_point.x),
        static_cast<LONG>(screen_point.y),
    };
    const HWND hit = WindowFromPoint(point);
    const HWND root = hit == nullptr ? nullptr : GetAncestor(hit, GA_ROOT);
    return reinterpret_cast<geometry::HitTarget>(root);
}

} // namespace stage_manager::platform::win32

#endif
