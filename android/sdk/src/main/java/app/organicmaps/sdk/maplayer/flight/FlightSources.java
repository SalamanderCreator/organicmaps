package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Maps&Fly: reads the flight sites file of the APK, which tools/python/flight_sites.py builds from
 * ParaglidingEarth (CC BY-SA) and OpenStreetMap (ODbL) at build time, and the file of the sites imported
 * on the phone in the same format.
 */
public final class FlightSources
{
  public static final String ASSET = "flight_sites.json";

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
      site.mRestricted = o.optBoolean("restricted");
      site.mGuestRules = o.optBoolean("guest");
      final JSONObject details = o.optJSONObject("details");
      if (details != null)
      {
        for (java.util.Iterator<String> keys = details.keys(); keys.hasNext();)
        {
          final String key = keys.next();
          site.mDetails.put(key, details.getString(key));
        }
      }
      sites.add(site);
    }
    return sites;
  }

  /** The file format of parseFile, for the sites imported on the phone. */
  @NonNull
  static String toFile(@NonNull List<FlightSite> sites, @NonNull String generated) throws Exception
  {
    final JSONArray array = new JSONArray();
    for (FlightSite s : sites)
    {
      final JSONObject o = new JSONObject();
      o.put("id", s.mId);
      o.put("name", s.mName);
      o.put("lat", s.mLat);
      o.put("lon", s.mLon);
      o.put("landing", s.mLanding);
      if (s.hasAltitude())
        o.put("alt", s.mAltitude);
      o.put("dirs", s.mDirections);
      o.put("src", s.mSources);
      if (s.mLinks.length > 0)
      {
        final JSONArray links = new JSONArray();
        for (String link : s.mLinks)
          links.put(link);
        o.put("links", links);
      }
      putText(o, "desc", s.mDescription);
      putText(o, "rules", s.mRules);
      putText(o, "access", s.mAccess);
      putText(o, "comments", s.mComments);
      putText(o, "url", s.mUrl);
      putText(o, "web", s.mWebsite);
      if (s.mRestricted)
        o.put("restricted", true);
      if (s.mGuestRules)
        o.put("guest", true);
      if (!s.mDetails.isEmpty())
      {
        final JSONObject details = new JSONObject();
        for (Map.Entry<String, String> e : s.mDetails.entrySet())
          details.put(e.getKey(), e.getValue());
        o.put("details", details);
      }
      array.put(o);
    }
    final JSONObject root = new JSONObject();
    root.put("generated", generated);
    root.put("sites", array);
    return root.toString();
  }

  private static void putText(@NonNull JSONObject o, @NonNull String key, @Nullable String value) throws Exception
  {
    if (value != null && !value.isEmpty())
      o.put(key, value);
  }

  /** The date the file was built, or empty. */
  @NonNull
  static String generated(@NonNull String json) throws Exception
  {
    return new JSONObject(json).optString("generated", "");
  }

  /**
   * Drops the sites of |sites| for which |preferred| has the same kind of site within |distanceM|, like an
   * OpenStreetMap takeoff where a more detailed DHV one is.
   */
  @NonNull
  static List<FlightSite> withoutDuplicates(@NonNull List<FlightSite> sites, @NonNull List<FlightSite> preferred,
                                            double distanceM)
  {
    // About 1 km cells, so the neighbours of a cell hold all close sites.
    final Map<Long, List<FlightSite>> grid = new java.util.HashMap<>();
    for (FlightSite p : preferred)
      grid.computeIfAbsent(cell(p.mLat, p.mLon), k -> new ArrayList<>()).add(p);
    final List<FlightSite> result = new ArrayList<>(sites.size());
    for (FlightSite s : sites)
    {
      boolean duplicate = false;
      final long row = (long) Math.floor(s.mLat * 100);
      final long col = (long) Math.floor(s.mLon * 100);
      for (long dr = -1; dr <= 1 && !duplicate; ++dr)
      {
        for (long dc = -1; dc <= 1 && !duplicate; ++dc)
        {
          final List<FlightSite> near = grid.get((row + dr) * 100000 + col + dc);
          if (near == null)
            continue;
          for (FlightSite p : near)
          {
            if (p.mLanding == s.mLanding && distanceM(p.mLat, p.mLon, s.mLat, s.mLon) < distanceM)
            {
              duplicate = true;
              break;
            }
          }
        }
      }
      if (!duplicate)
        result.add(s);
    }
    return result;
  }

  private static long cell(double lat, double lon)
  {
    return (long) Math.floor(lat * 100) * 100000 + (long) Math.floor(lon * 100);
  }

  static double distanceM(double lat1, double lon1, double lat2, double lon2)
  {
    final double r = 6371000;
    final double dLat = Math.toRadians(lat2 - lat1);
    final double dLon = Math.toRadians(lon2 - lon1) * Math.cos(Math.toRadians((lat1 + lat2) / 2));
    return r * Math.sqrt(dLat * dLat + dLon * dLon);
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
}
