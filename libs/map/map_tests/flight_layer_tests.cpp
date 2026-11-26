#include "testing/testing.hpp"

#include "map/flight_layer.hpp"

#include "drape_frontend/drape_api.hpp"
#include "drape_frontend/drape_frontend_tests/visual_params_fixture.hpp"

#include "geometry/mercator.hpp"

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

double Azimuth(m2::PointD const & from, m2::PointD const & to)
{
  double const deg = std::atan2(to.x - from.x, to.y - from.y) * 180.0 / M_PI;
  return deg < 0 ? deg + 360 : deg;
}

UNIT_TEST(FlightLayer_NeighbouringSectorsTouch)
{
  m2::PointD const center(10, 10);
  TEST(FlightLayer::MakeSectors(center, 0, 1.0).empty(), ());

  // SE, S and SW of 8 winds are 45 degrees each and touch: 112.5 to 247.5 degrees.
  auto const eight = FlightLayer::MakeSectors(center, (1 << 6) | (1 << 8) | (1 << 10), 1.0);
  TEST_EQUAL(eight.size(), 3, ());
  for (auto const & ring : eight)
  {
    TEST_EQUAL(ring.front(), center, ());
    TEST_EQUAL(ring.back(), center, ());
  }
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, eight[0][1]), 112.5, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, eight[0][eight[0].size() - 2]), 157.5, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, eight[1][1]), 157.5, 1e-6, ("touches SE"));
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, eight[2][eight[2].size() - 2]), 247.5, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(eight[0][1].Length(center), 1.0, 1e-9, ());

  // With a direction of 16 winds, like NNW, each is 22.5 degrees: N from 348.75 to 11.25.
  auto const sixteen = FlightLayer::MakeSectors(center, (1 << 15) | (1 << 0), 1.0);
  TEST_EQUAL(sixteen.size(), 2, ());
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, sixteen[0][1]), 348.75, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, sixteen[0][sixteen[0].size() - 2]), 11.25, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, sixteen[1][sixteen[1].size() - 2]), 348.75, 1e-6, ("touches N"));

  TEST_EQUAL(FlightLayer::MakeSectors(center, 0xFFFF, 1.0).size(), 16, ());

  // Between the radii, like the wreath around a takeoff: SW of 8 winds, from 202.5 to 247.5 degrees.
  auto const wreath = FlightLayer::MakeSectors(center, 1 << 10, 2.0, 1.0);
  TEST_EQUAL(wreath.size(), 1, ());
  auto const & piece = wreath.front();
  TEST_EQUAL(piece.front(), piece.back(), ("closed"));
  TEST_ALMOST_EQUAL_ABS(Azimuth(center, piece.front()), 202.5, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(piece.front().Length(center), 2.0, 1e-9, ());
  TEST_ALMOST_EQUAL_ABS(piece[piece.size() / 2].Length(center), 1.0, 1e-9, ("the inner arc back"));
  for (auto const & p : piece)
    TEST(p.Length(center) > 0.99 && p.Length(center) < 2.01, ());
}

UNIT_TEST(FlightLayer_Circle)
{
  m2::PointD const center(1, 2);
  auto const circle = FlightLayer::MakeCircle(center, 0.5, 16);
  TEST_EQUAL(circle.size(), 17, ());
  TEST(circle.front().EqualDxDy(circle.back(), 1e-12), ());
  for (auto const & p : circle)
    TEST_ALMOST_EQUAL_ABS(p.Length(center), 0.5, 1e-12, ());
}

UNIT_CLASS_TEST(VisualParamsFixture, FlightLayer_HitsSitesAlsoWithoutTheLayer)
{
  df::DrapeApi drapeApi;
  FlightLayer layer(drapeApi);

  auto const center = mercator::FromLatLon(47.2, 11.3);
  auto const screen = MakeScreen(center, 250.0);
  layer.SetViewport(screen);

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
  layer.SetSites({takeoff, landing});

  // About 0.5 m per pixel: 20 px are 10 m.
  TEST_EQUAL(layer.HitTest(mercator::FromLatLon(47.20005, 11.3), screen, 20.0), std::optional<std::string>("pge:1"), ());
  TEST_EQUAL(layer.HitTest(mercator::FromLatLon(47.19, 11.3015), screen, 20.0), std::optional<std::string>("osm:w2"),
             ("inside the area, away from the point"));
  TEST(!layer.HitTest(mercator::FromLatLon(47.21, 11.3), screen, 20.0), ());

  // Further out landings show up in the layer only.
  auto const wider = MakeScreen(center, 2000.0);
  TEST(!layer.HitTest(landing.m_point, wider, 20.0), ());
  layer.SetEnabled(true);
  TEST_EQUAL(layer.HitTest(landing.m_point, wider, 20.0), std::optional<std::string>("osm:w2"), ("in the layer"));
  TEST_EQUAL(layer.HitTest(center, wider, 20.0), std::optional<std::string>("pge:1"), ("in the layer"));

  // Zoomed out of the whole of Europe the sites are hidden.
  auto const far = MakeScreen(center, 5'000'000.0);
  TEST(!layer.HitTest(center, far, 20.0), ());
}
}  // namespace flight_layer_tests
