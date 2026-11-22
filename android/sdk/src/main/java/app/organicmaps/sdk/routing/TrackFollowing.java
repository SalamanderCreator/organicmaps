package app.organicmaps.sdk.routing;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.Router;

/**
 * Maps&amp;Fly: follows a saved track like a route. Distances along the track skip the gaps between its lines, like
 * the elevation profile. Use from the main thread.
 */
public final class TrackFollowing
{
  /** A point on a track: WGS84 degrees, distance in meters from a given position and from the track start. */
  public record TrackPoint(double lat, double lon, double distanceM, double distFromBegM)
  {
    @Nullable
    static TrackPoint of(@Nullable double[] values)
    {
      return values == null ? null : new TrackPoint(values[0], values[1], values[2], values[3]);
    }
  }

  /** The track to follow after arriving at its entry point with normal navigation. */
  public record Pending(long trackId, @NonNull String title, double distFromBegM, @NonNull Router router)
  {
  }

  @Nullable
  private static Pending sPending;

  private TrackFollowing() {}

  /** Returns the track point closest to the position, or {@code null} if the track is gone. */
  @Nullable
  public static TrackPoint getClosestPoint(long trackId, double lat, double lon)
  {
    return TrackPoint.of(nativeGetClosestPoint(trackId, lat, lon));
  }

  /** Returns the point at the distance from the track start, or {@code null} if the track is gone. */
  @Nullable
  public static TrackPoint getPointAt(long trackId, double distFromBegM)
  {
    return TrackPoint.of(nativeGetPointAt(trackId, distFromBegM));
  }

  /**
   * Returns the point selected on the track by a tap or in the elevation profile, or {@code null} if there is none.
   */
  @Nullable
  public static TrackPoint getSelectedPoint(long trackId)
  {
    return TrackPoint.of(nativeGetSelectedPoint(trackId));
  }

  /** Whether the current route follows a track. */
  public static boolean isActive()
  {
    return nativeIsActive();
  }

  static void setPending(@Nullable Pending pending)
  {
    sPending = pending;
  }

  /** Whether a track waits for the current navigation to arrive at its entry point. */
  public static boolean hasPending()
  {
    return sPending != null;
  }

  /** Returns and forgets the track to follow once the current navigation arrives. */
  @Nullable
  public static Pending takePending()
  {
    final Pending pending = sPending;
    sPending = null;
    return pending;
  }

  /**
   * Makes the next routes follow the track from the distance until routing closes.
   * @return the track end, or {@code null} if the track is gone or nothing is left after that distance
   */
  @Nullable
  static TrackPoint start(long trackId, double distFromBegM)
  {
    return TrackPoint.of(nativeStart(trackId, distFromBegM));
  }

  @Nullable
  private static native double[] nativeGetClosestPoint(long trackId, double lat, double lon);
  @Nullable
  private static native double[] nativeGetPointAt(long trackId, double distFromBegM);
  @Nullable
  private static native double[] nativeGetSelectedPoint(long trackId);
  @Nullable
  private static native double[] nativeStart(long trackId, double distFromBegM);
  private static native boolean nativeIsActive();
}
