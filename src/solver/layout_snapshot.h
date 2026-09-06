#pragma once

#include "geometry/rect.h"
#include "window/window_snapshot.h"

#include <cstdint>
#include <vector>

namespace stage_manager::solver {

struct LayoutWindow {
    window::WindowKey key;
    geometry::Rect placementRect;
    geometry::Rect visualRect;
    geometry::Rect workArea;
    geometry::Rect lastStableRect;
    std::uintptr_t monitor = 0;
    std::uint32_t dpi = 96;
    std::uint32_t titleBarHeight = 0;
    window::TitleBarHeightSource titleBarHeightSource = window::TitleBarHeightSource::Unknown;
    std::int32_t zIndex = -1;
    bool managed = false;
    bool movable = false;
    bool visible = false;
    bool blocksVisibility = false;
    bool currentDesktop = false;
    bool topmost = false;
};

struct LayoutSnapshot {
    std::uint64_t version = 0;
    std::vector<LayoutWindow> windows;
};

} // namespace stage_manager::solver
