#include "window/activation_placement.h"

#include <numeric>
#include <algorithm>

namespace stage_manager::window {
namespace {

std::int64_t horizontal_delta(
    const geometry::Rect& visual,
    const geometry::Rect& work_area,
    app::ActivationHorizontalAlignment alignment) noexcept
{
    switch (alignment) {
    case app::ActivationHorizontalAlignment::Left:
        return work_area.left - visual.left;
    case app::ActivationHorizontalAlignment::Center:
        return std::midpoint(work_area.left, work_area.right) -
            std::midpoint(visual.left, visual.right);
    case app::ActivationHorizontalAlignment::Right:
        return work_area.right - visual.right;
    }
    return 0;
}

std::int64_t vertical_delta(
    const geometry::Rect& visual,
    const geometry::Rect& work_area,
    app::ActivationVerticalAlignment alignment) noexcept
{
    if (visual.height() > work_area.height()) {
        return work_area.top - visual.top;
    }
    switch (alignment) {
    case app::ActivationVerticalAlignment::Top:
        return work_area.top - visual.top;
    case app::ActivationVerticalAlignment::Center:
        return std::midpoint(work_area.top, work_area.bottom) -
            std::midpoint(visual.top, visual.bottom);
    case app::ActivationVerticalAlignment::Bottom:
        return work_area.bottom - visual.bottom;
    }
    return 0;
}

} // namespace

std::optional<geometry::Rect> calculate_activated_placement(
    const solver::LayoutWindow& window,
    app::ActivationHorizontalAlignment horizontal_alignment,
    app::ActivationVerticalAlignment vertical_alignment) noexcept
{
    if (window.placementRect.empty() || window.visualRect.empty() ||
        window.workArea.empty() || window.placementRect.width() > window.workArea.width() ||
        window.placementRect.height() > window.workArea.height()) {
        return std::nullopt;
    }
    const auto desired = window.placementRect.translated(
        horizontal_delta(window.visualRect, window.workArea, horizontal_alignment),
        vertical_delta(window.visualRect, window.workArea, vertical_alignment));
    if (!desired) return std::nullopt;
    const auto left = std::clamp(desired->left, window.workArea.left,
        window.workArea.right - static_cast<std::int64_t>(desired->width()));
    const auto top = std::clamp(desired->top, window.workArea.top,
        window.workArea.bottom - static_cast<std::int64_t>(desired->height()));
    return desired->translated(left - desired->left, top - desired->top);
}

} // namespace stage_manager::window
