#include "Framework.hpp"

#include "map/off_track_monitor.hpp"
#include "map/track.hpp"

#include "geometry/mercator.hpp"

#include <memory>

// Maps&Fly: the off track alarm. All calls come from the UI thread, like the location updates.
namespace
{
std::unique_ptr<OffTrackMonitor> g_monitor;
kml::TrackId g_trackId = kml::kInvalidTrackId;
}  // namespace

extern "C"
{
JNIEXPORT jboolean Java_app_organicmaps_sdk_location_OffTrackMonitor_nativeStart(JNIEnv *, jclass, jlong trackId,
                                                                                 jdouble thresholdMeters)
{
  auto const * track = frm()->GetBookmarkManager().GetTrack(static_cast<kml::TrackId>(trackId));
  if (track == nullptr)
    return false;

  // A copy: the track may be edited or deleted while it is watched.
  std::vector<OffTrackMonitor::Line> lines;
  for (auto const & geometry : track->GetData().m_geometry.m_lines)
  {
    auto & line = lines.emplace_back();
    line.reserve(geometry.size());
    for (auto const & point : geometry)
      line.push_back(point.GetPoint());
  }
  if (lines.empty())
    return false;

  g_monitor = std::make_unique<OffTrackMonitor>(std::move(lines), thresholdMeters);
  g_trackId = static_cast<kml::TrackId>(trackId);
  return true;
}

JNIEXPORT void Java_app_organicmaps_sdk_location_OffTrackMonitor_nativeStop(JNIEnv *, jclass)
{
  g_monitor.reset();
  g_trackId = kml::kInvalidTrackId;
  frm()->GetRoutingManager().HideTrackApproachLine();
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_location_OffTrackMonitor_nativeIsActive(JNIEnv *, jclass)
{
  return g_monitor != nullptr;
}

JNIEXPORT jlong Java_app_organicmaps_sdk_location_OffTrackMonitor_nativeGetTrackId(JNIEnv *, jclass)
{
  return static_cast<jlong>(g_trackId);
}

JNIEXPORT void Java_app_organicmaps_sdk_location_OffTrackMonitor_nativeSetThreshold(JNIEnv *, jclass,
                                                                                    jdouble thresholdMeters)
{
  if (g_monitor)
    g_monitor->SetThreshold(thresholdMeters);
}

JNIEXPORT jint Java_app_organicmaps_sdk_location_OffTrackMonitor_nativeUpdate(JNIEnv *, jclass, jdouble lat,
                                                                              jdouble lon, jdouble accuracy,
                                                                              jdouble timestampSeconds)
{
  ASSERT(g_monitor, ());
  location::GpsInfo info;
  info.m_source = location::EAndroidNative;
  info.m_latitude = lat;
  info.m_longitude = lon;
  info.m_horizontalAccuracy = accuracy;
  info.m_timestamp = timestampSeconds;
  auto const state = g_monitor->Update(info);
  // A dotted line shows the way to the track while away from it.
  auto & routingManager = frm()->GetRoutingManager();
  if (state == OffTrackMonitor::State::OffTrack || state == OffTrackMonitor::State::NotReached)
    routingManager.ShowTrackApproachLine(mercator::FromLatLon(lat, lon), g_monitor->GetClosestPoint());
  else
    routingManager.HideTrackApproachLine();
  return static_cast<jint>(state);
}

JNIEXPORT jdouble Java_app_organicmaps_sdk_location_OffTrackMonitor_nativeGetDistance(JNIEnv *, jclass)
{
  return g_monitor ? g_monitor->GetDistanceMeters() : -1;
}
}  // extern "C"
