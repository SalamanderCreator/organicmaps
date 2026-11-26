package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import java.util.Locale;

/**
 * Maps&Fly: takeoff directions as 16 sectors. Bit i stands for i * 22.5 degrees clockwise from north
 * and means that the wind should blow from there, like a takeoff "NW" faces north-west.
 */
public final class FlightDirections
{
  public static final int SECTORS = 16;
  private static final double SECTOR_DEG = 360.0 / SECTORS;
  private static final String[] NAMES = {"N",  "NNE", "NE", "ENE", "E",  "ESE", "SE", "SSE",
                                         "S",  "SSW", "SW", "WSW", "W",  "WNW", "NW", "NNW"};

  /** How the wind direction suits the takeoff directions. */
  public enum Fit
  {
    /** No takeoff directions known. */
    UNKNOWN,
    FITS,
    /** Up to 45 degrees off: crosswind on the slope. */
    CROSS,
    WRONG,
    /** From behind the takeoff. */
    TAIL
  }

  private FlightDirections() {}

  /**
   * Reads directions like "N;NW", "SW-NW", "W, NW", "NO" (German east), "270" or "250-300". Ranges run
   * clockwise. Unknown parts are skipped.
   */
  public static int parse(@Nullable String value)
  {
    if (value == null)
      return 0;
    int mask = 0;
    for (String part : value.split("[;,/ ]+"))
    {
      part = part.trim();
      if (part.isEmpty())
        continue;
      final int dash = part.indexOf('-', 1);
      if (dash > 0)
      {
        final int from = toSector(part.substring(0, dash));
        final int to = toSector(part.substring(dash + 1));
        if (from < 0 || to < 0)
          continue;
        for (int s = from;; s = (s + 1) % SECTORS)
        {
          mask |= 1 << s;
          if (s == to)
            break;
        }
      }
      else
      {
        final int sector = toSector(part);
        if (sector >= 0)
          mask |= 1 << sector;
      }
    }
    return mask;
  }

  /** The sector of a compass name or of degrees, -1 if unknown. */
  static int toSector(@NonNull String value)
  {
    final String name = value.trim().toUpperCase(Locale.ROOT).replace('O', 'E');
    for (int i = 0; i < SECTORS; ++i)
      if (NAMES[i].equals(name))
        return i;
    try
    {
      final double degrees = Double.parseDouble(name);
      if (degrees < 0 || degrees > 360)
        return -1;
      return (int) Math.round(degrees / SECTOR_DEG) % SECTORS;
    }
    catch (NumberFormatException e)
    {
      return -1;
    }
  }

  /** The sector bit of one of the 8 main directions of ParaglidingEarth, index 0 is north. */
  public static int fromMainDirection(int index)
  {
    return 1 << (index * 2);
  }

  @NonNull
  public static String format(int mask)
  {
    final StringBuilder sb = new StringBuilder();
    for (int i = 0; i < SECTORS; ++i)
    {
      if ((mask & (1 << i)) == 0)
        continue;
      if (sb.length() > 0)
        sb.append(", ");
      sb.append(NAMES[i]);
    }
    return sb.toString();
  }

  /** The compass name of a wind direction in degrees. */
  @NonNull
  public static String name(double degrees)
  {
    return NAMES[(int) Math.round(normalize(degrees) / SECTOR_DEG) % SECTORS];
  }

  /** The smallest angle between |windFromDeg| and a takeoff direction, or -1 without directions. */
  public static double offset(int mask, double windFromDeg)
  {
    double best = -1;
    for (int i = 0; i < SECTORS; ++i)
    {
      if ((mask & (1 << i)) == 0)
        continue;
      double diff = Math.abs(normalize(windFromDeg) - i * SECTOR_DEG);
      if (diff > 180)
        diff = 360 - diff;
      if (best < 0 || diff < best)
        best = diff;
    }
    return best;
  }

  @NonNull
  public static Fit fit(int mask, double windFromDeg)
  {
    final double offset = offset(mask, windFromDeg);
    if (offset < 0)
      return Fit.UNKNOWN;
    // A sector is 22.5 degrees wide, and a wind a little beside it still works on most slopes.
    if (offset <= SECTOR_DEG)
      return Fit.FITS;
    if (offset <= 2 * SECTOR_DEG)
      return Fit.CROSS;
    if (offset >= 5 * SECTOR_DEG)
      return Fit.TAIL;
    return Fit.WRONG;
  }

  private static double normalize(double degrees)
  {
    final double d = degrees % 360;
    return d < 0 ? d + 360 : d;
  }
}
