package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/** Maps&Fly: a paragliding takeoff or landing site. */
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
  public final String mName;
  public final double mLat;
  public final double mLon;
  public final boolean mLanding;
  /** Meters above sea level or NO_ALTITUDE. */
  public int mAltitude;
  /** The altitude is taken from the terrain of the map, the source has none. */
  public boolean mAltitudeFromTerrain;
  /** See FlightDirections: bit i is i * 22.5 degrees from north, where the wind should come from. */
  public final int mDirections;
  /** SOURCE_* bits. */
  public final int mSources;
  /** Shown with an orange ring, e.g. for club members only. */
  public boolean mRestricted;
  /** The lat/lon pairs of a closed ring of a known landing area, or empty. */
  @NonNull
  public final double[] mArea;
  /** The ids of the sites that belong together at the source: the landings of a takeoff and back. */
  @NonNull
  public String[] mLinks = new String[0];

  // Texts and links of the sources, empty if unknown.
  @NonNull
  public String mDescription = "";
  @NonNull
  public String mRules = "";
  @NonNull
  public String mAccess = "";
  @NonNull
  public String mComments = "";
  @Nullable
  public String mUrl;
  @Nullable
  public String mOsmUrl;
  @Nullable
  public String mWebsite;

  public FlightSite(@NonNull String id, @NonNull String name, double lat, double lon, boolean landing, int altitude,
                    int directions, int sources, @NonNull double[] area)
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
  }

  public boolean hasAltitude()
  {
    return mAltitude != NO_ALTITUDE;
  }
}
