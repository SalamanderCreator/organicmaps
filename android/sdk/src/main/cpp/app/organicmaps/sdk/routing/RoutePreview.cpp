#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "geometry/mercator.hpp"

// Maps&Fly: route preview to a tapped place.
extern "C"
{
JNIEXPORT void Java_app_organicmaps_sdk_routing_RoutePreview_nativeCalculate(JNIEnv * env, jclass, jdouble fromLat,
                                                                             jdouble fromLon, jdouble toLat,
                                                                             jdouble toLon, jint routerType,
                                                                             jobject listener)
{
  frm()->GetRoutingManager().CalculateRoutePreview(
      mercator::FromLatLon(fromLat, fromLon), mercator::FromLatLon(toLat, toLon),
      static_cast<routing::RouterType>(routerType),
      [listener = jni::make_global_ref(listener)](std::optional<route_preview::Stats> const & stats)
  {
    JNIEnv * env = jni::GetEnv();
    jmethodID const methodId = jni::GetMethodID(env, *listener, "onResult", "(ZDDIIZ)V");
    auto const s = stats.value_or(route_preview::Stats{});
    env->CallVoidMethod(*listener, methodId, static_cast<jboolean>(stats.has_value()), s.m_distanceM, s.m_timeSec,
                        static_cast<jint>(s.m_ascentM), static_cast<jint>(s.m_descentM),
                        static_cast<jboolean>(s.m_hasAltitudes));
  });
}

JNIEXPORT void Java_app_organicmaps_sdk_routing_RoutePreview_nativeCancel(JNIEnv *, jclass)
{
  frm()->GetRoutingManager().CancelRoutePreview();
}
}  // extern "C"
