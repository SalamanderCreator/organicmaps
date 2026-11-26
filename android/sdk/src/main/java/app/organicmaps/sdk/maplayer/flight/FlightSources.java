package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.util.ArrayList;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Maps&Fly: reads the flight sites file of the APK, which tools/python/flight_sites.py builds from
 * ParaglidingEarth (CC BY-SA) and OpenStreetMap (ODbL) at build time.
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
}
