#pragma once

#include "app/settings.h"
#include "solver/layout_snapshot.h"

#include <optional>

namespace stage_manager::window {

std::optional<geometry::Rect> calculate_activated_placement(
    const solver::LayoutWindow& window,
    app::ActivationHorizontalAlignment horizontal_alignment,
    app::ActivationVerticalAlignment vertical_alignment) noexcept;

} // namespace stage_manager::window
