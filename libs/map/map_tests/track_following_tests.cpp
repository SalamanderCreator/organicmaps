#include "testing/testing.hpp"

#include "map/track_following.hpp"

#include "geometry/mercator.hpp"

namespace track_following_tests
{
using namespace track_following;

// On the equator 0.001 degrees of longitude are about 111 m.
geometry::PointWithAltitude Pt(double lon, geometry::Altitude altitude = geometry::kInvalidAltitude)
{
  return {mercator::FromLatLon(0, lon), altitude};
}

double Meters(double lonFrom, double lonTo)
{
  return mercator::DistanceOnEarth(mercator::FromLatLon(0, lonFrom), mercator::FromLatLon(0, lonTo));
}

// Two lines with a gap between 0.001 and 0.002.
kml::MultiGeometry MakeGeometry()
{
  kml::MultiGeometry geometry;
  geometry.m_lines.push_back({Pt(0, 100), Pt(0.001, 200)});
  geometry.m_lines.push_back({Pt(0.002, 300), Pt(0.003, 500)});
  return geometry;
}

UNIT_TEST(TrackFollowing_JoinedTrackSkipsGaps)
{
  JoinedTrack const track(MakeGeometry());
  TEST_EQUAL(track.m_points.size(), 4, ());
  double const section = Meters(0, 0.001);
  TEST_ALMOST_EQUAL_ABS(track.m_distFromBegM[0], 0.0, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(track.m_distFromBegM[1], section, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(track.m_distFromBegM[2], section, 1e-6, ());
  TEST_ALMOST_EQUAL_ABS(track.m_distFromBegM[3], 2 * section, 1e-3, ());
}

UNIT_TEST(TrackFollowing_FindClosest)
{
  JoinedTrack const track(MakeGeometry());
  double const section = Meters(0, 0.001);

  // 50 m north of the middle of the second line.
  auto const pos = FindClosest(track, mercator::FromLatLon(0.00045, 0.0025));
  TEST_EQUAL(pos.m_index, 2, ());
  TEST(pos.m_point.GetPoint().EqualDxDy(mercator::FromLatLon(0, 0.0025), 1e-9), (pos.m_point));
  TEST_EQUAL(pos.m_point.GetAltitude(), 400, ());
  TEST_ALMOST_EQUAL_ABS(pos.m_distFromBegM, 1.5 * section, 0.01, ());

  // Before the start.
  auto const start = FindClosest(track, mercator::FromLatLon(0, -0.01));
  TEST_EQUAL(start.m_index, 0, ());
  TEST_EQUAL(start.m_point, Pt(0, 100), ());
  TEST_ALMOST_EQUAL_ABS(start.m_distFromBegM, 0.0, 1e-6, ());

  // After the end.
  auto const end = FindClosest(track, mercator::FromLatLon(0, 0.01));
  TEST_EQUAL(end.m_index, 3, ());
  TEST_EQUAL(end.m_point, Pt(0.003, 500), ());
}

UNIT_TEST(TrackFollowing_FindClosestOnRoundTrip)
{
  // A round trip: east, north, back west and south to the start.
  kml::MultiGeometry geometry;
  geometry.m_lines.push_back({Pt(0),
                              Pt(0.001),
                              {mercator::FromLatLon(0.001, 0.001), geometry::kInvalidAltitude},
                              {mercator::FromLatLon(0.001, 0), geometry::kInvalidAltitude},
                              Pt(0)});
  JoinedTrack const track(geometry);
  auto const nearStart = mercator::FromLatLon(-0.0002, -0.0002);

  // Forward from the start, backwards from the end: both are at the same place.
  auto const forward = FindClosest(track, nearStart);
  TEST_ALMOST_EQUAL_ABS(forward.m_distFromBegM, 0.0, 1e-6, ());
  auto const backward = FindClosest(track, nearStart, true /* reverse */);
  TEST_ALMOST_EQUAL_ABS(backward.m_distFromBegM, track.m_distFromBegM.back(), 1e-6, ());
  TEST(MakeRemainder(track, backward, true /* reverse */), ());

  // Away from the shared start, the direction does not matter.
  auto const side = mercator::FromLatLon(0.0005, 0.0012);
  TEST_EQUAL(FindClosest(track, side).m_index, 1, ());
  TEST_EQUAL(FindClosest(track, side, true /* reverse */).m_index, 1, ());
}

UNIT_TEST(TrackFollowing_AtDistance)
{
  JoinedTrack const track(MakeGeometry());
  double const section = Meters(0, 0.001);

  TEST_EQUAL(AtDistance(track, -5).m_point, Pt(0, 100), ());
  TEST_EQUAL(AtDistance(track, 1e6).m_point, Pt(0.003, 500), ());
  // A distance at the gap gives the end of the earlier line, as Track::GetPoint() does.
  auto const gap = AtDistance(track, section);
  TEST_EQUAL(gap.m_index, 1, ());
  TEST_EQUAL(gap.m_point, Pt(0.001, 200), ());

  auto const pos = AtDistance(track, 1.25 * section);
  TEST_EQUAL(pos.m_index, 2, ());
  TEST(pos.m_point.GetPoint().EqualDxDy(mercator::FromLatLon(0, 0.00225), 1e-9), (pos.m_point));
  TEST_EQUAL(pos.m_point.GetAltitude(), 350, ());
  TEST_ALMOST_EQUAL_ABS(pos.m_distFromBegM, 1.25 * section, 1e-6, ());
}

UNIT_TEST(TrackFollowing_MakeRemainder)
{
  JoinedTrack const track(MakeGeometry());
  double const section = Meters(0, 0.001);

  // The whole track keeps the gap as a straight section.
  auto const whole = MakeRemainder(track, AtDistance(track, 0));
  TEST(whole, ());
  TEST_EQUAL(whole->m_points, track.m_points, ());
  TEST(whole->m_stopPointIndexes.empty(), ());

  auto const half = MakeRemainder(track, AtDistance(track, 1.5 * section));
  TEST(half, ());
  TEST_EQUAL(half->m_points.size(), 2, ());
  TEST_EQUAL(half->m_points.back(), Pt(0.003, 500), ());
  TEST_EQUAL(half->m_points.front().GetAltitude(), 400, ());

  // From the gap: the end of the first line, then the second line.
  auto const gap = MakeRemainder(track, AtDistance(track, section));
  TEST(gap, ());
  TEST_EQUAL(gap->m_points.size(), 3, ());
  TEST_EQUAL(gap->m_points.front(), Pt(0.001, 200), ());

  TEST(!MakeRemainder(track, AtDistance(track, 2 * section)), ());
  TEST(!MakeRemainder(track, FindClosest(track, mercator::FromLatLon(0, 1))), ());
}

UNIT_TEST(TrackFollowing_MakeReverseRemainder)
{
  JoinedTrack const track(MakeGeometry());
  double const section = Meters(0, 0.001);

  // From the end back to the start, keeping the gap as a straight section.
  auto const whole = MakeRemainder(track, AtDistance(track, 2 * section), true /* reverse */);
  TEST(whole, ());
  TEST_EQUAL(whole->m_points.size(), 4, ());
  TEST_EQUAL(whole->m_points.front(), Pt(0.003, 500), ());
  TEST_EQUAL(whole->m_points.back(), Pt(0, 100), ());

  // From the middle of the second line: the entry, then the start of that line and the first line backwards.
  auto const half = MakeRemainder(track, AtDistance(track, 1.5 * section), true /* reverse */);
  TEST(half, ());
  TEST_EQUAL(half->m_points.size(), 4, ());
  TEST_EQUAL(half->m_points.front().GetAltitude(), 400, ());
  TEST_EQUAL(half->m_points[1], Pt(0.002, 300), ());

  // From the gap: the end of the first line, then its start.
  auto const gap = MakeRemainder(track, AtDistance(track, section), true /* reverse */);
  TEST(gap, ());
  TEST_EQUAL(gap->m_points.size(), 2, ());
  TEST_EQUAL(gap->m_points.back(), Pt(0, 100), ());

  TEST(!MakeRemainder(track, AtDistance(track, 0), true /* reverse */), ());
}

UNIT_TEST(TrackFollowing_MissingAltitude)
{
  kml::MultiGeometry geometry;
  geometry.m_lines.push_back({Pt(0, 100), Pt(0.001)});
  JoinedTrack const track(geometry);
  TEST_EQUAL(AtDistance(track, Meters(0, 0.0005)).m_point.GetAltitude(), geometry::kInvalidAltitude, ());
  TEST_EQUAL(AtDistance(track, 0).m_point.GetAltitude(), 100, ());
}

UNIT_TEST(TrackFollowing_SinglePoint)
{
  kml::MultiGeometry geometry;
  geometry.m_lines.push_back({Pt(0, 100)});
  JoinedTrack const track(geometry);
  auto const pos = FindClosest(track, mercator::FromLatLon(0, 1));
  TEST_EQUAL(pos.m_point, Pt(0, 100), ());
  TEST(!MakeRemainder(track, pos), ());
}

UNIT_TEST(TrackFollowing_RemainingAltitudes)
{
  // Up 100 m, down 300 m, up 50 m, every 1000 m.
  ElevationInfo profile;
  profile.Assign({1000, 2000, 3000}, {500, 600, 300, 350});

  auto const all = GetRemainingAltitudes(profile, 0);
  TEST_EQUAL(all.GetTotalAscent(), 150, ());
  TEST_EQUAL(all.GetTotalDescent(), 300, ());

  // Halfway up the first climb: 50 m are left of it.
  auto const half = GetRemainingAltitudes(profile, 500);
  TEST_EQUAL(half.GetTotalAscent(), 100, ());
  TEST_EQUAL(half.GetTotalDescent(), 300, ());
  TEST_EQUAL(half.m_maxAltitude, 600, ());

  auto const last = GetRemainingAltitudes(profile, 2000);
  TEST_EQUAL(last.GetTotalAscent(), 50, ());
  TEST_EQUAL(last.GetTotalDescent(), 0, ());

  auto const end = GetRemainingAltitudes(profile, 3000);
  TEST_EQUAL(end.GetTotalAscent(), 0, ());
  TEST_EQUAL(end.GetTotalDescent(), 0, ());
}

UNIT_TEST(TrackFollowing_SectionAltitudes)
{
  ElevationInfo profile;
  profile.Assign({1000, 2000, 3000}, {500, 600, 300, 350});
  auto constexpr kThreshold = ElevationInfo::kDefThresholdMWM;

  // From 500 m (550 m high) to 2500 m (325 m high): up 50, down 300, up 25.
  auto const forward = GetSectionAltitudes(profile, 500, 2500, kThreshold);
  TEST_EQUAL(forward.GetTotalAscent(), 75, ());
  TEST_EQUAL(forward.GetTotalDescent(), 300, ());

  // The same section backwards.
  auto const backward = GetSectionAltitudes(profile, 2500, 500, kThreshold);
  TEST_EQUAL(backward.GetTotalAscent(), 300, ());
  TEST_EQUAL(backward.GetTotalDescent(), 75, ());

  // Clamped to the profile.
  auto const clamped = GetSectionAltitudes(profile, -100, 1e6, kThreshold);
  TEST_EQUAL(clamped.GetTotalAscent(), 150, ());
  TEST_EQUAL(clamped.GetTotalDescent(), 300, ());

  auto const empty = GetSectionAltitudes(profile, 700, 700, kThreshold);
  TEST_EQUAL(empty.GetTotalAscent(), 0, ());
}
}  // namespace track_following_tests
