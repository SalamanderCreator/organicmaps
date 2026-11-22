#include "app/organicmaps/sdk/Framework.hpp"

#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "map/track.hpp"
#include "map/track_following.hpp"

#include "geometry/mercator.hpp"

#include <cmath>
#include <optional>

// Maps&Fly: follow a saved track like a route. All calls come from the UI thread.
namespace
{
std::optional<track_following::JoinedTrack> GetJoinedTrack(jlong trackId)
{
  auto const * track = frm()->GetBookmarkManager().GetTrack(static_cast<kml::TrackId>(trackId));
  if (track == nullptr)
    return {};
  track_following::JoinedTrack joined(track->GetData().m_geometry);
  if (joined.m_points.empty())
    return {};
  return joined;
}

jdoubleArray ToJava(JNIEnv * env, track_following::TrackPosition const & pos, double distanceM)
{
  auto const latLon = mercator::ToLatLon(pos.m_point.GetPoint());
  jdouble const values[] = {latLon.m_lat, latLon.m_lon, distanceM, pos.m_distFromBegM};
  jdoubleArray result = env->NewDoubleArray(4);
  env->SetDoubleArrayRegion(result, 0, 4, values);
  return result;
}
}  // namespace

extern "C"
{
JNIEXPORT jdoubleArray Java_app_organicmaps_sdk_routing_TrackFollowing_nativeGetClosestPoint(JNIEnv * env, jclass,
                                                                                             jlong trackId, jdouble lat,
                                                                                             jdouble lon)
{
  auto const track = GetJoinedTrack(trackId);
  if (!track)
    return nullptr;
  auto const point = mercator::FromLatLon(lat, lon);
  auto const pos = track_following::FindClosest(*track, point);
  return ToJava(env, pos, mercator::DistanceOnEarth(point, pos.m_point.GetPoint()));
}

JNIEXPORT jdoubleArray Java_app_organicmaps_sdk_routing_TrackFollowing_nativeGetPointAt(JNIEnv * env, jclass,
                                                                                        jlong trackId,
                                                                                        jdouble distFromBegM)
{
  auto const track = GetJoinedTrack(trackId);
  if (!track)
    return nullptr;
  return ToJava(env, track_following::AtDistance(*track, distFromBegM), 0);
}

JNIEXPORT jdoubleArray Java_app_organicmaps_sdk_routing_TrackFollowing_nativeGetSelectedPoint(JNIEnv * env, jclass,
                                                                                              jlong trackId)
{
  auto const track = GetJoinedTrack(trackId);
  if (!track)
    return nullptr;
  auto const info = frm()->GetBookmarkManager().GetTrackSelectionInfo(static_cast<kml::TrackId>(trackId));
  if (!info.IsValid())
    return nullptr;
  return ToJava(env, track_following::AtDistance(*track, info.m_distFromBegM), 0);
}

JNIEXPORT jdoubleArray Java_app_organicmaps_sdk_routing_TrackFollowing_nativeStart(JNIEnv * env, jclass, jlong trackId,
                                                                                   jdouble distFromBegM)
{
  auto const track = GetJoinedTrack(trackId);
  if (!track)
    return nullptr;
  auto remainder = track_following::MakeRemainder(*track, track_following::AtDistance(*track, distFromBegM));
  if (!remainder)
    return nullptr;
  auto const end = remainder->m_points.back();
  frm()->GetRoutingManager().SetFollowedTrack(std::move(remainder));
  return ToJava(env, {track->m_points.size() - 1, end, track->m_distFromBegM.back()}, 0);
}

JNIEXPORT jdoubleArray Java_app_organicmaps_sdk_routing_TrackFollowing_nativeGetSection(JNIEnv * env, jclass,
                                                                                        jlong trackId,
                                                                                        jdouble fromDistM,
                                                                                        jdouble toDistM)
{
  auto const * track = frm()->GetBookmarkManager().GetTrack(static_cast<kml::TrackId>(trackId));
  if (track == nullptr)
    return nullptr;
  auto const * profile = track->GetElevationInfo();
  if (profile == nullptr)
    return nullptr;
  // The same threshold as the track totals.
  auto const threshold =
      track->GetId() == kml::kTempRelationTrackId ? ElevationInfo::kDefThresholdMWM : ElevationInfo::kDefThresholdGPS;
  auto const info = track_following::GetSectionAltitudes(*profile, fromDistM, toDistM, threshold);
  jdouble const values[] = {std::abs(toDistM - fromDistM), static_cast<jdouble>(info.GetTotalAscent()),
                            static_cast<jdouble>(info.GetTotalDescent())};
  jdoubleArray result = env->NewDoubleArray(3);
  env->SetDoubleArrayRegion(result, 0, 3, values);
  return result;
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_routing_TrackFollowing_nativeIsActive(JNIEnv *, jclass)
{
  return frm()->GetRoutingManager().IsFollowingTrack();
}
}  // extern "C"
