package app.organicmaps.sdk.maplayer.flight;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class FlightSourcesTest
{
  private static final String PGE = "{\"type\":\"FeatureCollection\",\"features\":[{\"type\":\"Feature\","
                                   + "\"geometry\":{\"type\":\"Point\",\"coordinates\":[11.0,47.0]},"
                                   + "\"properties\":{\"name\":\"Hochfelln\",\"pge_site_id\":\"42\","
                                   + "\"takeoff_altitude\":\"1650\",\"paragliding\":\"1\",\"N\":\"0\",\"W\":\"2\","
                                   + "\"NW\":\"1\",\"landing_lat\":\"47.01\",\"landing_lng\":\"11.01\","
                                   + "\"landing_altitude\":\"0\"}}]}";

  // A landing area around the PGE landing and an OSM-only takeoff.
  private static final String OSM =
      "{\"elements\":[{\"type\":\"way\",\"id\":7,\"tags\":{\"free_flying:site\":\"landing\",\"ele\":\"600 m\"},"
      + "\"geometry\":[{\"lat\":47.009,\"lon\":11.009},{\"lat\":47.011,\"lon\":11.009},"
      + "{\"lat\":47.011,\"lon\":11.011},{\"lat\":47.009,\"lon\":11.011},{\"lat\":47.009,\"lon\":11.009}]},"
      + "{\"type\":\"node\",\"id\":9,\"lat\":47.5,\"lon\":11.5,\"tags\":{\"free_flying:site\":\"takeoff\","
      + "\"name\":\"Kleiner Start\",\"free_flying:site:orientation\":\"S;SW\"}},"
      + "{\"type\":\"node\",\"id\":10,\"lat\":47.6,\"lon\":11.6,\"tags\":{\"free_flying:site\":\"takeoff\","
      + "\"free_flying:paragliding\":\"no\"}}]}";

  @Test
  public void readsParaglidingEarthTakeoffsAndLandings() throws Exception
  {
    final List<FlightSite> sites = FlightSources.parsePge(PGE);
    assertEquals(2, sites.size());
    final FlightSite takeoff = sites.get(0);
    assertEquals("pge:42", takeoff.mId);
    assertEquals(1650, takeoff.mAltitude);
    assertEquals(FlightDirections.parse("W;NW"), takeoff.mDirections);
    assertFalse(takeoff.mLanding);
    final FlightSite landing = sites.get(1);
    assertTrue(landing.mLanding);
    assertEquals("Hochfelln", landing.mName);
    assertFalse("0 is unknown", landing.hasAltitude());
  }

  @Test
  public void readsOverpassSitesAndMergesTheSameSite() throws Exception
  {
    final List<FlightSite> osm = FlightSources.parseOverpass(OSM);
    assertEquals(2, osm.size());
    assertEquals(10, osm.get(0).mArea.length);
    assertEquals(47.01, osm.get(0).mLat, 1e-9);
    assertEquals(FlightDirections.parse("S;SW"), osm.get(1).mDirections);

    final List<FlightSite> merged = FlightSources.merge(FlightSources.parsePge(PGE), osm);
    assertEquals(3, merged.size());
    final FlightSite landing = merged.get(1);
    assertEquals(FlightSite.SOURCE_PGE | FlightSite.SOURCE_OSM, landing.mSources);
    assertEquals(10, landing.mArea.length);
    assertEquals(600, landing.mAltitude);
    assertEquals("osm:n9", merged.get(2).mId);
  }

  @Test
  public void keepsSitesInTheCache() throws Exception
  {
    final List<FlightSite> sites = FlightSources.merge(FlightSources.parsePge(PGE), FlightSources.parseOverpass(OSM));
    final List<FlightSite> read = FlightSources.fromCache(FlightSources.toCache(sites));
    assertEquals(sites.size(), read.size());
    for (int i = 0; i < sites.size(); ++i)
    {
      assertEquals(sites.get(i).mId, read.get(i).mId);
      assertEquals(sites.get(i).mDirections, read.get(i).mDirections);
      assertEquals(sites.get(i).mAltitude, read.get(i).mAltitude);
      assertEquals(sites.get(i).mArea.length, read.get(i).mArea.length);
    }
  }

  @Test
  public void cellBoundsMatchTheCoreCells()
  {
    // FlightLayer::CellKey(47.5, 11.5) == (47 + 90) * 360 + (11 + 180)
    final double[] b = FlightSources.cellBounds((47 + 90) * 360 + (11 + 180));
    assertEquals(47, b[0], 0);
    assertEquals(11, b[1], 0);
    assertEquals(48, b[2], 0);
    assertEquals(12, b[3], 0);
  }
}
