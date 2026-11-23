#pragma once

#include "routing/route.hpp"
#include "routing/router.hpp"
#include "routing/vehicle_mask.hpp"

#include "geometry/point2d.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace routing
{
class AsyncRouter;
}

// Maps&Fly: routes calculated next to the user's route, e.g. to show how far a tapped point is.
namespace route_preview
{
struct Stats
{
  double m_distanceM = 0.0;
  double m_timeSec = 0.0;
  uint32_t m_ascentM = 0;
  uint32_t m_descentM = 0;
  bool m_hasAltitudes = false;
};

/// Hiking time after DIN 33466: 4 km/h on the flat, 300 m/h up and 500 m/h down;
/// the smaller of the horizontal and the vertical time counts half.
double EstimateHikingTimeSec(double distanceM, double ascentM, double descentM);

/// Pedestrians get the hiking time; other vehicles keep the router's ETA.
/// @param firstSegmentIdx counts only the route from this segment on, e.g. what is left while navigating.
Stats CalculateStats(routing::RouteBase const & route, routing::VehicleType vehicleType, size_t firstSegmentIdx = 0);

/// Adds the distances, climbs and times of two routes, e.g. the planned one and a detour from it.
/// Pedestrians get the hiking time of the sum.
Stats Add(Stats const & a, Stats const & b, routing::VehicleType vehicleType);

/// @return |route| where the part |removed| is replaced by |added|, e.g. a stretch of the planned route by a detour.
/// Pedestrians get the hiking time of the result.
Stats Replace(Stats const & route, Stats const & removed, Stats const & added, routing::VehicleType vehicleType);

/// @return |checkpoints| with |pt| inserted where it lengthens the straight lines between them the least.
std::vector<m2::PointD> InsertOptimally(std::vector<m2::PointD> checkpoints, m2::PointD const & pt);

struct RouteProjection
{
  m2::PointD m_point;
  /// Index of the leg between two checkpoints that holds |m_point|.
  size_t m_legIdx = 0;
  /// Index of the segment of that leg, from leg point m_segmentIdx to the next one.
  size_t m_segmentIdx = 0;
};

/// @param legs polylines of the route legs, each one with at least two points.
/// @return the route point closest to |pt|.
RouteProjection ProjectToRoute(std::vector<std::vector<m2::PointD>> const & legs, m2::PointD const & pt);

/// Where a side trip to a place leaves the route and where it joins it again. Both lie a few meters apart around
/// the closest route point, because the router refuses equal route points. A missing one is a checkpoint of the leg:
/// a side trip from the leg start or to the leg end needs no extra stop there.
struct Detour
{
  size_t m_legIdx = 0;
  std::optional<m2::PointD> m_leave;
  std::optional<m2::PointD> m_rejoin;
};

/// @param gapM distance of the leave and rejoin points from the closest route point along the route.
Detour MakeDetour(std::vector<std::vector<m2::PointD>> const & legs, RouteProjection const & projection, double gapM);

/// @param reference polylines of the planned route.
/// @return the parts of |route| farther than |maxDistM| from |reference|, each one extended by a point on both ends
/// where possible, so it joins the reference.
std::vector<std::vector<m2::PointD>> FindDivergingParts(std::vector<std::vector<m2::PointD>> const & reference,
                                                        std::vector<m2::PointD> const & route, double maxDistM);

/// Runs its own router next to the routing session, so the user's route stays untouched.
/// A new request cancels the previous one. Call from the GUI thread; callbacks come on it.
class Calculator
{
public:
  using RouterFactory = std::function<std::unique_ptr<routing::IRouter>(routing::VehicleType)>;
  /// The route is nullptr when it could not be built.
  using Callback = std::function<void(routing::RouteBase const * route)>;

  explicit Calculator(RouterFactory && routerFactory);
  ~Calculator();

  void Calculate(std::vector<m2::PointD> && checkpoints, routing::VehicleType vehicleType, Callback && callback);
  void Cancel();

private:
  RouterFactory m_routerFactory;
  std::unique_ptr<routing::AsyncRouter> m_router;
  std::optional<routing::VehicleType> m_vehicleType;
};
}  // namespace route_preview
