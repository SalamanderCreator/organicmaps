package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.net.URLEncoder;
import java.io.UnsupportedEncodingException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Maps&Fly: reads the takeoff and landing sites of a 1 x 1 degree cell from ParaglidingEarth (free for
 * non-commercial use, credit "paraglidingearth.com") and OpenStreetMap (Overpass API, ODbL), merges the
 * same sites of both and keeps them in a cache file of its own format.
 */
final class FlightSources
{
  // ParaglidingEarth moves to paragliding.earth, the old domain may stay for a while.
  private static final String[] PGE_HOSTS = {"https://www.paragliding.earth", "https://www.paraglidingearth.com"};
  private static final String PGE_PATH = "/api/geojson/getBoundingBoxSites.php"
                                       + "?north=%.4f&south=%.4f&east=%.4f&west=%.4f&limit=1000&style=detailled";
  private static final String PGE_SITE_URL = "https://www.paraglidingearth.com/?site=";
  // The main server is often busy, the others mirror it.
  private static final String[] OVERPASS_URLS = {"https://overpass-api.de/api/interpreter?data=",
                                                 "https://overpass.private.coffee/api/interpreter?data=",
                                                 "https://overpass.kumi.systems/api/interpreter?data="};
  private static final String OVERPASS_QUERY =
      "[out:json][timeout:25];nwr[\"free_flying:site\"](%.4f,%.4f,%.4f,%.4f);out tags geom;";
  private static final String OSM_URL = "https://www.openstreetmap.org/";
  private static final String[] PGE_DIRECTIONS = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  private static final String[] OSM_DIRECTION_KEYS = {"free_flying:site:orientation", "free_flying:takeoff:direction",
                                                      "free_flying:orientation", "direction", "orientation"};
  // The same site in both sources is usually closer than this.
  private static final double SAME_SITE_M = 200;

  private FlightSources() {}

  /** South, west, north, east of a cell of FlightLayer::CellKey. */
  @NonNull
  static double[] cellBounds(int cell)
  {
    final double south = cell / 360 - 90;
    final double west = cell % 360 - 180;
    return new double[] {south, west, south + 1, west + 1};
  }

  /** The same request at each host, to try one after the other. */
  @NonNull
  static String[] pgeUrls(int cell)
  {
    final double[] b = cellBounds(cell);
    final String path = String.format(Locale.US, PGE_PATH, b[2], b[0], b[3], b[1]);
    final String[] urls = new String[PGE_HOSTS.length];
    for (int i = 0; i < urls.length; ++i)
      urls[i] = PGE_HOSTS[i] + path;
    return urls;
  }

  /** The same query at each server, to try one after the other. */
  @NonNull
  static String[] overpassUrls(int cell) throws UnsupportedEncodingException
  {
    final double[] b = cellBounds(cell);
    final String query = URLEncoder.encode(String.format(Locale.US, OVERPASS_QUERY, b[0], b[1], b[2], b[3]), "UTF-8");
    final String[] urls = new String[OVERPASS_URLS.length];
    for (int i = 0; i < urls.length; ++i)
      urls[i] = OVERPASS_URLS[i] + query;
    return urls;
  }

  /** ParaglidingEarth GeoJSON: a takeoff per feature, with its main landing if given. */
  @NonNull
  static List<FlightSite> parsePge(@NonNull String json) throws Exception
  {
    final List<FlightSite> sites = new ArrayList<>();
    final JSONArray features = new JSONObject(json).optJSONArray("features");
    if (features == null)
      return sites;
    for (int i = 0; i < features.length(); ++i)
    {
      final JSONObject feature = features.getJSONObject(i);
      final JSONObject p = feature.optJSONObject("properties");
      final JSONObject geometry = feature.optJSONObject("geometry");
      final JSONArray coordinates = geometry == null ? null : geometry.optJSONArray("coordinates");
      if (p == null || coordinates == null || coordinates.length() < 2)
        continue;
      // Hang gliding only sites.
      if (p.has("paragliding") && p.optInt("paragliding", 1) == 0)
        continue;

      final String id = p.optString("pge_site_id", p.optString("id", ""));
      if (id.isEmpty())
        continue;
      final String name = p.optString("name", "").trim();
      final String url = PGE_SITE_URL + id;

      int directions = 0;
      final JSONObject orientations = p.optJSONObject("orientations");
      final JSONObject source = orientations != null ? orientations : p;
      for (int d = 0; d < PGE_DIRECTIONS.length; ++d)
        if (source.optInt(PGE_DIRECTIONS[d], 0) > 0)
          directions |= FlightDirections.fromMainDirection(d);

      sites.add(new FlightSite("pge:" + id, name, coordinates.getDouble(1), coordinates.getDouble(0), false,
                               altitude(p.optString("takeoff_altitude", null)), directions, FlightSite.SOURCE_PGE,
                               new double[0], url));

      final double landingLat = p.optDouble("landing_lat", Double.NaN);
      final double landingLon = p.optDouble("landing_lng", p.optDouble("landing_lon", Double.NaN));
      if (!Double.isNaN(landingLat) && !Double.isNaN(landingLon) && (landingLat != 0 || landingLon != 0))
      {
        String landingName = p.optString("landing_name", "").trim();
        if (landingName.isEmpty())
          landingName = name;
        sites.add(new FlightSite("pge:" + id + ":landing", landingName, landingLat, landingLon, true,
                                 altitude(p.optString("landing_altitude", null)), 0, FlightSite.SOURCE_PGE,
                                 new double[0], url));
      }
    }
    return sites;
  }

  /** Overpass JSON with "out tags geom": nodes, ways with their points and relations with their bounds. */
  @NonNull
  static List<FlightSite> parseOverpass(@NonNull String json) throws Exception
  {
    final List<FlightSite> sites = new ArrayList<>();
    final JSONArray elements = new JSONObject(json).optJSONArray("elements");
    if (elements == null)
      return sites;
    for (int i = 0; i < elements.length(); ++i)
    {
      final JSONObject e = elements.getJSONObject(i);
      final JSONObject tags = e.optJSONObject("tags");
      if (tags == null || "no".equals(tags.optString("free_flying:paragliding")))
        continue;
      final String kind = tags.optString("free_flying:site").toLowerCase(Locale.ROOT);
      final boolean landing;
      if (kind.contains("landing"))
        landing = true;
      else if (kind.contains("takeoff") || kind.contains("launch") || kind.contains("towing") || kind.contains("winch"))
        landing = false;
      else
        continue;

      final String type = e.optString("type");
      double lat;
      double lon;
      double[] area = new double[0];
      if ("node".equals(type))
      {
        lat = e.optDouble("lat", Double.NaN);
        lon = e.optDouble("lon", Double.NaN);
      }
      else if ("way".equals(type) && e.optJSONArray("geometry") != null)
      {
        final JSONArray geometry = e.getJSONArray("geometry");
        final int n = geometry.length();
        if (n == 0)
          continue;
        final JSONObject first = geometry.getJSONObject(0);
        final JSONObject last = geometry.getJSONObject(n - 1);
        final boolean closed = n >= 4 && first.optDouble("lat") == last.optDouble("lat")
                            && first.optDouble("lon") == last.optDouble("lon");
        if (closed)
        {
          area = new double[n * 2];
          lat = 0;
          lon = 0;
          for (int k = 0; k < n; ++k)
          {
            final JSONObject pt = geometry.getJSONObject(k);
            area[2 * k] = pt.getDouble("lat");
            area[2 * k + 1] = pt.getDouble("lon");
            if (k + 1 < n)
            {
              lat += area[2 * k];
              lon += area[2 * k + 1];
            }
          }
          lat /= n - 1;
          lon /= n - 1;
        }
        else
        {
          final JSONObject middle = geometry.getJSONObject(n / 2);
          lat = middle.getDouble("lat");
          lon = middle.getDouble("lon");
        }
      }
      else
      {
        final JSONObject bounds = e.optJSONObject("bounds");
        if (bounds == null)
          continue;
        lat = (bounds.optDouble("minlat") + bounds.optDouble("maxlat")) / 2;
        lon = (bounds.optDouble("minlon") + bounds.optDouble("maxlon")) / 2;
      }
      if (Double.isNaN(lat) || Double.isNaN(lon))
        continue;

      int directions = 0;
      if (!landing)
        for (String key : OSM_DIRECTION_KEYS)
          directions |= FlightDirections.parse(tags.optString(key, null));

      final long osmId = e.optLong("id");
      sites.add(new FlightSite("osm:" + type.charAt(0) + osmId, tags.optString("name", "").trim(), lat, lon, landing,
                               altitude(tags.optString("ele", null)), directions, FlightSite.SOURCE_OSM, area,
                               OSM_URL + type + "/" + osmId));
    }
    return sites;
  }

  /**
   * Maps&Fly test sites at Liezen, shown whatever the sources answer, to test the layer while
   * ParaglidingEarth and Overpass cannot be reached. Remove them once real data shows up.
   */
  @NonNull
  static List<FlightSite> testSites(int cell)
  {
    final List<FlightSite> sites = new ArrayList<>();
    final FlightSite[] all = {
        new FlightSite("test:liezen", "Test-Startplatz Liezen", 47.0 + 34.0 / 60 + 55.09 / 3600,
                       14.0 + 13.0 / 60 + 55.57 / 3600, false, FlightSite.NO_ALTITUDE, 0, FlightSite.SOURCE_TEST,
                       new double[0], null),
        new FlightSite("test:liezen:landing", "Test-Landeplatz Liezen", 47.0 + 33.0 / 60 + 58.39 / 3600,
                       14.0 + 13.0 / 60 + 52.17 / 3600, true, FlightSite.NO_ALTITUDE, 0, FlightSite.SOURCE_TEST,
                       new double[0], null)};
    final double[] b = cellBounds(cell);
    for (FlightSite site : all)
      if (site.mLat >= b[0] && site.mLat < b[2] && site.mLon >= b[1] && site.mLon < b[3])
        sites.add(site);
    return sites;
  }

  /** Adds the OpenStreetMap sites to the ParaglidingEarth ones, a site in both only once. */
  @NonNull
  static List<FlightSite> merge(@NonNull List<FlightSite> pge, @NonNull List<FlightSite> osm)
  {
    final List<FlightSite> result = new ArrayList<>(pge);
    for (FlightSite o : osm)
    {
      FlightSite same = null;
      for (FlightSite p : pge)
      {
        if (p.mLanding == o.mLanding && (distanceM(p.mLat, p.mLon, o.mLat, o.mLon) < SAME_SITE_M
                                         || (o.mArea.length > 0 && isInside(o.mArea, p.mLat, p.mLon))))
        {
          same = p;
          break;
        }
      }
      if (same == null)
      {
        result.add(o);
        continue;
      }
      same.mSources |= FlightSite.SOURCE_OSM;
      if (same.mDirections == 0)
        same.mDirections = o.mDirections;
      if (same.mArea.length == 0)
        same.mArea = o.mArea;
      if (!same.hasAltitude())
        same.mAltitude = o.mAltitude;
      if (same.mName.isEmpty())
        same.mName = o.mName;
    }
    return result;
  }

  @NonNull
  static String toCache(@NonNull List<FlightSite> sites) throws Exception
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
      if (s.mArea.length > 0)
      {
        final JSONArray area = new JSONArray();
        for (double v : s.mArea)
          area.put(v);
        o.put("area", area);
      }
      if (s.mUrl != null)
        o.put("url", s.mUrl);
      array.put(o);
    }
    return array.toString();
  }

  @NonNull
  static List<FlightSite> fromCache(@NonNull String json) throws Exception
  {
    final JSONArray array = new JSONArray(json);
    final List<FlightSite> sites = new ArrayList<>(array.length());
    for (int i = 0; i < array.length(); ++i)
    {
      final JSONObject o = array.getJSONObject(i);
      final JSONArray jArea = o.optJSONArray("area");
      final double[] area = new double[jArea == null ? 0 : jArea.length()];
      for (int k = 0; k < area.length; ++k)
        area[k] = jArea.getDouble(k);
      sites.add(new FlightSite(o.getString("id"), o.optString("name", ""), o.getDouble("lat"), o.getDouble("lon"),
                               o.optBoolean("landing"), o.optInt("alt", FlightSite.NO_ALTITUDE), o.optInt("dirs"),
                               o.optInt("src"), area, o.has("url") ? o.getString("url") : null));
    }
    return sites;
  }

  /** The leading number of "1250", "1250 m" or "1250.5", NO_ALTITUDE if none. */
  static int altitude(@Nullable String value)
  {
    if (value == null)
      return FlightSite.NO_ALTITUDE;
    final java.util.regex.Matcher m = java.util.regex.Pattern.compile("^\\s*(-?\\d+(?:[.,]\\d+)?)").matcher(value);
    if (!m.find())
      return FlightSite.NO_ALTITUDE;
    final double alt = Double.parseDouble(m.group(1).replace(',', '.'));
    // ParaglidingEarth writes 0 for an unknown height.
    return alt == 0 ? FlightSite.NO_ALTITUDE : (int) Math.round(alt);
  }

  static double distanceM(double lat1, double lon1, double lat2, double lon2)
  {
    final double r = 6371000;
    final double dLat = Math.toRadians(lat2 - lat1);
    final double dLon = Math.toRadians(lon2 - lon1) * Math.cos(Math.toRadians((lat1 + lat2) / 2));
    return r * Math.sqrt(dLat * dLat + dLon * dLon);
  }

  /** Even-odd rule on lat/lon pairs. */
  static boolean isInside(@NonNull double[] ring, double lat, double lon)
  {
    boolean inside = false;
    final int n = ring.length / 2;
    for (int i = 0, j = n - 1; i < n; j = i++)
    {
      final double yi = ring[2 * i];
      final double xi = ring[2 * i + 1];
      final double yj = ring[2 * j];
      final double xj = ring[2 * j + 1];
      if ((yi > lat) != (yj > lat) && lon < (xj - xi) * (lat - yi) / (yj - yi) + xi)
        inside = !inside;
    }
    return inside;
  }
}
