#include "geometry/interaction_zones.h"
#include "geometry/region.h"
#include "geometry/work_area.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <random>
#include <vector>

#define CHECK_CASE(condition, case_index)                                                \
    do {                                                                                 \
        if (!(condition)) {                                                              \
            std::fprintf(stderr,                                                         \
                         "property failed: %s (case %u, line %d)\n",                    \
                         #condition,                                                     \
                         static_cast<unsigned>(case_index),                              \
                         __LINE__);                                                      \
            return 1;                                                                    \
        }                                                                                \
    } while (false)

namespace {

stage_manager::geometry::Rect random_rect(std::mt19937_64& random)
{
    std::uniform_int_distribution<std::int64_t> position(-500, 500);
    std::uniform_int_distribution<std::int64_t> extent(1, 200);
    const auto left = position(random);
    const auto top = position(random);
    return {left, top, left + extent(random), top + extent(random)};
}

stage_manager::geometry::Region region_of(stage_manager::geometry::Rect rectangle)
{
    const std::vector<stage_manager::geometry::Rect> rectangles = {rectangle};
    return *stage_manager::geometry::Region::from_disjoint(rectangles);
}

bool is_disjoint(const stage_manager::geometry::Region& region)
{
    const auto& rectangles = region.rectangles();
    for (std::size_t left = 0; left < rectangles.size(); ++left) {
        if (rectangles[left].empty()) {
            return false;
        }
        for (std::size_t right = left + 1; right < rectangles.size(); ++right) {
            if (rectangles[left].intersects(rectangles[right])) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

int main()
{
    using stage_manager::geometry::Point;
    using stage_manager::geometry::Region;
    using stage_manager::geometry::fully_within_work_area;
    using stage_manager::geometry::intersect;
    using stage_manager::geometry::make_interaction_zones;
    using stage_manager::geometry::preserves_minimum_onscreen;
    using stage_manager::geometry::subtract;
    using stage_manager::geometry::unite;

    constexpr std::uint64_t kSeed = 0x5a17e2026ULL;
    constexpr std::uint32_t kCases = 5000;
    std::mt19937_64 random(kSeed);
    std::uniform_int_distribution<std::int64_t> sample_position(-600, 700);
    std::uniform_int_distribution<std::uint64_t> depth(1, 250);

    for (std::uint32_t case_index = 0; case_index < kCases; ++case_index) {
        const auto left_rect = random_rect(random);
        const auto right_rect = random_rect(random);
        const auto left = region_of(left_rect);
        const auto right = region_of(right_rect);

        const auto overlap = intersect(left, right);
        const auto merged = unite(left, right);
        const auto reverse_merged = unite(right, left);
        const auto difference = subtract(left, right);
        const auto repeated_difference = subtract(left, right);

        CHECK_CASE(overlap.succeeded(), case_index);
        CHECK_CASE(merged.succeeded(), case_index);
        CHECK_CASE(reverse_merged.succeeded(), case_index);
        CHECK_CASE(difference.succeeded(), case_index);
        CHECK_CASE(repeated_difference.succeeded(), case_index);
        CHECK_CASE(is_disjoint(overlap.region), case_index);
        CHECK_CASE(is_disjoint(merged.region), case_index);
        CHECK_CASE(is_disjoint(difference.region), case_index);
        CHECK_CASE(merged.region.area() ==
                       left.area() + right.area() - overlap.region.area(),
                   case_index);
        CHECK_CASE(reverse_merged.region.area() == merged.region.area(), case_index);
        CHECK_CASE(difference.region.area() + overlap.region.area() == left.area(),
                   case_index);
        CHECK_CASE(difference.region.rectangles() == repeated_difference.region.rectangles(),
                   case_index);

        for (const auto& fragment : difference.region.rectangles()) {
            CHECK_CASE(!fragment.intersects(right_rect), case_index);
        }

        for (int sample = 0; sample < 8; ++sample) {
            const Point point{sample_position(random), sample_position(random)};
            CHECK_CASE(merged.region.contains(point) ==
                           (left_rect.contains(point) || right_rect.contains(point)),
                       case_index);
            CHECK_CASE(difference.region.contains(point) ==
                           (left_rect.contains(point) && !right_rect.contains(point)),
                       case_index);
        }

        const auto zones = make_interaction_zones(left_rect, depth(random));
        for (const auto& zone : zones) {
            CHECK_CASE(left_rect.contains(zone.bounds), case_index);
            CHECK_CASE(!zone.bounds.empty(), case_index);
        }

        const auto work_area = random_rect(random);
        CHECK_CASE(!fully_within_work_area(left_rect, work_area) ||
                       preserves_minimum_onscreen(
                           left_rect,
                           work_area,
                           static_cast<std::int64_t>(left_rect.width()),
                           static_cast<std::int64_t>(left_rect.height())),
                   case_index);
    }

    const std::vector<stage_manager::geometry::Rect> tiles = {
        {0, 0, 10, 10},
        {10, 0, 20, 10},
        {0, 10, 10, 20},
        {10, 10, 20, 20},
    };
    auto shuffled = tiles;
    std::shuffle(shuffled.begin(), shuffled.end(), random);
    const auto ordered_region = Region::from_disjoint(tiles);
    const auto shuffled_region = Region::from_disjoint(shuffled);
    CHECK_CASE(ordered_region.has_value(), kCases);
    CHECK_CASE(shuffled_region.has_value(), kCases);
    CHECK_CASE(ordered_region->rectangles() == shuffled_region->rectangles(), kCases);
    CHECK_CASE(ordered_region->size() == 1, kCases);
    return 0;
}
