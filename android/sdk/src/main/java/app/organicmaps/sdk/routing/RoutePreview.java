package app.organicmaps.sdk.routing;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.Router;

/**
 * Maps&amp;Fly: walking or cycling routes to a place, calculated next to the planned route, which stays untouched.
 * The routes are drawn on the map until {@link #cancel()}. Use from the main thread; listeners are called on it.
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

  public interface InsertVariantsListener
  {
    /**
     * @param current the planned route
     * @param optimal the route with the place inserted between the stops where it adds the least time, {@code null}
     *            when not found
     * @param via the planned route, which leaves for the place and joins again further on; {@code null} when not
     *            found or when it equals another variant
     * @param detour the planned route with a way to the place and back, {@code null} when not found
     */
    void onInsertVariants(@NonNull Result current, @Nullable Result optimal, @Nullable Result via,
                          @Nullable Result detour);
  }

  /** Kinds of {@link RoutingController#addDetourStop}, like RoutingManager::RouteInsertKind. */
  public static final int INSERT_OPTIMAL = 0;
  public static final int INSERT_VIA = 1;
  public static final int INSERT_SIDE_TRIP = 2;

  private RoutePreview() {}

  /** Replaces a running calculation. Only {@link Router#Pedestrian} and {@link Router#Bicycle} are supported. */
  public static void calculate(double fromLat, double fromLon, double toLat, double toLon, @NonNull Router router,
                               @NonNull Listener listener)
  {
    nativeCalculate(fromLat, fromLon, toLat, toLon, router.getType(), listener);
  }

  /**
   * While a walking or cycling route is planned, calculates both ways to add the place as a stop.
   * @return false without such a route; the listener is not called then
   */
  public static boolean calculateInsertVariants(double lat, double lon, @NonNull InsertVariantsListener listener)
  {
    return nativeCalculateInsertVariants(lat, lon, listener);
  }

  /** Stops the calculation and hides the drawn routes. */
  public static void cancel()
  {
    nativeCancel();
  }

  private static native void nativeCalculate(double fromLat, double fromLon, double toLat, double toLon, int routerType,
                                             @NonNull Listener listener);

  private static native boolean nativeCalculateInsertVariants(double lat, double lon,
                                                              @NonNull InsertVariantsListener listener);

  /** Adds a variant of the last {@link #calculateInsertVariants} as stops, see RoutingController. */
  static native boolean nativeAddDetour(@NonNull String title, @NonNull String subtitle, double lat, double lon,
                                        @NonNull String junctionTitle, int kind);

  private static native void nativeCancel();
}
