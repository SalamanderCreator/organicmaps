package app.organicmaps.sdk.routing;

import androidx.annotation.Nullable;

/** Maps&amp;Fly: the elevation of the followed route from the current position on. Use from the main thread. */
public final class RouteElevation
{
  /**
   * @param fromStartM distance of the current position from the route start
   * @param totalM route length
   */
  public record Remaining(double fromStartM, int ascentM, int descentM, double totalM)
  {
  }

  private RouteElevation() {}

  /** Returns {@code null} without a route or without altitudes on it. */
  @Nullable
  public static Remaining getRemaining()
  {
    final double[] values = nativeGetRemaining();
    if (values == null)
      return null;
    return new Remaining(values[0], (int) values[1], (int) values[2], values[3]);
  }

  @Nullable
  private static native double[] nativeGetRemaining();
}
