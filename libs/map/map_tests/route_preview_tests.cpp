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
}  // namespace route_preview_tests
