#pragma once

#include "map/elevation_info.hpp"

#include "routing/track_router.hpp"

#include "kml/types.hpp"

#include "geometry/point2d.hpp"

#include <memory>
#include <vector>

/// Maps&Fly: helpers to follow a saved track like a route with routing::TrackRouter.
namespace track_following
{
/// All lines of a track joined in recording order. A gap between lines becomes a straight section.
struct JoinedTrack
{
  explicit JoinedTrack(kml::MultiGeometry const & geometry);

  kml::TrackGeometry m_points;
  /// Distance along the track from its start for each point. Gaps count as zero, as in Track, so these distances
  /// match the ones of the elevation profile and the selected track point.
  std::vector<double> m_distFromBegM;
};

/// A point on a joined track.
struct TrackPosition
{
  /// The position lies on the section [m_index, m_index + 1], or on the last point.
  size_t m_index = 0;
  geometry::PointWithAltitude m_point;
  double m_distFromBegM = 0;
};

/// @param track must have at least one point.
TrackPosition FindClosest(JoinedTrack const & track, m2::PointD const & point);

/// @return the position at the given distance from the start, clamped to the track ends.
TrackPosition AtDistance(JoinedTrack const & track, double distFromBegM);

/// @return the track from |entry| to its end, starting with the entry point itself, or nullptr if the entry is
/// (almost) the track end.
std::shared_ptr<routing::TrackFollowingData const> MakeRemainder(JoinedTrack const & track,
                                                                 TrackPosition const & entry);

/// Ascent and descent of |profile| walking from one distance to the other, in either direction, with the altitudes
/// at both ends interpolated. Distances beyond the profile are clamped to it.
ElevationInfo::AltitudesInfo GetSectionAltitudes(ElevationInfo const & profile, double fromDistM, double toDistM,
                                                 geometry::Altitude threshold);

/// Ascent and descent of |profile| (one line, e.g. of a route) from the distance to its end, with the altitude
/// at that distance interpolated. Uses the threshold of the route profile, so the numbers match its totals.
ElevationInfo::AltitudesInfo GetRemainingAltitudes(ElevationInfo const & profile, double fromDistM);
}  // namespace track_following
