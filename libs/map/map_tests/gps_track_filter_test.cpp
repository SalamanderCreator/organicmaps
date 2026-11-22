#include "testing/testing.hpp"

#include "map/gps_track_filter.hpp"

#include "platform/settings.hpp"

#include <vector>

namespace gps_track_filter_test
{
using Points = std::vector<location::GpsInfo>;

location::GpsInfo Make(double timestamp, double lat, double lon, double speed)
{
  location::GpsInfo info;
  info.m_timestamp = timestamp;
  info.m_speed = speed;
  info.m_latitude = lat;
  info.m_longitude = lon;
  info.m_horizontalAccuracy = 5;
  info.m_verticalAccuracy = 1.0;
  info.m_source = location::EAndroidNative;
  return info;
}

// Degrees of longitude per meter at the equator.
double constexpr kDegPerMeter = 1.0 / 111320.0;

// Straight line to the east, 15 m every second at 15 m/s: every point passes the filter.
Points MakeWalk(size_t count)
{
  Points points;
  for (size_t i = 0; i < count; ++i)
    points.push_back(Make(100.0 + i, 0.0, 15.0 * i * kDegPerMeter, 15.0));
  return points;
}

std::string_view constexpr kMinAccuracyKey = "GpsTrackingMinAccuracy";

// Restores the accuracy setting of the test environment.
class MinAccuracyGuard
{
public:
  MinAccuracyGuard() : m_hadSetting(settings::Get(kMinAccuracyKey, m_original)) {}
  ~MinAccuracyGuard()
  {
    if (m_hadSetting)
      GpsTrackFilter::StoreMinHorizontalAccuracy(m_original);
    else
      settings::Delete(kMinAccuracyKey);
  }

private:
  double m_original = 0;
  bool m_hadSetting;
};

UNIT_TEST(GpsTrackFilter_DefaultMinAccuracy)
{
  MinAccuracyGuard guard;
  settings::Delete(kMinAccuracyKey);
  TEST_EQUAL(GpsTrackFilter::LoadMinHorizontalAccuracy(), 25.0, ());
}

UNIT_TEST(GpsTrackFilter_SkipsInaccurateFixes)
{
  MinAccuracyGuard guard;
  settings::Delete(kMinAccuracyKey);
  GpsTrackFilter filter;
  auto points = MakeWalk(6);
  // Also the first fix: it would be accepted without any other check.
  points[0].m_horizontalAccuracy = 60;
  points[3].m_horizontalAccuracy = 26;
  points[4].m_horizontalAccuracy = 25;
  Points out;
  filter.Process(points, out);
  TEST_EQUAL(out.size(), 4, ());
  TEST_EQUAL(out[0].m_timestamp, points[1].m_timestamp, ());
  TEST_EQUAL(out[1].m_timestamp, points[2].m_timestamp, ());
  TEST_EQUAL(out[2].m_timestamp, points[4].m_timestamp, ());
  TEST_EQUAL(out[3].m_timestamp, points[5].m_timestamp, ());
}

UNIT_TEST(GpsTrackFilter_InaccurateFixDoesNotBlockNextFix)
{
  MinAccuracyGuard guard;
  settings::Delete(kMinAccuracyKey);
  GpsTrackFilter filter;
  auto points = MakeWalk(4);
  // A far-off inaccurate fix would make the next good fix look like a jump if it entered the history.
  points[2].m_horizontalAccuracy = 80;
  points[2].m_latitude = 200 * kDegPerMeter;
  Points out;
  filter.Process(points, out);
  TEST_EQUAL(out.size(), 3, ());
  TEST_EQUAL(out.back().m_timestamp, points[3].m_timestamp, ());
}

UNIT_TEST(GpsTrackFilter_RejectsJump)
{
  MinAccuracyGuard guard;
  settings::Delete(kMinAccuracyKey);
  GpsTrackFilter filter;
  auto points = MakeWalk(4);
  // An accurate-looking fix 300 m away within a second.
  auto jump = Make(103.5, 300 * kDegPerMeter, 45.0 * kDegPerMeter, 15.0);
  points.insert(points.begin() + 3, jump);
  Points out;
  filter.Process(points, out);
  for (auto const & p : out)
    TEST_NOT_EQUAL(p.m_timestamp, jump.m_timestamp, ());
}

UNIT_TEST(GpsTrackFilter_ResetAppliesStoredAccuracy)
{
  MinAccuracyGuard guard;
  settings::Delete(kMinAccuracyKey);
  GpsTrackFilter filter;
  // 50 m apart, so that the 40 m accuracy circles do not overlap.
  Points points;
  for (size_t i = 0; i < 3; ++i)
  {
    points.push_back(Make(100.0 + i, 0.0, 50.0 * i * kDegPerMeter, 50.0));
    points.back().m_horizontalAccuracy = 40;
  }

  Points out;
  filter.Process(points, out);
  TEST(out.empty(), ());

  GpsTrackFilter::StoreMinHorizontalAccuracy(50);
  filter.Reset();
  filter.Process(points, out);
  TEST_EQUAL(out.size(), 3, ());
}

location::GpsInfo MakeAt(double timestamp, double meters, double accuracy)
{
  auto info = Make(timestamp, 0.0, meters * kDegPerMeter, 1.0);
  info.m_horizontalAccuracy = accuracy;
  return info;
}

std::vector<double> Times(Points const & points)
{
  std::vector<double> times;
  for (auto const & p : points)
    times.push_back(p.m_timestamp);
  return times;
}

UNIT_TEST(SelectAccurateTrackPoints_KeepsFixesFartherThanTheirAccuracy)
{
  // Most accurate first: 2 (3 m), 4 (4 m), 0 (5 m), 3 (6 m), 1 (8 m).
  Points const points = {MakeAt(0, 0, 5), MakeAt(1, 6, 8), MakeAt(2, 12, 3), MakeAt(3, 16, 6), MakeAt(4, 30, 4)};
  // 1 is 6 m from 0 and from 2, within its 8 m. 3 is 4 m from 2, within its 6 m.
  TEST_EQUAL(Times(SelectAccurateTrackPoints(points)), (std::vector<double>{0, 2, 4}), ());
}

UNIT_TEST(SelectAccurateTrackPoints_ChecksOnlyExistingNeighbours)
{
  // The first kept fix has no neighbours. The next one is checked against the only neighbour before it.
  Points const points = {MakeAt(0, 0, 5), MakeAt(1, 4, 2)};
  TEST_EQUAL(Times(SelectAccurateTrackPoints(points)), (std::vector<double>{1}), ());
  Points const farApart = {MakeAt(0, 0, 5), MakeAt(1, 10, 2)};
  TEST_EQUAL(Times(SelectAccurateTrackPoints(farApart)), (std::vector<double>{0, 1}), ());
}

UNIT_TEST(SelectAccurateTrackPoints_InaccurateFixFillsGap)
{
  // A 60 m fix more than 60 m from both kept neighbours fills the gap.
  Points const points = {MakeAt(0, 0, 5), MakeAt(10, 100, 60), MakeAt(20, 200, 5)};
  TEST_EQUAL(Times(SelectAccurateTrackPoints(points)), (std::vector<double>{0, 10, 20}), ());
}

UNIT_TEST(SelectAccurateTrackPoints_DropsJump)
{
  // Walking at 10 m/s with one accurate-looking fix 2 km off.
  Points points;
  for (int i = 0; i < 6; ++i)
    points.push_back(MakeAt(10 * i, 100 * i, 5));
  points[3] = MakeAt(30, 2300, 3);
  TEST_EQUAL(Times(SelectAccurateTrackPoints(points)), (std::vector<double>{0, 10, 20, 40, 50}), ());
}

UNIT_TEST(SelectAccurateTrackPoints_KeepsFastPlausibleMove)
{
  // 50 m/s, below the speed limit.
  Points const points = {MakeAt(0, 0, 5), MakeAt(10, 500, 5), MakeAt(20, 1000, 5)};
  TEST_EQUAL(Times(SelectAccurateTrackPoints(points)), (std::vector<double>{0, 10, 20}), ());
}

UNIT_TEST(SelectAccurateTrackPoints_InaccurateFixIsNoJump)
{
  // 1 km in 10 s is too fast, but not when the fix may be off by 900 m.
  Points const points = {MakeAt(0, 0, 5), MakeAt(10, 1000, 900), MakeAt(20, 200, 5)};
  TEST_EQUAL(Times(SelectAccurateTrackPoints(points)), (std::vector<double>{0, 20}), ());
}

UNIT_TEST(SelectAccurateTrackPoints_FirstFixIsJump)
{
  // The first fix is 5 km off, the following fixes agree with each other.
  Points points = {MakeAt(0, 5000, 5)};
  for (int i = 1; i <= 6; ++i)
    points.push_back(MakeAt(i * 10, i * 100, 5));
  TEST_EQUAL(Times(SelectAccurateTrackPoints(points)), (std::vector<double>{10, 20, 30, 40, 50, 60}), ());
}

UNIT_TEST(SelectAccurateTrackPoints_SameTimeKeepsMoreAccurateFix)
{
  Points const points = {MakeAt(0, 0, 5), MakeAt(5, 50, 9), MakeAt(5, 80, 3)};
  auto const result = SelectAccurateTrackPoints(points);
  TEST_EQUAL(result.size(), 2, ());
  TEST_EQUAL(result[1].m_horizontalAccuracy, 3, ());
}

UNIT_TEST(GpsTrackFilter_RepeatedFixKeepsNextFix)
{
  Points const walk = MakeWalk(4);
  GpsTrackFilter filter;
  Points out;
  filter.Process({walk[0], walk[1]}, out);
  // The same fix again, e.g. the cached location replayed to a new location observer.
  filter.Process({walk[1]}, out);
  filter.Process({walk[2], walk[3]}, out);
  TEST_EQUAL(out.size(), 4, ());
  TEST_EQUAL(out[2].m_timestamp, walk[2].m_timestamp, ());
}

UNIT_TEST(GpsTrackFilter_SameTimeDistinctFixKeepsDirection)
{
  auto const fix = [](double timestamp, double east, double north)
  { return Make(timestamp, north * kDegPerMeter, east * kDegPerMeter, 15.0); };
  GpsTrackFilter filter;
  Points out;
  // The fix at 30 m has the same time as the one before it, but it is not a replay: it must stay in the
  // history, or the turn at 54 m looks like a jump from 15 m.
  filter.Process({fix(1000, 0, 0), fix(1001, 15, 0), fix(1001, 30, 0), fix(1011, 54, 8)}, out);
  TEST_EQUAL(out.size(), 3, ());
  TEST_EQUAL(out[2].m_timestamp, 1011, ());
}

UNIT_TEST(GpsTrackFilter_ClockRollbackAfterRejectedFix)
{
  auto const fix = [](double timestamp, double meters) { return Make(timestamp, 0.0, meters * kDegPerMeter, 15.0); };
  GpsTrackFilter filter;
  Points out;
  // The fix at 20 m is rejected as too close. Then the clock goes back: the fix at 1050 is older than
  // it and is dropped, but the fixes after it must be recorded.
  filter.Process({fix(1000, 0), fix(1001, 15), fix(1100, 20), fix(1050, 35), fix(1051, 50), fix(1052, 65)}, out);
  filter.Finalize(out);
  TEST_EQUAL(out.size(), 4, ());
  TEST_EQUAL(out[2].m_timestamp, 1051, ());
  TEST_EQUAL(out[3].m_timestamp, 1052, ());
}

UNIT_TEST(GpsTrackFilter_FinalizeAppendsLastFix)
{
  GpsTrackFilter filter;
  Points out;
  auto points = MakeWalk(5);
  points.front().m_verticalAccuracy = -1;
  filter.Process(points, out);
  TEST_EQUAL(out.size(), 5, ());
  TEST(!out.front().HasAltitude(), ());

  // Finalization keeps the last eligible fix even when sampling deferred it.
  auto const last = Make(160.0, 0.0, (15.0 * 4 + 5.0) * kDegPerMeter, 0.0);
  auto missingAltitude = Make(161.0, 0.0, (15.0 * 4 + 8.0) * kDegPerMeter, 0.0);
  missingAltitude.m_verticalAccuracy = -1.0;
  out.clear();
  filter.Process({last, missingAltitude}, out);
  TEST(out.empty(), ());
  filter.Finalize(out);
  TEST_EQUAL(out.size(), 1, ());
  TEST_EQUAL(out[0].m_timestamp, missingAltitude.m_timestamp, ());
  TEST_EQUAL(out[0].m_longitude, missingAltitude.m_longitude, ());
  TEST(!out[0].HasAltitude(), ());
}

UNIT_TEST(GpsTrackFilter_ZeroAccuracyBypassesSampling)
{
  MinAccuracyGuard guard;
  GpsTrackFilter::StoreMinHorizontalAccuracy(0);
  GpsTrackFilter filter;
  auto points = MakeWalk(2);
  points.front().m_speed = -1;
  points.front().m_verticalAccuracy = -1;
  Points out{Make(99, 0, 0, 0)};
  filter.Process(points, out);
  filter.Finalize(out);
  TEST_EQUAL(out.size(), 3, ());
  TEST_EQUAL(out[0].m_timestamp, 99, ());
  TEST_EQUAL(out[1].m_timestamp, points.front().m_timestamp, ());
  TEST(!out[1].HasAltitude(), ());
  TEST_EQUAL(out[2].m_timestamp, points.back().m_timestamp, ());
}

}  // namespace gps_track_filter_test
