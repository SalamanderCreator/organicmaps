#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "geometry/mercator.hpp"

// Maps&Fly: routes to a tapped place, see RoutePreview.java.
namespace
{
jobject ToJavaResult(JNIEnv * env, std::optional<route_preview::Stats> const & stats)
{
  if (!stats)
    return nullptr;
  static jclass const clazz = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/routing/RoutePreview$Result");
  static jmethodID const ctor = jni::GetConstructorID(env, clazz, "(DDIIZ)V");
  return env->NewObject(clazz, ctor, stats->m_distanceM, stats->m_timeSec, static_cast<jint>(stats->m_ascentM),
                        static_cast<jint>(stats->m_descentM), static_cast<jboolean>(stats->m_hasAltitudes));
}
}  // namespace

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
    jni::TScopedLocalRef result(env, ToJavaResult(env, stats));
    jmethodID const methodId =
        jni::GetMethodID(env, *listener, "onRoutePreview", "(Lapp/organicmaps/sdk/routing/RoutePreview$Result;)V");
    env->CallVoidMethod(*listener, methodId, result.get());
  });
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_routing_RoutePreview_nativeCalculateInsertVariants(JNIEnv * env, jclass,
                                                                                               jdouble lat, jdouble lon,
                                                                                               jobject listener)
{
  return frm()->GetRoutingManager().CalculateRouteInsertVariants(
      mercator::FromLatLon(lat, lon),
      [listener = jni::make_global_ref(listener)](RoutingManager::RouteInsertVariants const & variants)
  {
    JNIEnv * env = jni::GetEnv();
    jni::TScopedLocalRef current(env, ToJavaResult(env, variants.m_current));
    jni::TScopedLocalRef optimal(env, ToJavaResult(env, variants.m_optimal));
    jni::TScopedLocalRef via(env, ToJavaResult(env, variants.m_via));
    jni::TScopedLocalRef detour(env, ToJavaResult(env, variants.m_detour));
    char const * const sig =
        "(Lapp/organicmaps/sdk/routing/RoutePreview$Result;Lapp/organicmaps/sdk/routing/RoutePreview$Result;"
        "Lapp/organicmaps/sdk/routing/RoutePreview$Result;Lapp/organicmaps/sdk/routing/RoutePreview$Result;)V";
    jmethodID const methodId = jni::GetMethodID(env, *listener, "onInsertVariants", sig);
    env->CallVoidMethod(*listener, methodId, current.get(), optimal.get(), via.get(), detour.get());
  });
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_routing_RoutePreview_nativeAddDetour(JNIEnv * env, jclass, jstring title,
                                                                                 jstring subtitle, jdouble lat,
                                                                                 jdouble lon, jstring junctionTitle,
                                                                                 jboolean sideTrip)
{
  RouteMarkData data;
  data.m_title = jni::ToNativeString(env, title);
  data.m_subTitle = jni::ToNativeString(env, subtitle);
  data.m_position = mercator::FromLatLon(lat, lon);
  return frm()->GetRoutingManager().AddRouteDetour(std::move(data), jni::ToNativeString(env, junctionTitle),
                                                   static_cast<bool>(sideTrip));
}

JNIEXPORT void Java_app_organicmaps_sdk_routing_RoutePreview_nativeCancel(JNIEnv *, jclass)
{
  frm()->GetRoutingManager().CancelRoutePreview();
}
}  // extern "C"
