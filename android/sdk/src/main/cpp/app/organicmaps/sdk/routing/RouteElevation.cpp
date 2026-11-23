#include "app/organicmaps/sdk/Framework.hpp"

#include "map/elevation_info.hpp"
#include "map/track_following.hpp"

// Maps&Fly: what is left to climb and descend on the followed route.
extern "C"
{
JNIEXPORT jdoubleArray Java_app_organicmaps_sdk_routing_RouteElevation_nativeGetRemaining(JNIEnv * env, jclass)
{
  auto const & manager = frm()->GetRoutingManager();
  auto const * route = manager.RoutingSession().GetRoute();
  ElevationInfo profile;
  if (route == nullptr || !route->IsValid() || !manager.GetRouteElevationInfo(profile))
    return nullptr;

  double const fromStartM = route->GetCurrentDistanceFromBeginMeters();
  auto const info = track_following::GetRemainingAltitudes(profile, fromStartM);
  jdouble const values[] = {fromStartM, static_cast<jdouble>(info.GetTotalAscent()),
                            static_cast<jdouble>(info.GetTotalDescent()), route->GetTotalDistanceMeters()};
  jdoubleArray result = env->NewDoubleArray(4);
  env->SetDoubleArrayRegion(result, 0, 4, values);
  return result;
}
}  // extern "C"
