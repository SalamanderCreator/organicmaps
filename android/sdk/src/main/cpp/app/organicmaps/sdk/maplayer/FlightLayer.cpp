#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "map/flight_layer.hpp"

#include "geometry/mercator.hpp"

#include <string>
#include <vector>

// Maps&Fly: the flight sites and the flight preparation layer, see FlightLayer.java. All calls run on the
// UI thread.
extern "C"
{
JNIEXPORT void Java_app_organicmaps_sdk_maplayer_flight_FlightLayer_nativeSetEnabled(JNIEnv *, jclass,
                                                                                    jboolean enabled)
{
  frm()->SetFlightLayerEnabled(static_cast<bool>(enabled));
}

JNIEXPORT jboolean Java_app_organicmaps_sdk_maplayer_flight_FlightLayer_nativeIsEnabled(JNIEnv *, jclass)
{
  return static_cast<jboolean>(frm()->IsFlightLayerEnabled());
}

JNIEXPORT jdouble Java_app_organicmaps_sdk_maplayer_flight_FlightLayer_nativeGetTerrainElevation(JNIEnv *, jclass,
                                                                                               jdouble lat,
                                                                                               jdouble lon)
{
  // No NaN: the app is built with fast math, which assumes there is none.
  auto const elevation = frm()->GetTerrainElevation(mercator::FromLatLon(lat, lon));
  return elevation ? *elevation : -1e6;
}

// "kind\tname\tdistance\tlat\tlon" of the nearest aerial lift station ("cablecar") and parking ("parking").
JNIEXPORT jobjectArray Java_app_organicmaps_sdk_maplayer_flight_FlightLayer_nativeFindServices(JNIEnv * env, jclass,
                                                                                             jdouble lat,
                                                                                             jdouble lon)
{
  std::vector<std::string> items;
  for (auto const & s : frm()->FindFlightServices(mercator::FromLatLon(lat, lon)))
  {
    auto const ll = mercator::ToLatLon(s.m_point);
    items.push_back(std::string(s.m_kind == Framework::FlightService::Kind::CableCarStation ? "cablecar" : "parking") +
                    "\t" + s.m_name + "\t" + std::to_string(s.m_distanceM) + "\t" + std::to_string(ll.m_lat) + "\t" +
                    std::to_string(ll.m_lon));
  }
  return jni::ToJavaStringArray(env, items);
}

// void onSiteTapped(String id);
JNIEXPORT void Java_app_organicmaps_sdk_maplayer_flight_FlightLayer_nativeSetListener(JNIEnv *, jclass,
                                                                                     jobject listener)
{
  auto const ref = jni::make_global_ref(listener);
  frm()->SetFlightSiteTapFn([ref](std::string const & id)
  {
    JNIEnv * env = jni::GetEnv();
    jni::TScopedLocalRef jId(env, jni::ToJavaString(env, id));
    jmethodID const method = jni::GetMethodID(env, *ref, "onSiteTapped", "(Ljava/lang/String;)V");
    env->CallVoidMethod(*ref, method, jId.get());
  });
}

// |areaOffsets| has one more entry than sites: the area of site i are the lat/lon pairs
// [areaOffsets[i], areaOffsets[i + 1]) of |areaLatLons|.
JNIEXPORT void Java_app_organicmaps_sdk_maplayer_flight_FlightLayer_nativeSetSites(
    JNIEnv * env, jclass, jobjectArray ids, jobjectArray names, jdoubleArray lats, jdoubleArray lons,
    jbooleanArray landings, jintArray directions, jbooleanArray restricted, jbooleanArray labeled,
    jintArray areaOffsets, jdoubleArray areaLatLons)
{
  jsize const count = env->GetArrayLength(ids);
  std::vector<jdouble> latV(count), lonV(count);
  std::vector<jboolean> landingV(count), restrictedV(count), labeledV(count);
  std::vector<jint> directionV(count), offsetV(count + 1);
  env->GetDoubleArrayRegion(lats, 0, count, latV.data());
  env->GetDoubleArrayRegion(lons, 0, count, lonV.data());
  env->GetBooleanArrayRegion(landings, 0, count, landingV.data());
  env->GetBooleanArrayRegion(restricted, 0, count, restrictedV.data());
  env->GetBooleanArrayRegion(labeled, 0, count, labeledV.data());
  env->GetIntArrayRegion(directions, 0, count, directionV.data());
  env->GetIntArrayRegion(areaOffsets, 0, count + 1, offsetV.data());
  std::vector<jdouble> areaV(env->GetArrayLength(areaLatLons));
  env->GetDoubleArrayRegion(areaLatLons, 0, static_cast<jsize>(areaV.size()), areaV.data());

  std::vector<FlightLayer::Site> sites(count);
  for (jsize i = 0; i < count; ++i)
  {
    auto & site = sites[i];
    jni::TScopedLocalRef jId(env, env->GetObjectArrayElement(ids, i));
    jni::TScopedLocalRef jName(env, env->GetObjectArrayElement(names, i));
    site.m_id = jni::ToNativeString(env, static_cast<jstring>(jId.get()));
    site.m_name = jni::ToNativeString(env, static_cast<jstring>(jName.get()));
    site.m_point = mercator::FromLatLon(latV[i], lonV[i]);
    site.m_type = landingV[i] ? FlightLayer::Site::Type::Landing : FlightLayer::Site::Type::Takeoff;
    site.m_directions = static_cast<uint16_t>(directionV[i]);
    site.m_restricted = restrictedV[i];
    site.m_labeled = labeledV[i];
    for (jint j = offsetV[i]; j < offsetV[i + 1]; ++j)
      site.m_area.push_back(mercator::FromLatLon(areaV[2 * j], areaV[2 * j + 1]));
  }
  frm()->GetFlightLayer().SetSites(std::move(sites));
}
}  // extern "C"
