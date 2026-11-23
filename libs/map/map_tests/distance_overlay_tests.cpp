#include "testing/testing.hpp"

#include "map/distance_overlay.hpp"

#include "geometry/mercator.hpp"

namespace distance_overlay_tests
{
UNIT_TEST(DistanceOverlay_CircleHasTheRadiusInMeters)
{
  for (double const lat : {0.0, 47.0, 70.0})
  {
    auto const center = mercator::FromLatLon(lat, 11.0);
    for (double const radius : DistanceOverlay::kCircleRadiiM)
    {
      auto const circle = DistanceOverlay::MakeCircle(center, radius);
      TEST_GREATER(circle.size(), 32, ());
      TEST(circle.front().EqualDxDy(circle.back(), 1e-9), ("closed", lat, radius));
      // The label is drawn at the first point, which lies to the north.
      TEST_ALMOST_EQUAL_ABS(circle.front().x, center.x, 1e-9, (lat, radius));
      TEST_GREATER(circle.front().y, center.y, (lat, radius));
      for (auto const & point : circle)
        TEST_ALMOST_EQUAL_ABS(mercator::DistanceOnEarth(center, point), radius, radius * 0.01, (lat, point));
    }
  }
}
}  // namespace distance_overlay_tests
