#include "testing/testing.hpp"

#include "map/off_track_monitor.hpp"

#include "geometry/mercator.hpp"

#include <vector>

namespace off_track_monitor_tests
{
using State = OffTrackMonitor::State;

// Degrees per meter near the equator, a little less than the real value, so offsets are not shorter than meant.
double constexpr kDegPerMeter = 1.0 / 111000.0;

// A 1 km track along the equator to the east.
std::vector<OffTrackMonitor::Line> MakeTrack()
{
  return {{mercator::FromLatLon(0, 0), mercator::FromLatLon(0, 1000 * kDegPerMeter)}};
}

// A fix @a eastMeters along the track and @a northMeters beside it.
location::GpsInfo MakeFix(double timestamp, double eastMeters, double northMeters, double accuracy = 5)
{
  location::GpsInfo info;
  info.m_source = location::EAndroidNative;
  info.m_timestamp = timestamp;
  info.m_latitude = northMeters * kDegPerMeter;
  info.m_longitude = eastMeters * kDegPerMeter;
  info.m_horizontalAccuracy = accuracy;
  return info;
}

UNIT_TEST(OffTrackMonitor_OnTrack)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  TEST_EQUAL(monitor.GetDistanceMeters(), -1, ());
  TEST_EQUAL(monitor.GetState(), State::NotReached, ());
  for (int t = 0; t < 30; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 10 * t, 0)), State::OnTrack, ());
  TEST_LESS(monitor.GetDistanceMeters(), 1, ());
}

UNIT_TEST(OffTrackMonitor_ClosestPoint)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  monitor.Update(MakeFix(0, 100, 60));
  TEST(monitor.GetClosestPoint().EqualDxDy(mercator::FromLatLon(0, 100 * kDegPerMeter), 1e-9), ());
  // Before the track start: its first point.
  monitor.Update(MakeFix(1, -60, 10));
  TEST(monitor.GetClosestPoint().EqualDxDy(mercator::FromLatLon(0, 0), 1e-9), ());
}

UNIT_TEST(OffTrackMonitor_AlarmAfterDelayAndBack)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  TEST_EQUAL(monitor.Update(MakeFix(0, 100, 0)), State::OnTrack, ());

  // 60 m beside the track is more than 30 m + 5 m accuracy.
  for (int t = 1; t < 11; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, 60)), State::OnTrack, (t));
  TEST_EQUAL(monitor.Update(MakeFix(11, 100, 60)), State::OffTrack, ());
  TEST_ALMOST_EQUAL_ABS(monitor.GetDistanceMeters(), 60.0, 2.0, ());

  // Distance is measured to the closest point of the segment, also before its start.
  TEST_EQUAL(monitor.Update(MakeFix(12, -60, 0)), State::OffTrack, ());
  TEST_ALMOST_EQUAL_ABS(monitor.GetDistanceMeters(), 60.0, 2.0, ());

  for (int t = 13; t < 16; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 120, 10)), State::OffTrack, (t));
  TEST_EQUAL(monitor.Update(MakeFix(16, 120, 10)), State::OnTrack, ());
}

UNIT_TEST(OffTrackMonitor_ShortExcursionIsNoAlarm)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  TEST_EQUAL(monitor.Update(MakeFix(-1, 100, 0)), State::OnTrack, ());
  for (int t = 0; t < 30; ++t)
  {
    // Every 9 s out of 10 off the track: never long enough in a row.
    bool const back = t % 10 == 9;
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, back ? 0 : 60)), State::OnTrack, (t));
  }
}

UNIT_TEST(OffTrackMonitor_AccuracyIsAdded)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  TEST_EQUAL(monitor.Update(MakeFix(-1, 100, 0)), State::OnTrack, ());
  // 45 m away, but 20 m accuracy: the position may be 25 m away, within the threshold.
  for (int t = 0; t < 30; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, 45, 20)), State::OnTrack, (t));

  // The same position with 5 m accuracy is off the track.
  for (int t = 30; t < 45; ++t)
    monitor.Update(MakeFix(t, 100, 45, 5));
  TEST_EQUAL(monitor.GetState(), State::OffTrack, ());
}

UNIT_TEST(OffTrackMonitor_GpsInaccurate)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  TEST_EQUAL(monitor.Update(MakeFix(-1, 100, 0)), State::OnTrack, ());
  // Far away, but with an accuracy worse than kMaxAccuracyMeters: a hint, no alarm.
  for (int t = 0; t < 10; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, 300, 80)), State::OnTrack, (t));
  TEST_EQUAL(monitor.Update(MakeFix(10, 100, 300, 80)), State::GpsInaccurate, ());
  for (int t = 11; t < 30; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, 300, 80)), State::GpsInaccurate, (t));

  // Accurate again: the off track delay starts anew.
  for (int t = 30; t < 40; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, 300)), State::GpsInaccurate, (t));
  TEST_EQUAL(monitor.Update(MakeFix(40, 100, 300)), State::OffTrack, ());
}

UNIT_TEST(OffTrackMonitor_NotReachedYet)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  // Starting 1 km away: no alarm, however long it takes to get there.
  for (int t = 0; t < 60; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, -1000 + 15 * t, 0)), State::NotReached, (t));
  TEST_ALMOST_EQUAL_ABS(monitor.GetDistanceMeters(), 115.0, 2.0, ());

  // Reaching the track counts at once, leaving it afterwards is an alarm.
  TEST_EQUAL(monitor.Update(MakeFix(60, 10, 0)), State::OnTrack, ());
  for (int t = 61; t < 71; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, 60)), State::OnTrack, (t));
  TEST_EQUAL(monitor.Update(MakeFix(71, 100, 60)), State::OffTrack, ());
}

UNIT_TEST(OffTrackMonitor_NotReachedWithInaccurateGps)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  for (int t = 0; t < 10; ++t)
    TEST_EQUAL(monitor.Update(MakeFix(t, 100, 300, 80)), State::NotReached, (t));
  TEST_EQUAL(monitor.Update(MakeFix(10, 100, 300, 80)), State::GpsInaccurate, ());
  // Accurate again and still not on the track: back to the info at once, no alarm.
  TEST_EQUAL(monitor.Update(MakeFix(11, 100, 300)), State::NotReached, ());
}

UNIT_TEST(OffTrackMonitor_ClosestOfSeveralLines)
{
  auto lines = MakeTrack();
  lines.push_back({mercator::FromLatLon(200 * kDegPerMeter, 0), mercator::FromLatLon(200 * kDegPerMeter, 0.01)});
  lines.push_back({mercator::FromLatLon(500 * kDegPerMeter, 0)});
  OffTrackMonitor monitor(std::move(lines), 30);

  monitor.Update(MakeFix(0, 100, 190));
  TEST_ALMOST_EQUAL_ABS(monitor.GetDistanceMeters(), 10.0, 1.0, ());
  monitor.Update(MakeFix(1, 0, 480));
  TEST_ALMOST_EQUAL_ABS(monitor.GetDistanceMeters(), 20.0, 1.0, ());
}

UNIT_TEST(OffTrackMonitor_SetThreshold)
{
  OffTrackMonitor monitor(MakeTrack(), 30);
  TEST_EQUAL(monitor.Update(MakeFix(-1, 100, 0)), State::OnTrack, ());
  for (int t = 0; t < 15; ++t)
    monitor.Update(MakeFix(t, 100, 60));
  TEST_EQUAL(monitor.GetState(), State::OffTrack, ());

  monitor.SetThreshold(100);
  for (int t = 15; t < 20; ++t)
    monitor.Update(MakeFix(t, 100, 60));
  TEST_EQUAL(monitor.GetState(), State::OnTrack, ());
}
}  // namespace off_track_monitor_tests
