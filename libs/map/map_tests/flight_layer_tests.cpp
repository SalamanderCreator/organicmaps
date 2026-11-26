#include "testing/testing.hpp"

#include "map/flight_layer.hpp"

#include "drape_frontend/drape_api.hpp"
#include "drape_frontend/drape_frontend_tests/visual_params_fixture.hpp"

#include "geometry/mercator.hpp"

#include "base/math.hpp"

#include <cmath>

namespace flight_layer_tests
{
using df::test_support::VisualParamsFixture;

ScreenBase MakeScreen(m2::PointD const & center, double sizeM)
{
  ScreenBase screen;
  screen.OnSize(0, 0, 1000, 1000);
  screen.SetFromRect(m2::AnyRectD(mercator::RectByCenterXYAndSizeInMeters(center, sizeM)));
  return screen;
}

UNIT_TEST(FlightLayer_CellKey)
{
  TEST_EQUAL(FlightLayer::CellKey(47.5, 11.5), (47 + 90) * 360 + (11 + 180), ());
  TEST_EQUAL(FlightLayer::CellKey(-0.5, -0.5), 89 * 360 + 179, ());
  TEST_EQUAL(FlightLayer::CellKey(90.0, 180.0), 179 * 360 + 359, ());
}

UNIT_TEST(FlightLayer_CellsInRect)
{
  m2::RectD const rect(mercator::FromLatLon(46.5, 10.5), mercator::FromLatLon(47.5, 11.5));
  auto const cells = FlightLayer::CellsInRect(rect, 4);
  TEST_EQUAL(cells.size(), 4, ());
  TEST_EQUAL(cells.front(), FlightLayer::CellKey(46.5, 10.5), ());
  TEST_EQUAL(cells.back(), FlightLayer::CellKey(47.5, 11.5), ());
  TEST(FlightLayer::CellsInRect(rect, 3).empty(), ("too many cells"));
}

UNIT_TEST(FlightLayer_DirectionStar)
{
  auto const center = mercator::FromLatLon(47.0, 11.0);
  // North (bit 0) and west (bit 12).
  auto const star = FlightLayer::MakeDirectionStar(center, (1 << 0) | (1 << 12), 100.0);
  TEST_EQUAL(star.size(), 5, ());
  TEST_EQUAL(star.front(), center, ());
  TEST_EQUAL(star.back(), center, ());
  TEST_ALMOST_EQUAL_ABS(mercator::DistanceOnEarth(center, star[1]), 100.0, 1.0, ());
  TEST_ALMOST_EQUAL_ABS(star[1].x, center.x, 1e-9, ("north"));
  TEST_GREATER(star[1].y, center.y, ("north"));
  TEST_LESS(star[3].x, center.x, ("west"));
  TEST_ALMOST_EQUAL_ABS(star[3].y, center.y, 1e-9, ("west"));
  TEST(FlightLayer::MakeDirectionStar(center, 0, 100.0).empty(), ());
}

UNIT_CLASS_TEST(VisualParamsFixture, FlightLayer_RequestsCellsAndHitsSites)
{
  df::DrapeApi drapeApi;
  FlightLayer layer(drapeApi);

  auto const center = mercator::FromLatLon(47.2, 11.3);
  std::vector<int> requested;
  layer.SetCellsRequestFn([&requested](std::vector<int> const & cells)
  { requested.insert(requested.end(), cells.begin(), cells.end()); });
  // A 5 km wide screen is at a zoom of more than kLoadZoom.
  auto const screen = MakeScreen(center, 5000.0);
  layer.SetViewport(screen);
  TEST(requested.empty(), ("disabled"));

  layer.SetEnabled(true);
  TEST_EQUAL(requested, std::vector<int>{FlightLayer::CellKey(47.2, 11.3)}, ());
  layer.SetViewport(screen);
  TEST_EQUAL(requested.size(), 1, ("asked once"));

  FlightLayer::Site takeoff;
  takeoff.m_id = "pge:1";
  takeoff.m_point = center;
  FlightLayer::Site landing;
  landing.m_id = "osm:w2";
  landing.m_type = FlightLayer::Site::Type::Landing;
  landing.m_point = mercator::FromLatLon(47.19, 11.3);
  auto const areaRect = mercator::RectByCenterXYAndSizeInMeters(landing.m_point, 400.0);
  landing.m_area = {areaRect.LeftBottom(), areaRect.LeftTop(), areaRect.RightTop(), areaRect.RightBottom(),
                    areaRect.LeftBottom()};
  layer.SetSites(requested.front(), {takeoff, landing});

  // 1000 px for 5 km: 20 px are 100 m.
  TEST_EQUAL(layer.HitTest(mercator::FromLatLon(47.2003, 11.3), screen, 20.0), std::optional<std::string>("pge:1"), ());
  TEST_EQUAL(layer.HitTest(mercator::FromLatLon(47.19, 11.3015), screen, 20.0), std::optional<std::string>("osm:w2"),
             ("inside the area, away from the point"));
  TEST(!layer.HitTest(mercator::FromLatLon(47.21, 11.3), screen, 20.0), ());

  layer.SetEnabled(false);
  TEST(!layer.HitTest(center, screen, 20.0), ("disabled"));
}
}  // namespace flight_layer_tests
