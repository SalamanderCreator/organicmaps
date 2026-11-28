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

std::optional<std::string> HitSite(FlightLayer const & layer, m2::PointD const & point, ScreenBase const & screen)
{
  auto const hit = layer.HitTest(point, screen, 20.0);
  if (!hit)
    return {};
  TEST(!hit->m_cluster, ());
  return hit->m_siteId;
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
  TEST_EQUAL(HitSite(layer, mercator::FromLatLon(47.20005, 11.3), screen), std::optional<std::string>("pge:1"), ());
  TEST_EQUAL(HitSite(layer, mercator::FromLatLon(47.19, 11.3015), screen), std::optional<std::string>("osm:w2"),
             ("inside the area, away from the point"));
  TEST(!HitSite(layer, mercator::FromLatLon(47.21, 11.3), screen), ());

  // Further out landings show up in the layer only.
  auto const wider = MakeScreen(center, 2000.0);
  TEST(!HitSite(layer, landing.m_point, wider), ());
  layer.SetEnabled(true);
  TEST_EQUAL(HitSite(layer, landing.m_point, wider), std::optional<std::string>("osm:w2"), ("in the layer"));
  TEST_EQUAL(HitSite(layer, center, wider), std::optional<std::string>("pge:1"), ("in the layer"));

  // Zoomed out of the whole of Europe the sites are hidden.
  auto const far = MakeScreen(center, 5'000'000.0);
  TEST(!HitSite(layer, center, far), ());
}
UNIT_CLASS_TEST(VisualParamsFixture, FlightLayer_JoinsNearTakeoffsInTheLayer)
{
  df::DrapeApi drapeApi;
  FlightLayer layer(drapeApi);
  layer.SetEnabled(true);

  auto const center = mercator::FromLatLon(47.2, 11.3);
  auto const makeTakeoff = [](std::string const & id, m2::PointD const & point, uint16_t directions)
  {
    FlightLayer::Site site;
    site.m_id = id;
    site.m_point = point;
    site.m_directions = directions;
    return site;
  };
  // 100 m apart, and one 3 km away.
  auto const east = mercator::FromLatLon(47.2, 11.3013);
  layer.SetSites({makeTakeoff("a", center, 0x1), makeTakeoff("b", east, 0x10),
                  makeTakeoff("c", mercator::FromLatLon(47.227, 11.3), 0x100)});

  // About 10 m per pixel: one symbol between both, tapped to zoom in.
  auto const wide = MakeScreen(center, 10'000.0);
  layer.SetViewport(wide);
  auto const middle = (center + east) / 2.0;
  auto const hit = layer.HitTest(middle, wide, 20.0);
  TEST(hit && hit->m_cluster, ());
  TEST(hit->m_cluster->EqualDxDy(middle, 1e-9), ());
  TEST_EQUAL(HitSite(layer, mercator::FromLatLon(47.227, 11.3), wide), std::optional<std::string>("c"), ("alone"));

  // Closer both come apart.
  auto const close = MakeScreen(center, 1'000.0);
  layer.SetViewport(close);
  TEST_EQUAL(HitSite(layer, center, close), std::optional<std::string>("a"), ());
  TEST_EQUAL(HitSite(layer, east, close), std::optional<std::string>("b"), ());

  // Without the layer they are never joined.
  layer.SetEnabled(false);
  layer.SetViewport(wide);
  TEST(!layer.HitTest(middle, wide, 20.0) || !layer.HitTest(middle, wide, 20.0)->m_cluster, ());
}
UNIT_CLASS_TEST(VisualParamsFixture, FlightLayer_AsksToRateAroundTheScreenInTheLayer)
{
  df::DrapeApi drapeApi;
  FlightLayer layer(drapeApi);
  std::optional<int> askedZoom;
  layer.SetViewportFn([&askedZoom](m2::PointD const &, int zoom) { askedZoom = zoom; });

  FlightLayer::Site site;
  site.m_id = "a";
  site.m_point = mercator::FromLatLon(47.2, 11.3);
  layer.SetSites({site});
  auto const screen = MakeScreen(site.m_point, 10'000.0);
  layer.SetViewport(screen);
  TEST(!askedZoom, ("Only in the layer"));

  layer.SetEnabled(true);
  TEST(askedZoom, ());
  askedZoom.reset();
  layer.SetRatings({{"a", FlightLayer::kGreen, true}, {"unknown", FlightLayer::kRed, false}});
  TEST(askedZoom, ("Redrawn with the new rating"));
  askedZoom.reset();
  layer.SetRatings({{"a", FlightLayer::kGreen, true}});
  TEST(!askedZoom, ("Nothing changed"));
}
}  // namespace flight_layer_tests
