package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/** Maps&Fly: a paragliding takeoff or landing site of the flight preparation layer. */
public final class FlightSite
{
  public static final int NO_ALTITUDE = Integer.MIN_VALUE;
  public static final int SOURCE_PGE = 1;
  public static final int SOURCE_OSM = 2;
  /** Fixed test sites of the app, see FlightSources.testSites. */
  public static final int SOURCE_TEST = 4;

  @NonNull
  public final String mId;
  @NonNull
  public String mName;
  public final double mLat;
  public final double mLon;
  public final boolean mLanding;
  /** Meters above sea level or NO_ALTITUDE. */
  public int mAltitude;
  /** See FlightDirections: bit i is i * 22.5 degrees from north, where the wind should come from. */
  public int mDirections;
  /** SOURCE_* bits. */
  public int mSources;
  /** The lat/lon pairs of a closed ring of a known landing area, or empty. */
  @NonNull
  public double[] mArea;
  /** The page of the site at its source. */
  @Nullable
  public String mUrl;

  public FlightSite(@NonNull String id, @NonNull String name, double lat, double lon, boolean landing, int altitude,
                    int directions, int sources, @NonNull double[] area, @Nullable String url)
  {
    mId = id;
    mName = name;
    mLat = lat;
    mLon = lon;
    mLanding = landing;
    mAltitude = altitude;
    mDirections = directions;
    mSources = sources;
    mArea = area;
    mUrl = url;
  }

  public boolean hasAltitude()
  {
    return mAltitude != NO_ALTITUDE;
  }
}
