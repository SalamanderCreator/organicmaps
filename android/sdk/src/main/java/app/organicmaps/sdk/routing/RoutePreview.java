package app.organicmaps.sdk.routing;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.Router;

/**
 * Maps&amp;Fly: a walking or cycling route to a place, calculated next to the current route, which stays untouched.
 * Use from the main thread; the listener is called on it.
 */
public final class RoutePreview
{
  /**
   * @param timeSec hiking time after DIN 33466 for walking, the router's estimate for cycling
   * @param hasAltitudes false when the maps have no altitudes for the route; ascent and descent are 0 then
   */
  public record Result(double distanceM, double timeSec, int ascentM, int descentM, boolean hasAltitudes)
  {
  }

  public interface Listener
  {
    /** @param result {@code null} when no route was found */
    void onRoutePreview(@Nullable Result result);
  }

  private RoutePreview() {}

  /** Replaces a running calculation. Only {@link Router#Pedestrian} and {@link Router#Bicycle} are supported. */
  public static void calculate(double fromLat, double fromLon, double toLat, double toLon, @NonNull Router router,
                               @NonNull Listener listener)
  {
    nativeCalculate(fromLat, fromLon, toLat, toLon, router.getType(), new NativeListener(listener));
  }

  public static void cancel()
  {
    nativeCancel();
  }

  private record NativeListener(@NonNull Listener listener)
  {
    // Called from JNI.
    @SuppressWarnings("unused")
    void onResult(boolean found, double distanceM, double timeSec, int ascentM, int descentM, boolean hasAltitudes)
    {
      listener.onRoutePreview(found ? new Result(distanceM, timeSec, ascentM, descentM, hasAltitudes) : null);
    }
  }

  private static native void nativeCalculate(double fromLat, double fromLon, double toLat, double toLon, int routerType,
                                             @NonNull NativeListener listener);

  private static native void nativeCancel();
}
