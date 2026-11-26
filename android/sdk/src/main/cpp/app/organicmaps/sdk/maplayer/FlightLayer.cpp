#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "map/flight_layer.hpp"

#include "geometry/mercator.hpp"

#include <cmath>
#include <vector>

// Maps&Fly: the flight preparation layer, see FlightLayer.java. All calls run on the UI thread.
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
  auto const elevation = frm()->GetTerrainElevation(mercator::FromLatLon(lat, lon));
  return elevation ? *elevation : NAN;
}

// void onCellsNeeded(int[] cells); void onSiteTapped(String id);
JNIEXPORT void Java_app_organicmaps_sdk_maplayer_flight_FlightLayer_nativeSetListener(JNIEnv *, jclass,
                                                                                     jobject listener)
{
  auto const ref = jni::make_global_ref(listener);
  frm()->GetFlightLayer().SetCellsRequestFn([ref](std::vector<int> const & cells)
  {
    JNIEnv * env = jni::GetEnv();
    jni::TScopedLocalIntArrayRef jCells(env, env->NewIntArray(static_cast<jsize>(cells.size())));
    env->SetIntArrayRegion(jCells.get(), 0, static_cast<jsize>(cells.size()), cells.data());
    jmethodID const method = jni::GetMethodID(env, *ref, "onCellsNeeded", "([I)V");
    env->CallVoidMethod(*ref, method, jCells.get());
  });
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
    JNIEnv * env, jclass, jint cell, jobjectArray ids, jobjectArray names, jdoubleArray lats, jdoubleArray lons,
    jbooleanArray landings, jintArray directions, jintArray areaOffsets, jdoubleArray areaLatLons)
{
  jsize const count = env->GetArrayLength(ids);
  std::vector<jdouble> latV(count), lonV(count);
  std::vector<jboolean> landingV(count);
  std::vector<jint> directionV(count), offsetV(count + 1);
  env->GetDoubleArrayRegion(lats, 0, count, latV.data());
  env->GetDoubleArrayRegion(lons, 0, count, lonV.data());
  env->GetBooleanArrayRegion(landings, 0, count, landingV.data());
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
    for (jint j = offsetV[i]; j < offsetV[i + 1]; ++j)
      site.m_area.push_back(mercator::FromLatLon(areaV[2 * j], areaV[2 * j + 1]));
  }
  frm()->GetFlightLayer().SetSites(static_cast<int>(cell), std::move(sites));
}
}  // extern "C"
