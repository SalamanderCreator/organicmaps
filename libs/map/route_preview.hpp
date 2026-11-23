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
Stats CalculateStats(routing::RouteBase const & route, routing::VehicleType vehicleType);

/// Adds the distances, climbs and times of two routes, e.g. the planned one and a detour from it.
/// Pedestrians get the hiking time of the sum.
Stats Add(Stats const & a, Stats const & b, routing::VehicleType vehicleType);

/// @return |checkpoints| with |pt| inserted where it lengthens the straight lines between them the least.
std::vector<m2::PointD> InsertOptimally(std::vector<m2::PointD> checkpoints, m2::PointD const & pt);

struct RouteProjection
{
  m2::PointD m_point;
  /// Index of the leg between two checkpoints that holds |m_point|.
  size_t m_legIdx = 0;
};

/// @param legs polylines of the route legs, each one with at least two points.
/// @return the route point closest to |pt|.
RouteProjection ProjectToRoute(std::vector<std::vector<m2::PointD>> const & legs, m2::PointD const & pt);

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
