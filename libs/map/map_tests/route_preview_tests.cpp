#include "testing/testing.hpp"

#include "map/route_preview.hpp"

namespace route_preview_tests
{
using route_preview::EstimateHikingTimeSec;

UNIT_TEST(RoutePreview_HikingTime)
{
  double constexpr kHour = 3600.0;
  double constexpr kEps = 1.0;

  // Flat: 4 km/h.
  TEST_ALMOST_EQUAL_ABS(EstimateHikingTimeSec(8000.0, 0.0, 0.0), 2.0 * kHour, kEps, ());
  // Vertical 600/300 + 200/500 = 2.4 h dominates the horizontal 2 h, which counts half.
  TEST_ALMOST_EQUAL_ABS(EstimateHikingTimeSec(8000.0, 600.0, 200.0), 3.4 * kHour, kEps, ());
  // Steep: horizontal 0.5 h counts half next to the vertical 3 h.
  TEST_ALMOST_EQUAL_ABS(EstimateHikingTimeSec(2000.0, 900.0, 0.0), 3.25 * kHour, kEps, ());
  TEST_EQUAL(EstimateHikingTimeSec(0.0, 0.0, 0.0), 0.0, ());
}

UNIT_TEST(RoutePreview_InsertOptimally)
{
  using route_preview::InsertOptimally;

  // Start, stop, finish on a line: a point near the second half goes after the stop.
  std::vector<m2::PointD> const points = {{0, 0}, {1, 0}, {2, 0}};
  TEST_EQUAL(InsertOptimally(points, {1.5, 0.1}), (std::vector<m2::PointD>{{0, 0}, {1, 0}, {1.5, 0.1}, {2, 0}}), ());
  TEST_EQUAL(InsertOptimally(points, {0.5, -0.1}), (std::vector<m2::PointD>{{0, 0}, {0.5, -0.1}, {1, 0}, {2, 0}}), ());
  TEST_EQUAL(InsertOptimally({{0, 0}, {2, 0}}, {1, 1}), (std::vector<m2::PointD>{{0, 0}, {1, 1}, {2, 0}}), ());
}

UNIT_TEST(RoutePreview_ProjectToRoute)
{
  using route_preview::ProjectToRoute;

  // Two legs: along x to the stop, then up along y.
  std::vector<std::vector<m2::PointD>> const legs = {{{0, 0}, {1, 0}, {2, 0}}, {{2, 0}, {2, 1}, {2, 2}}};

  auto projection = ProjectToRoute(legs, {0.5, 0.3});
  TEST(projection.m_point.EqualDxDy({0.5, 0}, 1e-9), (projection.m_point));
  TEST_EQUAL(projection.m_legIdx, 0, ());

  projection = ProjectToRoute(legs, {2.4, 1.5});
  TEST(projection.m_point.EqualDxDy({2, 1.5}, 1e-9), (projection.m_point));
  TEST_EQUAL(projection.m_legIdx, 1, ());

  // Behind the finish: the finish itself.
  projection = ProjectToRoute(legs, {2, 3});
  TEST(projection.m_point.EqualDxDy({2, 2}, 1e-9), (projection.m_point));
  TEST_EQUAL(projection.m_legIdx, 1, ());
}

UNIT_TEST(RoutePreview_AddStats)
{
  using route_preview::Stats;

  Stats const route{8000.0 /* distance */, 100.0 /* time */, 600 /* ascent */, 100 /* descent */, true};
  Stats const detour{2000.0, 50.0, 0, 100, true};

  auto const walk = route_preview::Add(route, detour, routing::VehicleType::Pedestrian);
  TEST_ALMOST_EQUAL_ABS(walk.m_distanceM, 10000.0, 1e-9, ());
  TEST_EQUAL(walk.m_ascentM, 600, ());
  TEST_EQUAL(walk.m_descentM, 200, ());
  // Hiking time of the sum: horizontal 2.5 h, vertical 2.4 h.
  TEST_ALMOST_EQUAL_ABS(walk.m_timeSec, (2.5 + 1.2) * 3600.0, 1.0, ());
  TEST(walk.m_hasAltitudes, ());

  auto const bike = route_preview::Add(route, Stats{2000.0, 50.0, 0, 0, false}, routing::VehicleType::Bicycle);
  TEST_ALMOST_EQUAL_ABS(bike.m_timeSec, 150.0, 1e-9, ());
  TEST(!bike.m_hasAltitudes, ());
}
}  // namespace route_preview_tests
