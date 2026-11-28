package app.organicmaps.sdk.maplayer.flight;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class FlightSourcesTest
{
  // The shape tools/python/flight_sites.py writes.
  private static final String FILE =
      "{\"generated\":\"2026-11-26T10:00:00Z\",\"sources\":{\"pge\":true,\"osm\":true},\"sites\":["
      + "{\"id\":\"pge:42\",\"name\":\"Hochfelln\",\"lat\":47.0,\"lon\":11.0,\"landing\":false,\"dirs\":20480,"
      + "\"src\":1,\"url\":\"https://www.paragliding.earth/?site=42\",\"alt\":1650,\"rules\":\"Einweisung\","
      + "\"links\":[\"pge:42:landing\"]},"
      + "{\"id\":\"pge:42:landing\",\"name\":\"Hochfelln\",\"lat\":47.01,\"lon\":11.01,\"landing\":true,\"dirs\":0,"
      + "\"src\":3,\"url\":\"https://www.paragliding.earth/?site=42\",\"alt\":600,\"osm\":\"https://www.openstreetmap.org/way/7\","
      + "\"area\":[47.009,11.009,47.011,11.009,47.011,11.011,47.009,11.011,47.009,11.009],\"links\":[\"pge:42\"]},"
      + "{\"id\":\"osm:n9\",\"name\":\"Wiese\",\"lat\":47.02,\"lon\":11.0,\"landing\":true,\"dirs\":0,\"src\":2,"
      + "\"url\":\"https://www.openstreetmap.org/node/9\",\"alt\":700},"
      + "{\"id\":\"osm:n10\",\"name\":\"Gipfelwiese\",\"lat\":47.005,\"lon\":11.0,\"landing\":true,\"dirs\":0,\"src\":2,"
      + "\"url\":\"https://www.openstreetmap.org/node/10\",\"alt\":1700},"
      + "{\"id\":\"osm:n11\",\"name\":\"Weit weg\",\"lat\":47.5,\"lon\":11.0,\"landing\":true,\"dirs\":0,\"src\":2,"
      + "\"url\":\"https://www.openstreetmap.org/node/11\"}]}";

  @Test
  public void readsTheSitesFile() throws Exception
  {
    final List<FlightSite> sites = FlightSources.parseFile(FILE);
    assertEquals(5, sites.size());
    final FlightSite takeoff = sites.get(0);
    assertFalse(takeoff.mLanding);
    assertEquals(1650, takeoff.mAltitude);
    assertEquals(FlightDirections.parse("W;NW"), takeoff.mDirections);
    assertEquals("Einweisung", takeoff.mRules);
    assertNull(takeoff.mOsmUrl);
    final FlightSite landing = sites.get(1);
    assertTrue(landing.mLanding);
    assertEquals(10, landing.mArea.length);
    assertEquals("https://www.openstreetmap.org/way/7", landing.mOsmUrl);
    assertEquals("https://www.openstreetmap.org/node/9", sites.get(2).mOsmUrl);
    assertFalse(sites.get(4).hasAltitude());
  }

  @Test
  public void readsTheCables() throws Exception
  {
    final List<double[]> cables = FlightSources.parseCables(
        "{\"sites\":[],\"cables\":[{\"id\":\"osm:w4\",\"line\":[47.1,11.1,47.11,11.12]},"
        + "{\"id\":\"osm:w5\",\"line\":[47.2,11.2]}]}");
    assertEquals(1, cables.size());
    assertEquals(11.12, cables.get(0)[3], 1e-9);
    assertEquals(0, FlightSources.parseCables(FILE).size());
  }
}
