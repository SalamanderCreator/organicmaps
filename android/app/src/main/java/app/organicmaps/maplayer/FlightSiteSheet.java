package app.organicmaps.maplayer;

import android.content.Context;
import android.graphics.Typeface;
import android.view.LayoutInflater;
import android.view.View;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.core.widget.TextViewCompat;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.maplayer.flight.FlightDirections;
import app.organicmaps.sdk.maplayer.flight.FlightLayer;
import app.organicmaps.sdk.maplayer.flight.FlightSite;
import app.organicmaps.sdk.maplayer.flight.FlightSources;
import app.organicmaps.util.Utils;
import com.google.android.material.bottomsheet.BottomSheetDialog;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Maps&Fly: a tapped takeoff with its height, directions, landings, rules and access, the nearest aerial
 * lift and parking and the links to its sources; or a tapped landing with its description, parking and the
 * takeoffs it belongs to.
 */
public final class FlightSiteSheet
{
  private FlightSiteSheet() {}

  public static void show(@NonNull Context context, @NonNull FlightSite site)
  {
    final BottomSheetDialog dialog = new BottomSheetDialog(context);
    final View view = LayoutInflater.from(context).inflate(R.layout.flight_site_sheet, null);
    dialog.setContentView(view);

    @StringRes
    final int type = site.mLanding ? R.string.flight_site_landing : R.string.flight_site_takeoff;
    ((TextView) view.findViewById(R.id.flight_site_name)).setText(site.mName.isEmpty() ? context.getString(type)
                                                                                       : site.mName);
    ((TextView) view.findViewById(R.id.flight_site_type))
        .setText(context.getString(type) + " · " + context.getString(R.string.flight_site_source, sources(site)));

    final LinearLayout sections = view.findViewById(R.id.flight_site_sections);
    final List<String> facts = new ArrayList<>();
    if (site.hasAltitude())
    {
      final String altitude = Framework.nativeFormatAltitude(site.mAltitude);
      facts.add(site.mAltitudeFromTerrain ? context.getString(R.string.flight_site_altitude_terrain, altitude)
                                          : context.getString(R.string.flight_site_altitude, altitude));
    }
    if (!site.mLanding)
    {
      facts.add(site.mDirections == 0
                    ? context.getString(R.string.flight_site_directions_unknown)
                    : context.getString(R.string.flight_site_directions, directions(site.mDirections)));
    }
    if (site.mArea.length > 0)
      facts.add(context.getString(R.string.flight_site_area_known));
    if (site.mRestricted)
      facts.add(context.getString(R.string.flight_site_restricted));
    addText(sections, String.join("\n", facts));

    // The weather of a takeoff in a window over the map.
    if (!site.mLanding)
    {
      final com.google.android.material.button.MaterialButton weather =
          new com.google.android.material.button.MaterialButton(context);
      weather.setText(R.string.flight_site_weather);
      weather.setOnClickListener(v -> FlightWeatherSheet.show(context, site));
      final LinearLayout.LayoutParams params =
          new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT);
      params.topMargin = context.getResources().getDimensionPixelSize(R.dimen.margin_half);
      sections.addView(weather, params);
    }

    // The landings of a takeoff or the takeoffs of a landing.
    final List<FlightSources.Related> related = FlightLayer.getRelated(site);
    if (!related.isEmpty())
    {
      addHeader(sections, site.mLanding ? R.string.flight_site_takeoffs : R.string.flight_site_landings);
      for (FlightSources.Related r : related)
      {
        String text = (r.mSite.mName.isEmpty() ? context.getString(R.string.flight_site_unnamed) : r.mSite.mName)
                    + " · " + distance(r.mDistanceM);
        if (r.mSite.hasAltitude())
          text += " · " + Framework.nativeFormatAltitude(r.mSite.mAltitude);
        addLink(sections, text, v -> {
          dialog.dismiss();
          show(context, r.mSite);
        });
      }
    }

    addSection(sections, R.string.flight_site_rules, site.mRules);
    addSection(sections, R.string.flight_site_access, site.mAccess);
    addSection(sections, R.string.flight_site_description, site.mDescription);
    addSection(sections, R.string.flight_site_comments, site.mComments);

    final List<String> nearby = new ArrayList<>();
    for (FlightLayer.Service service : FlightLayer.findServices(site))
    {
      // A landing needs only the parking.
      if (site.mLanding && service.mCableCar)
        continue;
      final String name = service.mName.isEmpty() ? context.getString(R.string.flight_site_unnamed) : service.mName;
      nearby.add(context.getString(service.mCableCar ? R.string.flight_site_cable_car : R.string.flight_site_parking,
                                   name, distance(service.mDistanceM)));
    }
    if (!nearby.isEmpty())
    {
      addHeader(sections, R.string.flight_site_nearby);
      addText(sections, String.join("\n", nearby));
    }

    if (site.mUrl != null && (site.mSources & FlightSite.SOURCE_PGE) != 0)
      addLink(sections, context.getString(R.string.flight_site_link_pge), v -> Utils.openUrl(context, site.mUrl));
    if (site.mOsmUrl != null)
      addLink(sections, context.getString(R.string.flight_site_link_osm), v -> Utils.openUrl(context, site.mOsmUrl));
    if (site.mWebsite != null)
      addLink(sections, context.getString(R.string.flight_site_link_web), v -> Utils.openUrl(context, site.mWebsite));

    final List<String> credits = new ArrayList<>();
    if ((site.mSources & FlightSite.SOURCE_PGE) != 0)
      credits.add(context.getString(R.string.flight_attribution_pge));
    if ((site.mSources & FlightSite.SOURCE_OSM) != 0)
      credits.add(context.getString(R.string.flight_attribution_osm));
    ((TextView) view.findViewById(R.id.flight_attribution)).setText(String.join(" ", credits));

    dialog.show();
  }

  /** Directions with the German O for east in German, as the DHV and the weather page write them. */
  @NonNull
  static String directions(int mask)
  {
    final String names = FlightDirections.format(mask);
    return "de".equals(Locale.getDefault().getLanguage()) ? names.replace('E', 'O') : names;
  }

  @NonNull
  private static String sources(@NonNull FlightSite site)
  {
    final List<String> names = new ArrayList<>();
    if ((site.mSources & FlightSite.SOURCE_PGE) != 0)
      names.add("ParaglidingEarth");
    if ((site.mSources & FlightSite.SOURCE_OSM) != 0)
      names.add("OpenStreetMap");
    if ((site.mSources & FlightSite.SOURCE_TEST) != 0)
      names.add("Test");
    return String.join(", ", names);
  }

  @NonNull
  private static String distance(double meters)
  {
    if (meters < 1000)
      return String.format(Locale.getDefault(), "%d m", Math.round(meters / 10) * 10);
    return String.format(Locale.getDefault(), "%.1f km", meters / 1000);
  }

  private static void addSection(@NonNull LinearLayout parent, @StringRes int title, @Nullable String text)
  {
    if (text == null || text.isEmpty())
      return;
    addHeader(parent, title);
    addText(parent, text);
  }

  private static void addHeader(@NonNull LinearLayout parent, @StringRes int title)
  {
    final Context context = parent.getContext();
    final TextView header = new TextView(context);
    TextViewCompat.setTextAppearance(header, R.style.MwmTextAppearance_Body2);
    header.setTypeface(header.getTypeface(), Typeface.BOLD);
    header.setPadding(0, context.getResources().getDimensionPixelSize(R.dimen.margin_base), 0, 0);
    header.setText(title);
    parent.addView(header);
  }

  private static void addText(@NonNull LinearLayout parent, @NonNull String text)
  {
    if (text.isEmpty())
      return;
    final Context context = parent.getContext();
    final TextView view = new TextView(context);
    TextViewCompat.setTextAppearance(view, R.style.MwmTextAppearance_Body3);
    view.setPadding(0, context.getResources().getDimensionPixelSize(R.dimen.margin_quarter), 0, 0);
    view.setTextIsSelectable(true);
    view.setText(text);
    parent.addView(view);
  }

  private static void addLink(@NonNull LinearLayout parent, @NonNull String text, @NonNull View.OnClickListener onClick)
  {
    final Context context = parent.getContext();
    final TextView link = new TextView(context);
    TextViewCompat.setTextAppearance(link, R.style.MwmTextAppearance_Body2);
    final int pad = context.getResources().getDimensionPixelSize(R.dimen.margin_half);
    link.setPadding(0, pad, 0, pad);
    final android.util.TypedValue value = new android.util.TypedValue();
    context.getTheme().resolveAttribute(androidx.appcompat.R.attr.colorAccent, value, true);
    link.setTextColor(value.data);
    context.getTheme().resolveAttribute(android.R.attr.selectableItemBackground, value, true);
    link.setBackgroundResource(value.resourceId);
    link.setText(text);
    link.setOnClickListener(onClick);
    parent.addView(link);
  }
}
