#include "geometry/hit_testing.h"
#include "geometry/interaction_zones.h"
#include "geometry/region.h"

#include <cstdio>
#include <vector>

#define CHECK(condition)                                                                  \
    do {                                                                                  \
        if (!(condition)) {                                                               \
            std::fprintf(stderr, "check failed: %s (line %d)\n", #condition, __LINE__); \
            return 1;                                                                     \
        }                                                                                 \
    } while (false)

namespace {

stage_manager::geometry::Region region_of(stage_manager::geometry::Rect rectangle)
{
    const std::vector<stage_manager::geometry::Rect> rectangles = {rectangle};
    return *stage_manager::geometry::Region::from_disjoint(rectangles);
}

class FakeHitTester final : public stage_manager::geometry::IRootWindowHitTester {
public:
    stage_manager::geometry::HitTarget root_window_at(
        stage_manager::geometry::Point point) const override
    {
        return point == stage_manager::geometry::Point{10, 20} ? 42 : 0;
    }
};

} // namespace

int main()
{
    using stage_manager::geometry::Edge;
    using stage_manager::geometry::Point;
    using stage_manager::geometry::Rect;
    using stage_manager::geometry::Region;
    using stage_manager::geometry::RegionStatus;
    using stage_manager::geometry::has_exposed_edge_segment;
    using stage_manager::geometry::intersect;
    using stage_manager::geometry::make_interaction_zones;
    using stage_manager::geometry::point_belongs_to_window;
    using stage_manager::geometry::subtract;
    using stage_manager::geometry::unite;

    const auto source = region_of({0, 0, 100, 100});
    const auto blocker = region_of({25, 25, 75, 75});
    const auto difference = subtract(source, blocker);
    CHECK(difference.succeeded());
    CHECK(difference.region.size() == 4);
    CHECK(difference.region.area() == 7500);
    for (const auto& piece : difference.region.rectangles()) {
        CHECK(!piece.intersects(Rect{25, 25, 75, 75}));
    }

    const auto restored = unite(difference.region, blocker);
    CHECK(restored.succeeded());
    CHECK(restored.region.size() == 1);
    CHECK((restored.region.rectangles()[0] == Rect{0, 0, 100, 100}));

    const auto overlapping_union = unite(source, region_of({50, 0, 150, 100}));
    CHECK(overlapping_union.succeeded());
    CHECK(overlapping_union.region.area() == 15000);
    const auto clipped = intersect(source, region_of({50, -20, 120, 20}));
    CHECK(clipped.succeeded());
    CHECK(clipped.region.size() == 1);
    CHECK((clipped.region.rectangles()[0] == Rect{50, 0, 100, 20}));
    const auto adjacent_union = unite(
        region_of({0, 0, 10, 10}), region_of({10, 0, 20, 10}), 1);
    CHECK(adjacent_union.succeeded());
    CHECK(adjacent_union.region.size() == 1);
    CHECK((adjacent_union.region.rectangles()[0] == Rect{0, 0, 20, 10}));

    CHECK(subtract(source, blocker, 3).status == RegionStatus::TooComplex);
    CHECK(unite(Region{}, source, 0).status == RegionStatus::TooComplex);

    const auto zones = make_interaction_zones({0, 0, 100, 80}, 10);
    CHECK(zones[0].edge == Edge::Left);
    CHECK((zones[0].bounds == Rect{0, 0, 10, 80}));
    CHECK((zones[1].bounds == Rect{90, 0, 100, 80}));
    CHECK((zones[2].bounds == Rect{0, 0, 100, 10}));
    CHECK((zones[3].bounds == Rect{0, 70, 100, 80}));

    const auto exposed_left = subtract(region_of(zones[0].bounds), region_of({0, 0, 10, 40}));
    CHECK(exposed_left.succeeded());
    CHECK(has_exposed_edge_segment(exposed_left.region, zones[0], 40, 10));
    CHECK(!has_exposed_edge_segment(exposed_left.region, zones[0], 41, 10));
    CHECK(!has_exposed_edge_segment(exposed_left.region, zones[0], 40, 11));
    const auto detached_from_edge = region_of({5, 40, 10, 80});
    CHECK(!has_exposed_edge_segment(detached_from_edge, zones[0], 40, 5));

    const auto shallow_window_zones = make_interaction_zones({0, 0, 5, 4}, 10);
    CHECK((shallow_window_zones[0].bounds == Rect{0, 0, 5, 4}));
    CHECK((shallow_window_zones[2].bounds == Rect{0, 0, 5, 4}));

    FakeHitTester hit_tester;
    CHECK(point_belongs_to_window(hit_tester, Point{10, 20}, 42));
    CHECK(!point_belongs_to_window(hit_tester, Point{10, 20}, 0));
    CHECK(!point_belongs_to_window(hit_tester, Point{11, 20}, 42));
    return 0;
}
