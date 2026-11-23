#include "map/off_track_monitor.hpp"

#include "geometry/mercator.hpp"
#include "geometry/parametrized_segment.hpp"

#include "base/assert.hpp"

#include <algorithm>
#include <limits>

OffTrackMonitor::OffTrackMonitor(std::vector<Line> lines, double thresholdMeters)
  : m_lines(std::move(lines))
  , m_thresholdMeters(thresholdMeters)
{
  ASSERT(!m_lines.empty(), ());
}

double OffTrackMonitor::DistanceToTrack(m2::PointD const & point)
{
  // Mercator is conformal, so the closest point in mercator is also the closest one on the Earth nearby.
  double minDistance = std::numeric_limits<double>::max();
  auto const consider = [&](m2::PointD const & closest)
  {
    double const distance = mercator::DistanceOnEarth(point, closest);
    if (distance < minDistance)
    {
      minDistance = distance;
      m_closestPoint = closest;
    }
  };
  for (auto const & line : m_lines)
  {
    if (line.size() == 1)
      consider(line.front());
    for (size_t i = 1; i < line.size(); ++i)
      consider(m2::ParametrizedSegment<m2::PointD>(line[i - 1], line[i]).ClosestPointTo(point));
  }
  return minDistance;
}

OffTrackMonitor::State OffTrackMonitor::Update(location::GpsInfo const & info)
{
  m_distanceMeters = DistanceToTrack(mercator::FromLatLon(info.m_latitude, info.m_longitude));

  State current;
  if (info.m_horizontalAccuracy > kMaxAccuracyMeters)
    current = State::GpsInaccurate;
  else if (m_distanceMeters > m_thresholdMeters + info.m_horizontalAccuracy)
    current = m_reached ? State::OffTrack : State::NotReached;
  else
    current = State::OnTrack;

  if (current != m_candidate)
  {
    m_candidate = current;
    m_candidateSince = info.m_timestamp;
  }

  if (m_candidate != m_state)
  {
    double delay = kOnTrackDelaySeconds;
    // Reaching the track and the info that it is not reached yet are shown at once.
    if (m_candidate == State::NotReached || (m_state == State::NotReached && m_candidate == State::OnTrack))
      delay = 0;
    else if (m_candidate == State::OffTrack)
      delay = kOffTrackDelaySeconds;
    else if (m_candidate == State::GpsInaccurate)
      delay = kGpsInaccurateDelaySeconds;
    if (info.m_timestamp - m_candidateSince >= delay)
      m_state = m_candidate;
  }
  if (m_state == State::OnTrack)
    m_reached = true;
  return m_state;
}

std::string DebugPrint(OffTrackMonitor::State state)
{
  switch (state)
  {
  case OffTrackMonitor::State::OnTrack: return "OnTrack";
  case OffTrackMonitor::State::OffTrack: return "OffTrack";
  case OffTrackMonitor::State::GpsInaccurate: return "GpsInaccurate";
  case OffTrackMonitor::State::NotReached: return "NotReached";
  }
  UNREACHABLE();
}
