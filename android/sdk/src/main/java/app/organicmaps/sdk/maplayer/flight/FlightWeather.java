package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import app.organicmaps.sdk.util.concurrency.ThreadPool;
import app.organicmaps.sdk.util.concurrency.UiThread;
import app.organicmaps.sdk.util.log.Logger;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.function.Consumer;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Maps&Fly: the hourly forecast of the next days at a flight site from Open-Meteo (free for non-commercial
 * use, no key, attribution "Weather data by Open-Meteo.com", CC BY 4.0).
 */
public final class FlightWeather
{
  private static final String TAG = FlightWeather.class.getSimpleName();
  private static final String URL = "https://api.open-meteo.com/v1/forecast?latitude=%.5f&longitude=%.5f%s"
                                  + "&hourly=temperature_2m,dew_point_2m,precipitation,precipitation_probability,"
                                  + "wind_speed_10m,wind_direction_10m,wind_gusts_10m,wind_speed_850hPa,"
                                  + "wind_direction_850hPa,cape,boundary_layer_height"
                                  + "&wind_speed_unit=kmh&timezone=auto&forecast_days=3";
  // The cloud base rises about 125 m per degree of spread between temperature and dew point.
  private static final double CLOUD_BASE_M_PER_K = 125;

  public static final class Hour
  {
    /** Local time like "2026-11-26T14:00". */
    @NonNull
    public final String mTime;
    public final double mWindFromDeg;
    public final double mWindKmh;
    public final double mGustKmh;
    public final double mUpperWindFromDeg;
    public final double mUpperWindKmh;
    public final double mRainMm;
    /** -1 if unknown. */
    public final int mRainPct;
    public final double mCape;
    /** Height of the mixed layer above ground, about the height thermals reach, NaN if unknown. */
    public final double mBoundaryLayerM;
    /** Thermals reach about this height above sea level, NaN if unknown. */
    public final double mThermalTopM;
    /** Cumulus base above sea level, NaN if unknown. */
    public final double mCloudBaseM;
    @NonNull
    public final FlightRating mRating;

    Hour(@NonNull String time, double windFromDeg, double windKmh, double gustKmh, double upperWindFromDeg,
         double upperWindKmh, double rainMm, int rainPct, double cape, double boundaryLayerM, double thermalTopM,
         double cloudBaseM, @NonNull FlightRating rating)
    {
      mTime = time;
      mWindFromDeg = windFromDeg;
      mWindKmh = windKmh;
      mGustKmh = gustKmh;
      mUpperWindFromDeg = upperWindFromDeg;
      mUpperWindKmh = upperWindKmh;
      mRainMm = rainMm;
      mRainPct = rainPct;
      mCape = cape;
      mBoundaryLayerM = boundaryLayerM;
      mThermalTopM = thermalTopM;
      mCloudBaseM = cloudBaseM;
      mRating = rating;
    }

    /** "14:00". */
    @NonNull
    public String getClock()
    {
      final int t = mTime.indexOf('T');
      return t >= 0 ? mTime.substring(t + 1) : mTime;
    }

    /** "2026-11-26". */
    @NonNull
    public String getDate()
    {
      final int t = mTime.indexOf('T');
      return t >= 0 ? mTime.substring(0, t) : mTime;
    }
  }

  private FlightWeather() {}

  /** Calls |callback| on the UI thread with the hours or null on failure. */
  public static void load(@NonNull FlightSite site, @NonNull Consumer<List<Hour>> callback)
  {
    ThreadPool.getWorker().execute(() -> {
      List<Hour> hours = null;
      try
      {
        final String elevation = site.hasAltitude() ? String.format(Locale.US, "&elevation=%d", site.mAltitude) : "";
        final String json = FlightLayer.download(String.format(Locale.US, URL, site.mLat, site.mLon, elevation));
        if (json != null)
          hours = parse(json, site.mDirections, site.hasAltitude() ? site.mAltitude : Double.NaN);
      }
      catch (Exception e)
      {
        Logger.w(TAG, "Cannot load the forecast", e);
      }
      final List<Hour> result = hours;
      UiThread.run(() -> callback.accept(result));
    });
  }

  /** |siteAltitude| is NaN if unknown, then the model height of the place is used. */
  @NonNull
  static List<Hour> parse(@NonNull String json, int directions, double siteAltitude) throws Exception
  {
    final JSONObject root = new JSONObject(json);
    final double ground = Double.isNaN(siteAltitude) ? root.optDouble("elevation", Double.NaN) : siteAltitude;
    final JSONObject hourly = root.getJSONObject("hourly");
    final JSONArray times = hourly.getJSONArray("time");
    final List<Hour> hours = new ArrayList<>(times.length());
    for (int i = 0; i < times.length(); ++i)
    {
      final double windFrom = value(hourly, "wind_direction_10m", i);
      final double wind = value(hourly, "wind_speed_10m", i);
      final double gust = value(hourly, "wind_gusts_10m", i);
      final double upperFrom = value(hourly, "wind_direction_850hPa", i);
      final double upper = value(hourly, "wind_speed_850hPa", i);
      final double rain = value(hourly, "precipitation", i);
      final double rainPctValue = value(hourly, "precipitation_probability", i);
      final int rainPct = Double.isNaN(rainPctValue) ? -1 : (int) Math.round(rainPctValue);
      final double cape = value(hourly, "cape", i);
      final double boundaryLayer = value(hourly, "boundary_layer_height", i);
      final double spread = value(hourly, "temperature_2m", i) - value(hourly, "dew_point_2m", i);
      final double thermalTop = ground + boundaryLayer;
      final double cloudBase = ground + Math.max(0, spread) * CLOUD_BASE_M_PER_K;
      final FlightRating rating = FlightRating.rate(directions, windFrom, wind, gust, upper, rain, rainPct, cape);
      hours.add(new Hour(times.getString(i), windFrom, wind, gust, upperFrom, upper, rain, rainPct, cape,
                         boundaryLayer, thermalTop, cloudBase, rating));
    }
    return hours;
  }

  private static double value(@NonNull JSONObject hourly, @NonNull String key, int index)
  {
    final JSONArray values = hourly.optJSONArray(key);
    if (values == null || index >= values.length() || values.isNull(index))
      return Double.NaN;
    return values.optDouble(index, Double.NaN);
  }

  /** 0 weak, 1 moderate or 2 good thermals by the height they reach above ground, -1 if unknown. */
  public static int thermalStrength(@NonNull Hour hour)
  {
    if (Double.isNaN(hour.mBoundaryLayerM))
      return -1;
    if (hour.mBoundaryLayerM < 600)
      return 0;
    if (hour.mBoundaryLayerM < 1200)
      return 1;
    return 2;
  }
}
