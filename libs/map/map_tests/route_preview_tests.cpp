#include "testing/testing.hpp"

#include "map/route_preview.hpp"

#include "geometry/mercator.hpp"

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
  TEST_EQUAL(projection.m_segmentIdx, 1, ());

  // Behind the finish: the finish itself.
  projection = ProjectToRoute(legs, {2, 3});
  TEST(projection.m_point.EqualDxDy({2, 2}, 1e-9), (projection.m_point));
  TEST_EQUAL(projection.m_legIdx, 1, ());
}

UNIT_TEST(RoutePreview_MakeDetour)
{
  using route_preview::MakeDetour;
  using route_preview::ProjectToRoute;

  // Two legs of about 1.1 km along the equator, then north.
  double const step = mercator::MetersToMercator(1000.0);
  std::vector<std::vector<m2::PointD>> const legs = {{{0, 0}, {step, 0}}, {{step, 0}, {step, step}, {step, 2 * step}}};
  double constexpr kGapM = 3.0;
  double constexpr kEpsM = 0.1;

  // In the middle of a segment: both points around the projection.
  auto detour = MakeDetour(legs, ProjectToRoute(legs, {step / 2, step / 4}), kGapM);
  TEST_EQUAL(detour.m_legIdx, 0, ());
  TEST(detour.m_leave && detour.m_rejoin, ());
  TEST_ALMOST_EQUAL_ABS(mercator::DistanceOnEarth(*detour.m_leave, {step / 2, 0}), kGapM, kEpsM, ());
  TEST_ALMOST_EQUAL_ABS(mercator::DistanceOnEarth(*detour.m_rejoin, {step / 2, 0}), kGapM, kEpsM, ());
  TEST_LESS(detour.m_leave->x, detour.m_rejoin->x, ());

  // Across a leg point inside a leg: the walk continues on the next segment.
  double const near = mercator::MetersToMercator(1.0);
  detour = MakeDetour(legs, ProjectToRoute(legs, {step + 0.5 * step, step - near}), kGapM);
  TEST_EQUAL(detour.m_legIdx, 1, ());
  TEST(detour.m_leave && detour.m_rejoin, ());
  TEST_ALMOST_EQUAL_ABS(mercator::DistanceOnEarth(*detour.m_rejoin, {step, step}), kGapM - 1.0, kEpsM, ());
  TEST_ALMOST_EQUAL_ABS(detour.m_rejoin->x, step, 1e-12, ());

  // At the finish: the side trip returns to the finish itself.
  detour = MakeDetour(legs, ProjectToRoute(legs, {step, 3 * step}), kGapM);
  TEST_EQUAL(detour.m_legIdx, 1, ());
  TEST(detour.m_leave, ());
  TEST(!detour.m_rejoin, ());

  // At the start: it leaves from the start itself.
  detour = MakeDetour(legs, ProjectToRoute(legs, {-step, 0}), kGapM);
  TEST_EQUAL(detour.m_legIdx, 0, ());
  TEST(!detour.m_leave, ());
  TEST(detour.m_rejoin, ());
}

UNIT_TEST(RoutePreview_FindDivergingParts)
{
  using route_preview::FindDivergingParts;

  double const m = mercator::MetersToMercator(1.0);
  // The planned route goes 1 km east.
  std::vector<std::vector<m2::PointD>> const reference = {{{0, 0}, {1000 * m, 0}}};

  // A variant that follows it 5 m aside, leaves north between 400 m and 600 m and comes back.
  std::vector<m2::PointD> const variant = {{0, 5 * m},         {300 * m, 5 * m}, {400 * m, 5 * m}, {450 * m, 100 * m},
                                           {550 * m, 100 * m}, {600 * m, 5 * m}, {1000 * m, 5 * m}};
  auto const parts = FindDivergingParts(reference, variant, 15.0);
  TEST_EQUAL(parts.size(), 1, ());
  TEST_EQUAL(parts[0], (std::vector<m2::PointD>{variant[2], variant[3], variant[4], variant[5]}), ());

  // The same route: nothing to draw.
  TEST(FindDivergingParts(reference, {{0, 0}, {1000 * m, 0}}, 15.0).empty(), ());

  // A variant that ends off the route keeps its end; one that starts off it keeps its start.
  auto const tail = FindDivergingParts(reference, {{0, 0}, {500 * m, 0}, {500 * m, 200 * m}}, 15.0);
  TEST_EQUAL(tail, (std::vector<std::vector<m2::PointD>>{{{500 * m, 0}, {500 * m, 200 * m}}}), ());
  auto const head = FindDivergingParts(reference, {{0, 200 * m}, {0, 100 * m}, {0, 0}, {500 * m, 0}}, 15.0);
  TEST_EQUAL(head, (std::vector<std::vector<m2::PointD>>{{{0, 200 * m}, {0, 100 * m}, {0, 0}}}), ());
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
