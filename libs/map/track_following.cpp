#include "map/track_following.hpp"

#include "geometry/mercator.hpp"
#include "geometry/parametrized_segment.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

namespace track_following
{
namespace
{
// Less than this is left of the track after the entry: there is nothing to follow.
double constexpr kMinRemainderM = 1.0;

TrackPosition MakePosition(JoinedTrack const & track, size_t index, double t)
{
  auto const & points = track.m_points;
  auto const & dists = track.m_distFromBegM;
  if (index + 1 >= points.size() || t >= 1.0)
  {
    size_t const i = std::min(index + 1, points.size() - 1);
    return {i, points[i], dists[i]};
  }
  if (t <= 0.0)
    return {index, points[index], dists[index]};

  auto const & from = points[index];
  auto const & to = points[index + 1];
  geometry::Altitude altitude = geometry::kInvalidAltitude;
  if (from.GetAltitude() != geometry::kInvalidAltitude && to.GetAltitude() != geometry::kInvalidAltitude)
    altitude =
        static_cast<geometry::Altitude>(std::lround(from.GetAltitude() + t * (to.GetAltitude() - from.GetAltitude())));
  return {index,
          {from.GetPoint() + (to.GetPoint() - from.GetPoint()) * t, altitude},
          dists[index] + t * (dists[index + 1] - dists[index])};
}
}  // namespace

JoinedTrack::JoinedTrack(kml::MultiGeometry const & geometry)
{
  for (auto const & line : geometry.m_lines)
  {
    for (size_t i = 0; i < line.size(); ++i)
    {
      double dist = m_distFromBegM.empty() ? 0.0 : m_distFromBegM.back();
      // The first point of a later line starts after a gap, which has no length on the track.
      if (i > 0)
        dist += mercator::DistanceOnEarth(line[i - 1].GetPoint(), line[i].GetPoint());
      m_points.push_back(line[i]);
      m_distFromBegM.push_back(dist);
    }
  }
}

TrackPosition FindClosest(JoinedTrack const & track, m2::PointD const & point)
{
  auto const & points = track.m_points;
  CHECK(!points.empty(), ());
  if (points.size() == 1)
    return MakePosition(track, 0, 0.0);

  // Mercator is conformal, so the closest point in mercator is also the closest one on the Earth nearby.
  size_t bestIndex = 0;
  double bestT = 0;
  double bestSquaredDist = std::numeric_limits<double>::max();
  for (size_t i = 0; i + 1 < points.size(); ++i)
  {
    auto const & from = points[i].GetPoint();
    auto const & to = points[i + 1].GetPoint();
    auto const closest = m2::ParametrizedSegment<m2::PointD>(from, to).ClosestPointTo(point);
    double const squaredDist = closest.SquaredLength(point);
    if (squaredDist < bestSquaredDist)
    {
      bestSquaredDist = squaredDist;
      bestIndex = i;
      double const squaredLength = from.SquaredLength(to);
      bestT = squaredLength > 0 ? std::sqrt(from.SquaredLength(closest) / squaredLength) : 0.0;
    }
  }
  return MakePosition(track, bestIndex, bestT);
}

TrackPosition AtDistance(JoinedTrack const & track, double distFromBegM)
{
  auto const & dists = track.m_distFromBegM;
  CHECK(!dists.empty(), ());
  if (distFromBegM <= dists.front())
    return MakePosition(track, 0, 0.0);
  if (distFromBegM >= dists.back())
    return MakePosition(track, dists.size() - 1, 0.0);

  // The first point at or beyond the distance ends the section, as Track::GetPoint picks the earlier line at a gap.
  size_t const end = std::lower_bound(dists.begin(), dists.end(), distFromBegM) - dists.begin();
  ASSERT(end > 0 && end < dists.size(), (distFromBegM));
  double const length = dists[end] - dists[end - 1];
  ASSERT_GREATER(length, 0, ());
  return MakePosition(track, end - 1, (distFromBegM - dists[end - 1]) / length);
}

std::shared_ptr<routing::TrackFollowingData const> MakeRemainder(JoinedTrack const & track, TrackPosition const & entry)
{
  auto const & points = track.m_points;
  auto data = std::make_shared<routing::TrackFollowingData>();
  data->m_points.reserve(points.size() - entry.m_index + 1);
  data->m_points.push_back(entry.m_point);
  double lengthM = 0;
  for (size_t i = entry.m_index + 1; i < points.size(); ++i)
  {
    // Gaps count here: the user covers them too.
    lengthM += mercator::DistanceOnEarth(data->m_points.back().GetPoint(), points[i].GetPoint());
    data->m_points.push_back(points[i]);
  }
  if (lengthM < kMinRemainderM)
    return nullptr;
  return data;
}

ElevationInfo::AltitudesInfo GetSectionAltitudes(ElevationInfo const & profile, double fromDistM, double toDistM,
                                                 geometry::Altitude threshold)
{
  std::vector<ElevationInfo::Point> points;
  profile.ForEachPoint([&points](double dist, geometry::Altitude altitude) { points.push_back({dist, altitude}); });
  if (points.empty())
    return {};

  double const lo = std::max(std::min(fromDistM, toDistM), points.front().m_distance);
  double const hi = std::min(std::max(fromDistM, toDistM), points.back().m_distance);
  if (hi <= lo)
    return {};

  auto const altitudeAt = [&points](double dist)
  {
    auto const it = std::lower_bound(points.begin(), points.end(), dist,
                                     [](ElevationInfo::Point const & p, double d) { return p.m_distance < d; });
    if (it == points.begin())
      return it->m_altitude;
    auto const prev = std::prev(it);
    if (it == points.end() || it->m_distance <= prev->m_distance)
      return prev->m_altitude;
    double const t = (dist - prev->m_distance) / (it->m_distance - prev->m_distance);
    return static_cast<geometry::Altitude>(std::lround(prev->m_altitude + t * (it->m_altitude - prev->m_altitude)));
  };

  std::vector<ElevationInfo::Point> section;
  section.push_back({lo, altitudeAt(lo)});
  for (auto const & point : points)
    if (point.m_distance > lo && point.m_distance < hi)
      section.push_back(point);
  section.push_back({hi, altitudeAt(hi)});

  // Walking backwards along the track turns its climbs into descents.
  if (toDistM < fromDistM)
    std::reverse(section.begin(), section.end());

  std::vector<double> distances;
  geometry::Altitudes altitudes = {section.front().m_altitude};
  for (size_t i = 1; i < section.size(); ++i)
  {
    distances.push_back(std::abs(section[i].m_distance - section.front().m_distance));
    altitudes.push_back(section[i].m_altitude);
  }
  ElevationInfo result;
  result.Assign(distances, altitudes);
  return result.CalculateAltitudesInfo(threshold);
}

ElevationInfo::AltitudesInfo GetRemainingAltitudes(ElevationInfo const & profile, double fromDistM)
{
  return GetSectionAltitudes(profile, fromDistM, std::numeric_limits<double>::max(), ElevationInfo::kDefThresholdMWM);
}
}  // namespace track_following
