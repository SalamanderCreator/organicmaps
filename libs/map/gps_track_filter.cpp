#include "map/gps_track_filter.hpp"

#include "geometry/distance_on_sphere.hpp"
#include "geometry/mercator.hpp"

#include "platform/settings.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <numeric>
#include <set>

namespace
{
std::string_view constexpr kMinHorizontalAccuracyKey = "GpsTrackingMinAccuracy";

// Maps&Fly: fixes with a worse horizontal accuracy are skipped. 25 m keeps clean outdoor GPS fixes and drops the
// zigzag of Wi-Fi/cell fixes and of poor GPS in forests or between houses.
double constexpr kMinHorizontalAccuracyMeters = 25;

// Required for points decimation to reduce number of close points.
double constexpr kClosePointDistanceMeters = 10;

// Max acceptable acceleration to filter gps jumps
double constexpr kMaxAcceptableAcceleration = 2;  // m / sec ^ 2

double constexpr kCosine45degrees = 0.70710678118;

m2::PointD GetDirection(location::GpsInfo const & from, location::GpsInfo const & to)
{
  m2::PointD const pt0 = mercator::FromLatLon(from.m_latitude, from.m_longitude);
  m2::PointD const pt1 = mercator::FromLatLon(to.m_latitude, to.m_longitude);
  m2::PointD const d = pt1 - pt0;
  return d.IsAlmostZero() ? m2::PointD::Zero() : d.Normalize();
}

double GetDistance(location::GpsInfo const & from, location::GpsInfo const & to)
{
  return ms::DistanceOnEarth(from.m_latitude, from.m_longitude, to.m_latitude, to.m_longitude);
}

// Slower than walking: the phone reports 0 while standing still.
double constexpr kStandingSpeed = 0.3;  // m / sec

// Faster than any recorded movement, also a paraglider with tailwind or a car on a motorway.
double constexpr kMaxPlausibleSpeed = 200 / 3.6;  // m / sec

// After a rejected fix, this many fixes in a row that agree with each other show that the fix before them was
// the jump, e.g. the first fix of the recording.
size_t constexpr kJumpRecoveryFixCount = 5;

bool IsPlausibleMove(location::GpsInfo const & from, location::GpsInfo const & to)
{
  // Both fixes may be off by their accuracy.
  double const distance = std::max(0.0, GetDistance(from, to) - from.m_horizontalAccuracy - to.m_horizontalAccuracy);
  return distance <= kMaxPlausibleSpeed * (to.m_timestamp - from.m_timestamp);
}

// @param points - in time order.
std::vector<location::GpsInfo> RemoveJumps(std::vector<location::GpsInfo> const & points)
{
  std::vector<location::GpsInfo> result;
  // Rejected fixes in a row that agree with each other.
  std::vector<location::GpsInfo> rejected;
  for (auto const & point : points)
  {
    if (result.empty() || IsPlausibleMove(result.back(), point))
    {
      result.push_back(point);
      rejected.clear();
      continue;
    }

    if (!rejected.empty() && !IsPlausibleMove(rejected.back(), point))
      rejected.clear();
    rejected.push_back(point);
    if (rejected.size() == kJumpRecoveryFixCount)
    {
      while (!result.empty() && !IsPlausibleMove(result.back(), rejected.front()))
        result.pop_back();
      result.insert(result.end(), rejected.begin(), rejected.end());
      rejected.clear();
    }
  }
  return result;
}
}  // namespace

std::vector<location::GpsInfo> SelectAccurateTrackPoints(std::vector<location::GpsInfo> points)
{
  std::stable_sort(points.begin(), points.end(), [](auto const & l, auto const & r)
  {
    if (l.m_timestamp != r.m_timestamp)
      return l.m_timestamp < r.m_timestamp;
    return l.m_horizontalAccuracy < r.m_horizontalAccuracy;
  });
  // While the phone reports standing still, the position drifts by tens of meters with a good accuracy.
  std::erase_if(points, [](auto const & p) { return p.HasSpeed() && p.m_speed < kStandingSpeed; });

  // Keep the most accurate fix of each time.
  points.erase(std::unique(points.begin(), points.end(),
                           [](auto const & l, auto const & r) { return l.m_timestamp == r.m_timestamp; }),
               points.end());

  points = RemoveJumps(points);

  std::vector<size_t> order(points.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&points](size_t l, size_t r)
  { return points[l].m_horizontalAccuracy < points[r].m_horizontalAccuracy; });

  // Indices of the kept fixes, in time order.
  std::set<size_t> kept;
  for (size_t const i : order)
  {
    auto const & point = points[i];
    auto const next = kept.lower_bound(i);
    if (next != kept.end() && GetDistance(points[*next], point) <= point.m_horizontalAccuracy)
      continue;
    if (next != kept.begin() && GetDistance(points[*std::prev(next)], point) <= point.m_horizontalAccuracy)
      continue;
    kept.emplace_hint(next, i);
  }

  std::vector<location::GpsInfo> result;
  result.reserve(kept.size());
  for (size_t const i : kept)
    result.push_back(points[i]);
  return result;
}

void GpsTrackNullFilter::Process(std::vector<location::GpsInfo> const & inPoints,
                                 std::vector<location::GpsInfo> & outPoints)
{
  outPoints.insert(outPoints.end(), inPoints.begin(), inPoints.end());
}

void GpsTrackFilter::StoreMinHorizontalAccuracy(double value)
{
  settings::Set(kMinHorizontalAccuracyKey, value);
}

double GpsTrackFilter::LoadMinHorizontalAccuracy()
{
  double value = kMinHorizontalAccuracyMeters;
  settings::TryGet(kMinHorizontalAccuracyKey, value);
  return value;
}

GpsTrackFilter::GpsTrackFilter() : m_minAccuracy(LoadMinHorizontalAccuracy()) {}

void GpsTrackFilter::Reset()
{
  m_countLastInfo = 0;
  // A changed setting applies when a recording starts or is restored.
  m_minAccuracy = LoadMinHorizontalAccuracy();
}

void GpsTrackFilter::Process(GpsVectorT const & inPoints, GpsVectorT & outPoints)
{
  outPoints.reserve(inPoints.size());

  if (m_minAccuracy == 0)
  {
    // Debugging trick to turn off filtering
    outPoints.insert(outPoints.end(), inPoints.begin(), inPoints.end());
    return;
  }

  for (location::GpsInfo const & currInfo : inPoints)
  {
    // Jump filtering needs the previous fix's speed to estimate acceleration.
    if (!currInfo.HasSpeed())
      continue;

    // An inaccurate fix must not enter the history either: the first fix is accepted unchecked, and the jump
    // filter predicts the next fix from the history.
    if (currInfo.m_horizontalAccuracy > m_minAccuracy)
      continue;

    // A replayed fix (same time and position) must not enter the history: IsGoodVector() would take a zero
    // direction from it and reject the next fix. Other fixes still do, also an older one, so the filter follows
    // a backward clock correction.
    if (m_countLastInfo > 0)
    {
      auto const & lastInfo = GetLastInfo();
      if (currInfo.m_timestamp == lastInfo.m_timestamp && currInfo.GetLatLon() == lastInfo.GetLatLon())
        continue;
    }

    if (m_countLastInfo == 0 || (IsGoodPoint(currInfo) && (m_countLastInfo < 2 || IsGoodVector(currInfo))))
    {
      outPoints.emplace_back(currInfo);
      m_lastAcceptedInfo = currInfo;
    }

    AddLastInfo(currInfo);
  }
}

void GpsTrackFilter::Finalize(GpsVectorT & outPoints)
{
  if (m_countLastInfo > 0)
  {
    // Offer the final fix even if sampling skipped it. The collection filters duplicate positions.
    auto const & info = GetLastInfo();
    auto const & lastAccepted = GetLastAcceptedInfo();
    if (info.m_timestamp > lastAccepted.m_timestamp)
    {
      outPoints.push_back(info);
      m_lastAcceptedInfo = info;
    }
  }
}

bool GpsTrackFilter::IsGoodVector(location::GpsInfo const & info) const
{
  ASSERT_GREATER(m_countLastInfo, 1, ());

  double const predictionDistance = GetDistance(m_lastInfo[1], m_lastInfo[0]);  // meters
  double const realDistance = GetDistance(m_lastInfo[0], info);                 // meters

  m2::PointD const predictionDirection = GetDirection(m_lastInfo[1], m_lastInfo[0]);
  m2::PointD const realDirection = GetDirection(m_lastInfo[0], info);

  // Cosine of angle between prediction direction and real direction is
  double const cosine = m2::DotProduct(predictionDirection, realDirection);

  // Acceptable angle must be from 0 to 45 or from 0 to -45.
  // Acceptable distance must be not great than 2x than predicted, otherwise it is jump.
  return (cosine >= kCosine45degrees && realDistance <= std::max(kClosePointDistanceMeters, 2. * predictionDistance));
}

bool GpsTrackFilter::IsGoodPoint(location::GpsInfo const & info) const
{
  auto const & lastInfo = GetLastInfo();
  auto const & lastAcceptedInfo = GetLastAcceptedInfo();

  // Time spend to move from the last point to the current point, sec:
  double const timeFromLast = info.m_timestamp - lastInfo.m_timestamp;
  if (timeFromLast <= 0.0)
    return false;

  // Distance in meters between last accepted and current point is, meters:
  double const distanceFromLastAccepted = GetDistance(lastAcceptedInfo, info);

  // Filter point by close distance
  if (distanceFromLastAccepted < kClosePointDistanceMeters)
    return false;

  // Filter point if accuracy areas are intersected
  if (distanceFromLastAccepted < lastAcceptedInfo.m_horizontalAccuracy &&
      info.m_horizontalAccuracy > 0.5 * lastAcceptedInfo.m_horizontalAccuracy)
    return false;

  // Distance in meters between last and current point is, meters:
  double const distanceFromLast = GetDistance(lastInfo, info);

  // Speed to move from the last point to the current point
  double const speedFromLast = distanceFromLast / timeFromLast;

  // Filter by acceleration: skip point if it jumps too far in short time
  double const accelerationFromLast = (speedFromLast - lastInfo.m_speed) / timeFromLast;
  if (accelerationFromLast > kMaxAcceptableAcceleration)
    return false;

  return true;
}

location::GpsInfo const & GpsTrackFilter::GetLastInfo() const
{
  ASSERT_GREATER(m_countLastInfo, 0, ());
  return m_lastInfo[0];
}

location::GpsInfo const & GpsTrackFilter::GetLastAcceptedInfo() const
{
  ASSERT_GREATER(m_countLastInfo, 0, ());
  return m_lastAcceptedInfo;
}

void GpsTrackFilter::AddLastInfo(location::GpsInfo const & info)
{
  m_lastInfo[1] = m_lastInfo[0];
  m_lastInfo[0] = info;
  ++m_countLastInfo;
}
