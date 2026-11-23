#include "map/route_preview.hpp"

#include "map/elevation_info.hpp"

#include "routing/async_router.hpp"
#include "routing/checkpoint_predictor.hpp"
#include "routing/checkpoints.hpp"

#include "geometry/parametrized_segment.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <limits>
#include <utility>

namespace route_preview
{
using namespace routing;

double EstimateHikingTimeSec(double distanceM, double ascentM, double descentM)
{
  double constexpr kFlatMetersPerHour = 4000.0;
  double constexpr kAscentMetersPerHour = 300.0;
  double constexpr kDescentMetersPerHour = 500.0;

  double const horizontalH = distanceM / kFlatMetersPerHour;
  double const verticalH = ascentM / kAscentMetersPerHour + descentM / kDescentMetersPerHour;
  return (std::max(horizontalH, verticalH) + std::min(horizontalH, verticalH) / 2.0) * 3600.0;
}

Stats CalculateStats(RouteBase const & route, VehicleType vehicleType)
{
  ASSERT(route.IsValid(), ());
  auto const & segments = route.GetRouteSegments();

  Stats stats;
  stats.m_distanceM = segments.back().GetDistFromBeginningMeters();
  stats.m_timeSec = route.GetTotalTimeSec();

  if (route.HaveAltitudes())
  {
    geometry::Altitudes altitudes;
    route.GetAltitudes(altitudes);

    std::vector<double> distances;
    distances.reserve(segments.size());
    for (auto const & s : segments)
      distances.push_back(s.GetDistFromBeginningMeters());

    ElevationInfo ei;
    ei.Assign(distances, altitudes);
    auto const info = ei.CalculateAltitudesInfo(ElevationInfo::kDefThresholdMWM);
    stats.m_ascentM = info.GetTotalAscent();
    stats.m_descentM = info.GetTotalDescent();
    stats.m_hasAltitudes = true;
  }

  if (vehicleType == VehicleType::Pedestrian)
    stats.m_timeSec = EstimateHikingTimeSec(stats.m_distanceM, stats.m_ascentM, stats.m_descentM);

  return stats;
}

Stats Add(Stats const & a, Stats const & b, VehicleType vehicleType)
{
  Stats sum;
  sum.m_distanceM = a.m_distanceM + b.m_distanceM;
  sum.m_timeSec = a.m_timeSec + b.m_timeSec;
  sum.m_ascentM = a.m_ascentM + b.m_ascentM;
  sum.m_descentM = a.m_descentM + b.m_descentM;
  sum.m_hasAltitudes = a.m_hasAltitudes && b.m_hasAltitudes;
  if (vehicleType == VehicleType::Pedestrian)
    sum.m_timeSec = EstimateHikingTimeSec(sum.m_distanceM, sum.m_ascentM, sum.m_descentM);
  return sum;
}

std::vector<m2::PointD> InsertOptimally(std::vector<m2::PointD> checkpoints, m2::PointD const & pt)
{
  CHECK_GREATER_OR_EQUAL(checkpoints.size(), 2, ());
  // The same choice as RoutingManager::AddRoutePoint() with optimization makes for a new stop.
  std::vector<m2::PointD> const intermediates(checkpoints.begin() + 1, checkpoints.end() - 1);
  size_t const idx = CheckpointPredictor(checkpoints.front(), checkpoints.back()).PredictPosition(intermediates, pt);
  checkpoints.insert(checkpoints.begin() + 1 + idx, pt);
  return checkpoints;
}

RouteProjection ProjectToRoute(std::vector<std::vector<m2::PointD>> const & legs, m2::PointD const & pt)
{
  CHECK(!legs.empty(), ());
  RouteProjection best;
  double bestDistSq = std::numeric_limits<double>::max();
  for (size_t legIdx = 0; legIdx < legs.size(); ++legIdx)
  {
    auto const & leg = legs[legIdx];
    ASSERT_GREATER_OR_EQUAL(leg.size(), 2, ());
    for (size_t i = 1; i < leg.size(); ++i)
    {
      m2::ParametrizedSegment<m2::PointD> const segment(leg[i - 1], leg[i]);
      auto const closest = segment.ClosestPointTo(pt);
      double const distSq = closest.SquaredLength(pt);
      if (distSq < bestDistSq)
      {
        bestDistSq = distSq;
        best = {closest, legIdx};
      }
    }
  }
  return best;
}

Calculator::Calculator(RouterFactory && routerFactory) : m_routerFactory(std::move(routerFactory)) {}

Calculator::~Calculator()
{
  Cancel();
}

void Calculator::Calculate(std::vector<m2::PointD> && checkpoints, VehicleType vehicleType, Callback && callback)
{
  CHECK_GREATER_OR_EQUAL(checkpoints.size(), 2, ());

  if (!m_router)
    m_router = std::make_unique<AsyncRouter>(nullptr /* pointCheckCallback */);
  if (m_vehicleType != vehicleType)
  {
    m_router->SetRouter(m_routerFactory(vehicleType), nullptr /* finder */);
    m_vehicleType = vehicleType;
  }

  // A new request or Cancel() cancels the previous one, and AsyncRouter drops its queued callbacks.
  auto const cb = std::make_shared<Callback>(std::move(callback));
  // Long walks take the router many seconds; a preview is of no use then.
  uint32_t constexpr kTimeoutSec = 20;
  m_router->CalculateRoute(Checkpoints(std::move(checkpoints)), m2::PointD::Zero() /* direction */,
                           false /* adjustToPrevRoute */, false /* needAlternatives */,
                           [cb](std::shared_ptr<RoutesResult> result, RouterResultCode code)
  {
    bool const ok = code == RouterResultCode::NoError && result && result->IsValid();
    (*cb)(ok ? &result->GetActive() : nullptr);
  }, nullptr /* needMoreMapsCallback */, [cb](RouterResultCode) { (*cb)(nullptr); }, nullptr /* progressCallback */,
                           kTimeoutSec);
}

void Calculator::Cancel()
{
  if (m_router)
    m_router->ClearState();
}
}  // namespace route_preview
