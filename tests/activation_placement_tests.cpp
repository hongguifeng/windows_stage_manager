#include "window/activation_placement.h"

#include <array>

#define CHECK(condition) do { if (!(condition)) return __LINE__; } while (false)

int main()
{
    using stage_manager::app::ActivationHorizontalAlignment;
    using stage_manager::app::ActivationVerticalAlignment;
    using stage_manager::geometry::Rect;
    using stage_manager::solver::LayoutWindow;
    using stage_manager::window::calculate_activated_placement;

    LayoutWindow window;
    window.placementRect = {90, 80, 410, 400};
    window.visualRect = {100, 100, 400, 400};
    window.workArea = {50, 40, 1050, 740};

    constexpr std::array horizontal = {
        ActivationHorizontalAlignment::Left,
        ActivationHorizontalAlignment::Center,
        ActivationHorizontalAlignment::Right,
    };
    constexpr std::array vertical = {
        ActivationVerticalAlignment::Top,
        ActivationVerticalAlignment::Center,
        ActivationVerticalAlignment::Bottom,
    };
    constexpr std::array<std::int64_t, 3> expected_left = {40, 390, 740};
    constexpr std::array<std::int64_t, 3> expected_top = {20, 220, 420};

    for (std::size_t x = 0; x < horizontal.size(); ++x) {
        for (std::size_t y = 0; y < vertical.size(); ++y) {
            const auto result = calculate_activated_placement(
                window, horizontal[x], vertical[y]);
            CHECK(result.has_value());
            CHECK(result->left == expected_left[x]);
            CHECK(result->top == expected_top[y]);
            CHECK(result->width() == window.placementRect.width());
            CHECK(result->height() == window.placementRect.height());
        }
    }

    LayoutWindow oversized = window;
    oversized.placementRect = {90, -70, 410, 870};
    oversized.visualRect = {100, -50, 400, 850};
    const auto oversized_bottom = calculate_activated_placement(
        oversized,
        ActivationHorizontalAlignment::Center,
        ActivationVerticalAlignment::Bottom);
    CHECK(oversized_bottom.has_value());
    CHECK(oversized_bottom->top == 20);
    CHECK(oversized_bottom->height() == oversized.placementRect.height());

    LayoutWindow invalid = window;
    invalid.visualRect = {100, 100, 100, 400};
    CHECK(!calculate_activated_placement(
        invalid,
        ActivationHorizontalAlignment::Center,
        ActivationVerticalAlignment::Center));

    return 0;
}
