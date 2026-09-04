#include "geometry/dpi.h"
#include "geometry/rect.h"
#include "geometry/region.h"
#include "geometry/work_area.h"

#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

int main()
{
    using stage_manager::geometry::Point;
    using stage_manager::geometry::Rect;
    using stage_manager::geometry::Region;
    using stage_manager::geometry::fully_within_work_area;
    using stage_manager::geometry::pixels_to_dip_nearest;
    using stage_manager::geometry::preserves_minimum_onscreen;
    using stage_manager::geometry::scale_dip_ceil;
    using stage_manager::geometry::scale_dip_nearest;

    const Rect primary{0, 0, 100, 80};
    CHECK(primary.valid());
    CHECK(!primary.empty());
    CHECK(primary.width() == 100);
    CHECK(primary.height() == 80);
    CHECK(primary.contains(Point{0, 0}));
    CHECK(primary.contains(Point{99, 79}));
    CHECK(!primary.contains(Point{100, 79}));
    CHECK(primary.contains(Rect{10, 10, 20, 20}));

    const Rect overlap{80, 60, 120, 100};
    CHECK(primary.intersects(overlap));
    CHECK((primary.intersection(overlap) == Rect{80, 60, 100, 80}));
    CHECK(!primary.intersects(Rect{100, 0, 120, 20}));
    CHECK(!primary.intersection(Rect{100, 0, 120, 20}).has_value());
    CHECK((!Rect{10, 0, 0, 10}.valid()));
    CHECK((Rect{10, 0, 0, 10}.empty()));

    const Rect negative{-200, -100, -20, -10};
    CHECK(negative.width() == 180);
    CHECK((negative.translated(20, 10) == Rect{-180, -90, 0, 0}));
    const Rect near_limit{std::numeric_limits<std::int64_t>::max() - 10,
                          0,
                          std::numeric_limits<std::int64_t>::max(),
                          10};
    CHECK(!near_limit.translated(1, 0).has_value());

    const std::vector<Rect> disjoint = {
        {20, 20, 30, 30},
        {0, 0, 10, 10},
        {10, 0, 10, 10},
    };
    const auto region = Region::from_disjoint(disjoint);
    CHECK(region.has_value());
    CHECK(region->size() == 2);
    CHECK((region->rectangles()[0] == Rect{0, 0, 10, 10}));
    CHECK((region->rectangles()[1] == Rect{20, 20, 30, 30}));
    CHECK(region->area() == 200);
    CHECK((region->bounds() == Rect{0, 0, 30, 30}));
    CHECK(!Region::from_disjoint(
               std::vector<Rect>{{0, 0, 10, 10}, {5, 5, 15, 15}})
               .has_value());
    CHECK(!Region::from_disjoint(std::vector<Rect>{{10, 0, 0, 10}}).has_value());
    CHECK(!Region::from_disjoint(disjoint, 1).has_value());
    CHECK(Region{}.empty());
    CHECK(!Region{}.bounds().has_value());

    CHECK(scale_dip_ceil(48, 96) == 48);
    CHECK(scale_dip_ceil(48, 144) == 72);
    CHECK(scale_dip_ceil(48, 192) == 96);
    CHECK(scale_dip_ceil(1, 120) == 2);
    CHECK(scale_dip_ceil(48, 0) == 0);
    CHECK(scale_dip_nearest(3, 144) == 5);
    CHECK(scale_dip_nearest(-3, 144) == -5);
    CHECK(pixels_to_dip_nearest(144, 144) == 96);
    CHECK(pixels_to_dip_nearest(10, 0) == 0);

    const Rect work_area{-1920, 0, 0, 1080};
    CHECK(fully_within_work_area(Rect{-1800, 100, -200, 900}, work_area));
    CHECK(!fully_within_work_area(Rect{-2000, 100, -200, 900}, work_area));
    CHECK(preserves_minimum_onscreen(Rect{-2000, 100, -1800, 300}, work_area, 100, 100));
    CHECK(!preserves_minimum_onscreen(Rect{-2000, 100, -1900, 300}, work_area, 100, 100));
    CHECK(!preserves_minimum_onscreen(Rect{-1900, 100, -1800, 200}, work_area, -1, 10));
    return 0;
}
