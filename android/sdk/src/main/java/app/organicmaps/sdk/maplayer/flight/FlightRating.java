package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import java.util.ArrayList;
import java.util.List;

/**
 * Maps&Fly: a rough rating of one forecast hour at a takeoff for paragliding. Only a hint for the flight
 * preparation, the pilot decides on site.
 */
public final class FlightRating
{
  public enum Level
  {
    GOOD,
    MARGINAL,
    BAD
  }

  public enum Reason
  {
    CALM,
    DIRECTION_FITS,
    DIRECTION_UNKNOWN,
    CROSSWIND,
    WRONG_DIRECTION,
    TAILWIND,
    WIND_STRONG,
    WIND_TOO_STRONG,
    GUSTY,
    GUSTS_TOO_STRONG,
    UPPER_WIND_STRONG,
    UPPER_WIND_TOO_STRONG,
    RAIN_LIKELY,
    RAIN,
    THUNDERSTORM_RISK
  }

  // km/h at 10 m above ground.
  static final double CALM_KMH = 5;
  static final double WIND_MARGINAL_KMH = 20;
  static final double WIND_BAD_KMH = 28;
  static final double GUST_MARGINAL_KMH = 28;
  static final double GUST_BAD_KMH = 35;
  static final double GUST_SPREAD_KMH = 15;
  // km/h at 850 hPa, about 1500 m.
  static final double UPPER_MARGINAL_KMH = 35;
  static final double UPPER_BAD_KMH = 45;
  static final double RAIN_MARGINAL_MM = 0.1;
  static final double RAIN_BAD_MM = 0.5;
  static final int RAIN_LIKELY_PCT = 60;
  static final double CAPE_STORM = 1000;

  @NonNull
  public final Level mLevel;
  @NonNull
  public final List<Reason> mReasons;

  private FlightRating(@NonNull Level level, @NonNull List<Reason> reasons)
  {
    mLevel = level;
    mReasons = reasons;
  }

  /** NaN stands for an unknown value, which does not change the rating. */
  @NonNull
  public static FlightRating rate(int directions, double windFromDeg, double windKmh, double gustKmh,
                                  double upperWindKmh, double rainMm, int rainPct, double cape)
  {
    final List<Reason> reasons = new ArrayList<>();
    Level level = Level.GOOD;

    if (windKmh < CALM_KMH)
    {
      reasons.add(Reason.CALM);
    }
    else
    {
      switch (FlightDirections.fit(directions, windFromDeg))
      {
      case UNKNOWN -> reasons.add(Reason.DIRECTION_UNKNOWN);
      case FITS -> reasons.add(Reason.DIRECTION_FITS);
      case CROSS ->
      {
        reasons.add(Reason.CROSSWIND);
        level = worse(level, Level.MARGINAL);
      }
      case WRONG ->
      {
        reasons.add(Reason.WRONG_DIRECTION);
        level = Level.BAD;
      }
      case TAIL ->
      {
        reasons.add(Reason.TAILWIND);
        level = Level.BAD;
      }
      }
    }

    if (windKmh > WIND_BAD_KMH)
    {
      reasons.add(Reason.WIND_TOO_STRONG);
      level = Level.BAD;
    }
    else if (windKmh > WIND_MARGINAL_KMH)
    {
      reasons.add(Reason.WIND_STRONG);
      level = worse(level, Level.MARGINAL);
    }

    if (gustKmh > GUST_BAD_KMH)
    {
      reasons.add(Reason.GUSTS_TOO_STRONG);
      level = Level.BAD;
    }
    else if (gustKmh > GUST_MARGINAL_KMH || gustKmh - windKmh >= GUST_SPREAD_KMH)
    {
      reasons.add(Reason.GUSTY);
      level = worse(level, Level.MARGINAL);
    }

    if (upperWindKmh > UPPER_BAD_KMH)
    {
      reasons.add(Reason.UPPER_WIND_TOO_STRONG);
      level = Level.BAD;
    }
    else if (upperWindKmh > UPPER_MARGINAL_KMH)
    {
      reasons.add(Reason.UPPER_WIND_STRONG);
      level = worse(level, Level.MARGINAL);
    }

    if (rainMm >= RAIN_BAD_MM)
    {
      reasons.add(Reason.RAIN);
      level = Level.BAD;
    }
    else if (rainMm >= RAIN_MARGINAL_MM || rainPct >= RAIN_LIKELY_PCT)
    {
      reasons.add(Reason.RAIN_LIKELY);
      level = worse(level, Level.MARGINAL);
    }

    if (cape >= CAPE_STORM)
    {
      reasons.add(Reason.THUNDERSTORM_RISK);
      level = worse(level, Level.MARGINAL);
    }

    return new FlightRating(level, reasons);
  }

  @NonNull
  private static Level worse(@NonNull Level a, @NonNull Level b)
  {
    return a.ordinal() >= b.ordinal() ? a : b;
  }
}
