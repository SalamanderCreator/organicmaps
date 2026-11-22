package app.organicmaps.sdk.location;

/** Maps&Fly: watches the distance between the GPS position and a chosen track. */
public final class OffTrackMonitor
{
  // The order of the native OffTrackMonitor::State.
  public static final int STATE_ON_TRACK = 0;
  public static final int STATE_OFF_TRACK = 1;
  public static final int STATE_GPS_INACCURATE = 2;

  private OffTrackMonitor() {}

  /** Returns {@code false} if the track has no geometry. */
  public static native boolean nativeStart(long trackId, double thresholdMeters);

  public static native void nativeStop();

  public static native boolean nativeIsActive();

  public static native long nativeGetTrackId();

  public static native void nativeSetThreshold(double thresholdMeters);

  /** Returns the state after this fix, one of {@code STATE_*}. */
  public static native int nativeUpdate(double lat, double lon, double accuracyMeters, double timestampSeconds);

  /** Distance from the last fix to the track in meters, -1 before the first fix. */
  public static native double nativeGetDistance();
}
