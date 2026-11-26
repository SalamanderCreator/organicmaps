package app.organicmaps.sdk.maplayer.flight;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

public class FlightDirectionsTest
{
  private static int bits(int... sectors)
  {
    int mask = 0;
    for (int s : sectors)
      mask |= 1 << s;
    return mask;
  }

  @Test
  public void readsNamesRangesDegreesAndGermanEast()
  {
    assertEquals(bits(0, 14), FlightDirections.parse("N;NW"));
    assertEquals(bits(10, 11, 12, 13, 14), FlightDirections.parse("SW-NW"));
    assertEquals(bits(14, 15, 0, 1, 2), FlightDirections.parse("NW-NE"));
    assertEquals(bits(2, 6), FlightDirections.parse("NO, so"));
    assertEquals(bits(12), FlightDirections.parse("270"));
    assertEquals(bits(11, 12, 13), FlightDirections.parse("250-300"));
    assertEquals(0, FlightDirections.parse("steep"));
    assertEquals(0, FlightDirections.parse(null));
  }

  @Test
  public void formatsAndNamesDirections()
  {
    assertEquals("W, NW", FlightDirections.format(bits(12, 14)));
    assertEquals("SW", FlightDirections.name(230));
    assertEquals("N", FlightDirections.name(-5));
  }

  @Test
  public void ratesTheWindAgainstTheTakeoff()
  {
    final int nw = FlightDirections.parse("NW");
    assertEquals(FlightDirections.Fit.FITS, FlightDirections.fit(nw, 320));
    assertEquals(FlightDirections.Fit.FITS, FlightDirections.fit(nw, 300));
    assertEquals(FlightDirections.Fit.CROSS, FlightDirections.fit(nw, 275));
    assertEquals(FlightDirections.Fit.WRONG, FlightDirections.fit(nw, 230));
    assertEquals(FlightDirections.Fit.TAIL, FlightDirections.fit(nw, 135));
    assertEquals(FlightDirections.Fit.UNKNOWN, FlightDirections.fit(0, 135));
  }
}
