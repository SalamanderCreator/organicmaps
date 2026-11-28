package app.organicmaps.sdk.maplayer.flight;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

import org.junit.Test;

public class FlightWeatherCacheTest
{
  @Test
  public void pageGetsTheSiteInGermanDirections()
  {
    final FlightSite site = new FlightSite("test:1", "Hohe Wand Süd", 47.5, 14.25, false, 1050,
                                           FlightDirections.parse("SW;S;SE"), FlightSite.SOURCE_PGE, new double[0]);
    assertEquals("file:///android_asset/flugwetter/index.html?lat=47.500000&lon=14.250000&alt=1050"
                     + "&name=Hohe%20Wand%20S%C3%BCd&dir=SO%2C%20S%2C%20SW",
                 FlightWeatherCache.pageUrl(site));
  }

  @Test
  public void keysDifferPerRequest()
  {
    assertEquals(40, FlightWeatherCache.key("https://api.open-meteo.com/v1/forecast?latitude=47.5").length());
    assertNotEquals(FlightWeatherCache.key("https://api.open-meteo.com/v1/forecast?latitude=47.5"),
                    FlightWeatherCache.key("https://api.open-meteo.com/v1/forecast?latitude=47.6"));
  }
}
