#include "map/route_preview.hpp"

#include "map/elevation_info.hpp"

#include "routing/async_router.hpp"
#include "routing/checkpoint_predictor.hpp"
#include "routing/checkpoints.hpp"

#include "geometry/mercator.hpp"
#include "geometry/parametrized_segment.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <cmath>
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

Stats CalculateStats(RouteBase const & route, VehicleType vehicleType, size_t firstSegmentIdx)
{
  ASSERT(route.IsValid(), ());
  auto const & segments = route.GetRouteSegments();
  ASSERT_LESS(firstSegmentIdx, segments.size(), ());

  double const startDistM = firstSegmentIdx == 0 ? 0.0 : segments[firstSegmentIdx - 1].GetDistFromBeginningMeters();
  double const startTimeSec = firstSegmentIdx == 0 ? 0.0 : segments[firstSegmentIdx - 1].GetTimeFromBeginningSec();

  Stats stats;
  stats.m_distanceM = segments.back().GetDistFromBeginningMeters() - startDistM;
  stats.m_timeSec = route.GetTotalTimeSec() - startTimeSec;

  if (route.HaveAltitudes())
  {
    // Point i is the start of segment i.
    geometry::Altitudes altitudes;
    route.GetAltitudes(altitudes);
    altitudes.erase(altitudes.begin(), altitudes.begin() + firstSegmentIdx);

    std::vector<double> distances;
    distances.reserve(segments.size() - firstSegmentIdx);
    for (size_t i = firstSegmentIdx; i < segments.size(); ++i)
      distances.push_back(segments[i].GetDistFromBeginningMeters() - startDistM);

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
        best = {closest, legIdx, i - 1};
      }
    }
  }
  return best;
}

namespace
{
// Walks |distM| meters along |leg| from |pt| on segment |segmentIdx|, backwards for a negative distance.
// @return nullopt when the walk reaches the leg end.
std::optional<m2::PointD> MoveAlongLeg(std::vector<m2::PointD> const & leg, size_t segmentIdx, m2::PointD pt,
                                       double distM)
{
  bool const forward = distM > 0;
  double left = std::abs(distM);
  size_t idx = forward ? segmentIdx + 1 : segmentIdx;
  while (true)
  {
    auto const & next = leg[idx];
    double const stepM = mercator::DistanceOnEarth(pt, next);
    if (stepM > left)
      return pt + (next - pt) * (left / stepM);
    left -= stepM;
    pt = next;
    if (forward ? idx + 1 == leg.size() : idx == 0)
      return std::nullopt;
    idx = forward ? idx + 1 : idx - 1;
  }
}
}  // namespace

Detour MakeDetour(std::vector<std::vector<m2::PointD>> const & legs, RouteProjection const & projection, double gapM)
{
  CHECK_LESS(projection.m_legIdx, legs.size(), ());
  auto const & leg = legs[projection.m_legIdx];
  CHECK_LESS(projection.m_segmentIdx + 1, leg.size(), ());

  Detour detour;
  detour.m_legIdx = projection.m_legIdx;
  detour.m_leave = MoveAlongLeg(leg, projection.m_segmentIdx, projection.m_point, -gapM);
  detour.m_rejoin = MoveAlongLeg(leg, projection.m_segmentIdx, projection.m_point, gapM);
  return detour;
}

std::vector<std::vector<m2::PointD>> FindDivergingParts(std::vector<std::vector<m2::PointD>> const & reference,
                                                        std::vector<m2::PointD> const & route, double maxDistM)
{
  std::vector<m2::ParametrizedSegment<m2::PointD>> segments;
  for (auto const & line : reference)
    for (size_t i = 1; i < line.size(); ++i)
      segments.emplace_back(line[i - 1], line[i]);

  auto const isOnReference = [&](m2::PointD const & pt)
  {
    m2::RectD const box = mercator::RectByCenterXYAndSizeInMeters(pt, maxDistM);
    for (auto const & segment : segments)
    {
      if (!box.IsIntersect(m2::RectD(segment.GetP0(), segment.GetP1())))
        continue;
      if (mercator::DistanceOnEarth(pt, segment.ClosestPointTo(pt)) <= maxDistM)
        return true;
    }
    return false;
  };

  std::vector<std::vector<m2::PointD>> parts;
  bool previousOn = true;
  for (size_t i = 0; i < route.size(); ++i)
  {
    bool const on = isOnReference(route[i]);
    if (!on)
    {
      if (previousOn)
      {
        auto & part = parts.emplace_back();
        if (i > 0)
          part.push_back(route[i - 1]);
      }
      parts.back().push_back(route[i]);
    }
    else if (!previousOn)
    {
      parts.back().push_back(route[i]);
    }
    previousOn = on;
  }
  // A single point cannot be drawn.
  std::erase_if(parts, [](auto const & part) { return part.size() < 2; });
  return parts;
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
