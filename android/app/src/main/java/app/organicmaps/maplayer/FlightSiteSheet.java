package app.organicmaps.maplayer;

import android.content.Context;
import android.graphics.drawable.GradientDrawable;
import android.view.Gravity;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.StringRes;
import androidx.core.content.ContextCompat;
import androidx.core.widget.TextViewCompat;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.maplayer.flight.FlightDirections;
import app.organicmaps.sdk.maplayer.flight.FlightRating;
import app.organicmaps.sdk.maplayer.flight.FlightSite;
import app.organicmaps.sdk.maplayer.flight.FlightWeather;
import app.organicmaps.util.UiUtils;
import app.organicmaps.util.Utils;
import com.google.android.material.bottomsheet.BottomSheetDialog;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Date;
import java.util.List;
import java.util.Locale;

/**
 * Maps&Fly: name, height, takeoff directions, type and source of a tapped flight site, and its forecast
 * for the next days with a rating of each hour.
 */
public final class FlightSiteSheet
{
  // Hours outside are rarely flown.
  private static final int FIRST_HOUR = 7;
  private static final int LAST_HOUR = 20;

  private FlightSiteSheet() {}

  public static void show(@NonNull Context context, @NonNull FlightSite site)
  {
    final BottomSheetDialog dialog = new BottomSheetDialog(context);
    final View view = LayoutInflater.from(context).inflate(R.layout.flight_site_sheet, null);
    dialog.setContentView(view);

    final TextView name = view.findViewById(R.id.flight_site_name);
    name.setText(site.mName.isEmpty() ? context.getString(site.mLanding ? R.string.flight_site_landing
                                                                        : R.string.flight_site_takeoff)
                                      : site.mName);
    ((TextView) view.findViewById(R.id.flight_site_type))
        .setText(context.getString(site.mLanding ? R.string.flight_site_landing : R.string.flight_site_takeoff) + " · "
                 + context.getString(R.string.flight_site_source, sources(site.mSources)));

    final List<String> details = new ArrayList<>();
    if (site.hasAltitude())
    {
      final String altitude = Framework.nativeFormatAltitude(site.mAltitude);
      details.add(site.mAltitudeFromTerrain ? context.getString(R.string.flight_site_altitude_terrain, altitude)
                                            : context.getString(R.string.flight_site_altitude, altitude));
    }
    if (!site.mLanding)
    {
      details.add(site.mDirections == 0
                      ? context.getString(R.string.flight_site_directions_unknown)
                      : context.getString(R.string.flight_site_directions, FlightDirections.format(site.mDirections)));
    }
    if (site.mArea.length > 0)
      details.add(context.getString(R.string.flight_site_area_known));
    ((TextView) view.findViewById(R.id.flight_site_details)).setText(String.join("\n", details));

    final TextView link = view.findViewById(R.id.flight_site_link);
    UiUtils.showIf(site.mUrl != null, link);
    link.setOnClickListener(v -> Utils.openUrl(context, site.mUrl));

    final StringBuilder attribution = new StringBuilder();
    if ((site.mSources & FlightSite.SOURCE_PGE) != 0)
      attribution.append(context.getString(R.string.flight_attribution_pge)).append(" ");
    if ((site.mSources & FlightSite.SOURCE_OSM) != 0)
      attribution.append(context.getString(R.string.flight_attribution_osm)).append(" ");
    attribution.append(context.getString(R.string.flight_weather_attribution));
    ((TextView) view.findViewById(R.id.flight_attribution)).setText(attribution);

    final View progress = view.findViewById(R.id.flight_weather_progress);
    final View status = view.findViewById(R.id.flight_weather_status);
    final LinearLayout rows = view.findViewById(R.id.flight_weather_rows);
    FlightWeather.load(site, hours -> {
      if (!dialog.isShowing())
        return;
      UiUtils.hide(progress);
      if (hours == null)
      {
        UiUtils.show(status);
        return;
      }
      fillRows(context, rows, hours);
    });

    dialog.show();
  }

  @NonNull
  private static String sources(int sources)
  {
    final List<String> names = new ArrayList<>();
    if ((sources & FlightSite.SOURCE_PGE) != 0)
      names.add("ParaglidingEarth");
    if ((sources & FlightSite.SOURCE_OSM) != 0)
      names.add("OpenStreetMap");
    if ((sources & FlightSite.SOURCE_TEST) != 0)
      names.add("Test");
    return String.join(", ", names);
  }

  private static void fillRows(@NonNull Context context, @NonNull LinearLayout rows,
                               @NonNull List<FlightWeather.Hour> hours)
  {
    final int pad = context.getResources().getDimensionPixelSize(R.dimen.margin_half);
    String date = null;
    for (FlightWeather.Hour hour : hours)
    {
      final int clock = hourOf(hour);
      if (clock < FIRST_HOUR || clock > LAST_HOUR)
        continue;
      if (!hour.getDate().equals(date))
      {
        date = hour.getDate();
        final TextView header = new TextView(context);
        TextViewCompat.setTextAppearance(header, R.style.MwmTextAppearance_Body2);
        header.setTypeface(header.getTypeface(), android.graphics.Typeface.BOLD);
        header.setPadding(0, pad * 2, 0, pad / 2);
        header.setText(formatDate(date));
        rows.addView(header);
      }
      rows.addView(makeRow(context, hour, pad));
    }
  }

  @NonNull
  private static View makeRow(@NonNull Context context, @NonNull FlightWeather.Hour hour, int pad)
  {
    final LinearLayout row = new LinearLayout(context);
    row.setOrientation(LinearLayout.HORIZONTAL);
    row.setGravity(Gravity.TOP);
    row.setPadding(0, pad / 2, 0, pad / 2);

    final View dot = new View(context);
    final GradientDrawable circle = new GradientDrawable();
    circle.setShape(GradientDrawable.OVAL);
    circle.setColor(ContextCompat.getColor(context, colorOf(hour.mRating.mLevel)));
    dot.setBackground(circle);
    final int size = pad * 3 / 2;
    final LinearLayout.LayoutParams dotParams = new LinearLayout.LayoutParams(size, size);
    dotParams.setMargins(0, pad / 2, pad, 0);
    row.addView(dot, dotParams);

    final List<String> lines = new ArrayList<>();
    String first = hour.getClock() + "  ";
    if (!Double.isNaN(hour.mWindKmh))
    {
      first += context.getString(R.string.flight_weather_wind, FlightDirections.name(hour.mWindFromDeg),
                                 Math.round(hour.mWindKmh), Math.round(hour.mGustKmh));
    }
    if (!Double.isNaN(hour.mRainMm))
    {
      final String rain = String.format(Locale.getDefault(), "%.1f", hour.mRainMm);
      first += " · " + context.getString(R.string.flight_weather_rain, rain);
    }
    lines.add(first);

    final List<String> second = new ArrayList<>();
    final int thermal = FlightWeather.thermalStrength(hour);
    if (thermal >= 0)
    {
      @StringRes
      final int strength = thermal == 0   ? R.string.flight_weather_thermal_weak
                         : thermal == 1 ? R.string.flight_weather_thermal_moderate
                                        : R.string.flight_weather_thermal_good;
      String text = context.getString(strength);
      if (!Double.isNaN(hour.mThermalTopM))
        text += " " + context.getString(R.string.flight_weather_thermal_top,
                                        Framework.nativeFormatAltitude(hour.mThermalTopM));
      second.add(text);
    }
    if (!Double.isNaN(hour.mCloudBaseM))
      second.add(
          context.getString(R.string.flight_weather_cloud_base, Framework.nativeFormatAltitude(hour.mCloudBaseM)));
    if (!Double.isNaN(hour.mUpperWindKmh))
    {
      second.add(context.getString(R.string.flight_weather_upper_wind, FlightDirections.name(hour.mUpperWindFromDeg),
                                   Math.round(hour.mUpperWindKmh)));
    }
    if (!second.isEmpty())
      lines.add(String.join(" · ", second));

    final List<String> reasons = new ArrayList<>();
    for (FlightRating.Reason reason : hour.mRating.mReasons)
      reasons.add(context.getString(reasonText(reason)));
    if (!reasons.isEmpty())
      lines.add(String.join(", ", reasons));

    final TextView text = new TextView(context);
    TextViewCompat.setTextAppearance(text, R.style.MwmTextAppearance_Body3);
    text.setText(String.join("\n", lines));
    row.addView(text, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1));
    return row;
  }

  private static int hourOf(@NonNull FlightWeather.Hour hour)
  {
    final String clock = hour.getClock();
    try
    {
      return Integer.parseInt(clock.substring(0, Math.min(2, clock.length())));
    }
    catch (NumberFormatException e)
    {
      return -1;
    }
  }

  @NonNull
  private static String formatDate(@NonNull String isoDate)
  {
    try
    {
      final Date date = new SimpleDateFormat("yyyy-MM-dd", Locale.US).parse(isoDate);
      if (date != null)
        return new SimpleDateFormat("EEEE, d. MMMM", Locale.getDefault()).format(date);
    }
    catch (java.text.ParseException ignored)
    {}
    return isoDate;
  }

  private static int colorOf(@NonNull FlightRating.Level level)
  {
    return switch (level)
    {
      case GOOD -> R.color.flight_rating_good;
      case MARGINAL -> R.color.flight_rating_marginal;
      case BAD -> R.color.flight_rating_bad;
    };
  }

  @StringRes
  private static int reasonText(@NonNull FlightRating.Reason reason)
  {
    return switch (reason)
    {
      case CALM -> R.string.flight_reason_calm;
      case DIRECTION_FITS -> R.string.flight_reason_direction_fits;
      case DIRECTION_UNKNOWN -> R.string.flight_reason_direction_unknown;
      case CROSSWIND -> R.string.flight_reason_crosswind;
      case WRONG_DIRECTION -> R.string.flight_reason_wrong_direction;
      case TAILWIND -> R.string.flight_reason_tailwind;
      case WIND_STRONG -> R.string.flight_reason_wind_strong;
      case WIND_TOO_STRONG -> R.string.flight_reason_wind_too_strong;
      case GUSTY -> R.string.flight_reason_gusty;
      case GUSTS_TOO_STRONG -> R.string.flight_reason_gusts_too_strong;
      case UPPER_WIND_STRONG -> R.string.flight_reason_upper_wind_strong;
      case UPPER_WIND_TOO_STRONG -> R.string.flight_reason_upper_wind_too_strong;
      case RAIN_LIKELY -> R.string.flight_reason_rain_likely;
      case RAIN -> R.string.flight_reason_rain;
      case THUNDERSTORM_RISK -> R.string.flight_reason_thunderstorm;
    };
  }
}
