package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.util.LinkedHashMap;
import java.util.Map;

/** Maps&Fly: a paragliding takeoff or landing site. */
public final class FlightSite
{
  public static final int NO_ALTITUDE = Integer.MIN_VALUE;
  public static final int SOURCE_PGE = 1;
  public static final int SOURCE_OSM = 2;
  /** Fixed test sites of the app, see FlightSources.testSites. */
  public static final int SOURCE_TEST = 4;
  /** Privately imported from the DHV on this phone, see FlightDhvImport. */
  public static final int SOURCE_DHV = 8;

  // Keys of mDetails, shown with their own labels.
  public static final String DETAIL_HEIGHT_DIFFERENCE = "height_difference";
  public static final String DETAIL_SUITABILITY = "suitability";
  public static final String DETAIL_CABLE_CAR = "cable_car";
  public static final String DETAIL_OTHER_TAKEOFFS = "other_takeoffs";
  public static final String DETAIL_INFO = "info";
  public static final String DETAIL_WEATHER = "weather";
  public static final String DETAIL_WEBCAMS = "webcams";
  public static final String DETAIL_HOLDER = "holder";
  public static final String DETAIL_CONTACT = "contact";
  /** Comma separated ACCESS_* values. */
  public static final String DETAIL_ACCESS_BY = "access_by";
  public static final String ACCESS_CAR = "car";
  public static final String ACCESS_PUBLIC_TRANSPORT = "public_transport";
  public static final String ACCESS_FOOT = "foot";

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
  /** There are rules for guest pilots, like a sign-up or a briefing. */
  public boolean mGuestRules;
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
  /** More texts of the source by DETAIL_* keys, in their order. */
  @NonNull
  public Map<String, String> mDetails = new LinkedHashMap<>();

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
