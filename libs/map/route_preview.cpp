#include "map/route_preview.hpp"

#include "map/elevation_info.hpp"

#include "routing/async_router.hpp"
#include "routing/checkpoints.hpp"

#include "base/assert.hpp"

#include <algorithm>
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
