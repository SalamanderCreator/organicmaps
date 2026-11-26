package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Maps&Fly: reads the flight sites file of the APK, which tools/python/flight_sites.py builds from
 * ParaglidingEarth (CC BY-SA) and OpenStreetMap (ODbL) at build time, and finds the sites that belong
 * together.
 */
public final class FlightSources
{
  public static final String ASSET = "flight_sites.json";
  // Landings this close to a takeoff belong to it, and back.
  static final double NEARBY_M = 8000;
  static final int MAX_RELATED = 6;

  private FlightSources() {}

  @NonNull
  static List<FlightSite> parseFile(@NonNull String json) throws Exception
  {
    final JSONArray array = new JSONObject(json).getJSONArray("sites");
    final List<FlightSite> sites = new ArrayList<>(array.length());
    for (int i = 0; i < array.length(); ++i)
    {
      final JSONObject o = array.getJSONObject(i);
      final JSONArray jArea = o.optJSONArray("area");
      final double[] area = new double[jArea == null ? 0 : jArea.length()];
      for (int k = 0; k < area.length; ++k)
        area[k] = jArea.getDouble(k);
      final FlightSite site = new FlightSite(o.getString("id"), o.optString("name", ""), o.getDouble("lat"),
                                             o.getDouble("lon"), o.optBoolean("landing"),
                                             o.optInt("alt", FlightSite.NO_ALTITUDE), o.optInt("dirs"),
                                             o.optInt("src"), area);
      final JSONArray links = o.optJSONArray("links");
      if (links != null)
      {
        site.mLinks = new String[links.length()];
        for (int k = 0; k < links.length(); ++k)
          site.mLinks[k] = links.getString(k);
      }
      site.mDescription = o.optString("desc", "");
      site.mRules = o.optString("rules", "");
      site.mAccess = o.optString("access", "");
      site.mComments = o.optString("comments", "");
      site.mUrl = text(o, "url");
      site.mOsmUrl = text(o, "osm");
      site.mWebsite = text(o, "web");
      if (site.mOsmUrl == null && site.mId.startsWith("osm:"))
        site.mOsmUrl = site.mUrl;
      sites.add(site);
    }
    return sites;
  }

  @Nullable
  private static String text(@NonNull JSONObject o, @NonNull String key)
  {
    final String value = o.optString(key, "");
    return value.isEmpty() ? null : value;
  }

  /**
   * Maps&Fly test sites at Liezen, to test the layer while ParaglidingEarth and Overpass cannot be reached.
   * Their height comes from the terrain of the map. Remove them once real data shows up.
   */
  @NonNull
  static List<FlightSite> testSites()
  {
    final FlightSite takeoff =
        new FlightSite("test:liezen", "Test-Startplatz Liezen", 47.0 + 34.0 / 60 + 55.09 / 3600,
                       14.0 + 13.0 / 60 + 55.57 / 3600, false, FlightSite.NO_ALTITUDE,
                       FlightDirections.parse("SW;S;SE"), FlightSite.SOURCE_TEST, new double[0]);
    final FlightSite landing =
        new FlightSite("test:liezen:landing", "Test-Landeplatz Liezen", 47.0 + 33.0 / 60 + 58.39 / 3600,
                       14.0 + 13.0 / 60 + 52.17 / 3600, true, FlightSite.NO_ALTITUDE, 0, FlightSite.SOURCE_TEST,
                       new double[0]);
    takeoff.mLinks = new String[] {landing.mId};
    landing.mLinks = new String[] {takeoff.mId};
    final List<FlightSite> sites = new ArrayList<>();
    sites.add(takeoff);
    sites.add(landing);
    return sites;
  }

  /** A site with its distance from another one. */
  public static final class Related
  {
    @NonNull
    public final FlightSite mSite;
    public final double mDistanceM;

    Related(@NonNull FlightSite site, double distanceM)
    {
      mSite = site;
      mDistanceM = distanceM;
    }
  }

  /**
   * The landings of a takeoff, or the takeoffs of a landing: the ones the source links and the ones
   * nearby, closest first.
   */
  @NonNull
  static List<Related> related(@NonNull FlightSite site, @NonNull Map<String, FlightSite> sites)
  {
    final List<Related> result = new ArrayList<>();
    for (String id : site.mLinks)
    {
      final FlightSite linked = sites.get(id);
      if (linked != null && linked.mLanding != site.mLanding)
        result.add(new Related(linked, distanceM(site.mLat, site.mLon, linked.mLat, linked.mLon)));
    }
    final List<Related> nearby = new ArrayList<>();
    for (FlightSite other : sites.values())
    {
      if (other.mLanding == site.mLanding || contains(result, other))
        continue;
      final double d = distanceM(site.mLat, site.mLon, other.mLat, other.mLon);
      if (d > NEARBY_M)
        continue;
      // A landing lies below its takeoff.
      final FlightSite takeoff = site.mLanding ? other : site;
      final FlightSite landing = site.mLanding ? site : other;
      if (takeoff.hasAltitude() && landing.hasAltitude() && landing.mAltitude >= takeoff.mAltitude)
        continue;
      nearby.add(new Related(other, d));
    }
    Collections.sort(result, Comparator.comparingDouble(r -> r.mDistanceM));
    Collections.sort(nearby, Comparator.comparingDouble(r -> r.mDistanceM));
    for (Related r : nearby)
    {
      if (result.size() >= MAX_RELATED)
        break;
      result.add(r);
    }
    return result;
  }

  private static boolean contains(@NonNull List<Related> list, @NonNull FlightSite site)
  {
    for (Related r : list)
      if (r.mSite == site)
        return true;
    return false;
  }

  static double distanceM(double lat1, double lon1, double lat2, double lon2)
  {
    final double r = 6371000;
    final double dLat = Math.toRadians(lat2 - lat1);
    final double dLon = Math.toRadians(lon2 - lon1) * Math.cos(Math.toRadians((lat1 + lat2) / 2));
    return r * Math.sqrt(dLat * dLat + dLon * dLon);
  }
}
