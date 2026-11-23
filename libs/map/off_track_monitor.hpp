#pragma once

#include "platform/location.hpp"

#include "geometry/point2d.hpp"

#include <string>
#include <vector>

/// Maps&Fly: watches the distance between the GPS position and a chosen track. The position counts as off the track
/// only if it is farther than the threshold plus the GPS accuracy, so a poor fix does not raise an alarm. A state
/// changes only after the new condition has held for some seconds. A fix less accurate than kMaxAccuracyMeters gives
/// "GPS inaccurate" instead of an alarm. Until the position has been on the track once, being away from it is no
/// alarm but NotReached, e.g. on the way to the start of the track.
class OffTrackMonitor
{
public:
  using Line = std::vector<m2::PointD>;

  enum class State
  {
    OnTrack,
    OffTrack,
    GpsInaccurate,
    NotReached,
  };

  static double constexpr kMaxAccuracyMeters = 50;
  static double constexpr kOffTrackDelaySeconds = 10;
  static double constexpr kOnTrackDelaySeconds = 3;
  static double constexpr kGpsInaccurateDelaySeconds = 10;

  /// @param lines - track lines in mercator.
  OffTrackMonitor(std::vector<Line> lines, double thresholdMeters);

  void SetThreshold(double thresholdMeters) { m_thresholdMeters = thresholdMeters; }

  /// @return the state after this fix.
  State Update(location::GpsInfo const & info);

  State GetState() const { return m_state; }
  /// Distance from the last fix to the track, -1 before the first fix.
  double GetDistanceMeters() const { return m_distanceMeters; }

private:
  double DistanceToTrack(m2::PointD const & point) const;

  std::vector<Line> m_lines;
  double m_thresholdMeters;
  State m_state = State::NotReached;
  // The state the recent fixes point to, since m_candidateSince.
  State m_candidate = State::NotReached;
  bool m_reached = false;
  double m_candidateSince = 0;
  double m_distanceMeters = -1;
};

std::string DebugPrint(OffTrackMonitor::State state);
