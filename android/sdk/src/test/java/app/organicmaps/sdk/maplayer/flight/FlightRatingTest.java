package app.organicmaps.sdk.maplayer.flight;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public class FlightRatingTest
{
  private static final int WEST = FlightDirections.parse("W");
  private static final double NONE = Double.NaN;

  @Test
  public void fittingModerateWindIsGood()
  {
    final FlightRating r = FlightRating.rate(WEST, 265, 12, 18, 20, 0, 10, 100);
    assertEquals(FlightRating.Level.GOOD, r.mLevel);
    assertTrue(r.mReasons.contains(FlightRating.Reason.DIRECTION_FITS));
  }

  @Test
  public void calmWindIgnoresTheDirection()
  {
    final FlightRating r = FlightRating.rate(WEST, 90, 3, 6, NONE, NONE, -1, NONE);
    assertEquals(FlightRating.Level.GOOD, r.mLevel);
    assertTrue(r.mReasons.contains(FlightRating.Reason.CALM));
  }

  @Test
  public void tailwindIsBad()
  {
    final FlightRating r = FlightRating.rate(WEST, 90, 12, 15, NONE, 0, 0, NONE);
    assertEquals(FlightRating.Level.BAD, r.mLevel);
    assertTrue(r.mReasons.contains(FlightRating.Reason.TAILWIND));
  }

  @Test
  public void crosswindGustsAndRainAreMarginal()
  {
    assertEquals(FlightRating.Level.MARGINAL, FlightRating.rate(WEST, 225, 10, 14, NONE, 0, 0, NONE).mLevel);
    final FlightRating gusty = FlightRating.rate(WEST, 270, 10, 26, NONE, 0, 0, NONE);
    assertEquals(FlightRating.Level.MARGINAL, gusty.mLevel);
    assertTrue(gusty.mReasons.contains(FlightRating.Reason.GUSTY));
    assertEquals(FlightRating.Level.MARGINAL, FlightRating.rate(WEST, 270, 10, 14, NONE, 0, 70, NONE).mLevel);
    assertEquals(FlightRating.Level.MARGINAL, FlightRating.rate(WEST, 270, 10, 14, NONE, 0, 0, 1500).mLevel);
  }

  @Test
  public void strongWindGustsAloftOrRainAreBad()
  {
    assertEquals(FlightRating.Level.BAD, FlightRating.rate(WEST, 270, 30, 34, NONE, 0, 0, NONE).mLevel);
    assertEquals(FlightRating.Level.BAD, FlightRating.rate(WEST, 270, 15, 40, NONE, 0, 0, NONE).mLevel);
    assertEquals(FlightRating.Level.BAD, FlightRating.rate(WEST, 270, 15, 20, 50, 0, 0, NONE).mLevel);
    assertEquals(FlightRating.Level.BAD, FlightRating.rate(WEST, 270, 15, 20, NONE, 1.2, 90, NONE).mLevel);
  }

  @Test
  public void unknownDirectionsRateOnlyTheStrength()
  {
    final FlightRating r = FlightRating.rate(0, 90, 12, 15, NONE, 0, 0, NONE);
    assertEquals(FlightRating.Level.GOOD, r.mLevel);
    assertTrue(r.mReasons.contains(FlightRating.Reason.DIRECTION_UNKNOWN));
  }
}
