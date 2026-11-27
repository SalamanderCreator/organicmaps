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
import app.organicmaps.sdk.maplayer.flight.FlightSite;
import app.organicmaps.util.Utils;
import com.google.android.material.bottomsheet.BottomSheetDialog;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Maps&Fly: the information of a takeoff (from its weather window) or of a tapped landing: height,
 * directions, rules, access, description and the links to its sources.
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
    if (site.mGuestRules)
      facts.add(context.getString(R.string.flight_site_guest_rules));
    addDetail(context, facts, R.string.flight_site_height_difference, site, FlightSite.DETAIL_HEIGHT_DIFFERENCE);
    addDetail(context, facts, R.string.flight_site_suitability, site, FlightSite.DETAIL_SUITABILITY);
    addText(sections, String.join("\n", facts));

    addSection(sections, R.string.flight_site_rules, site.mRules);
    addSection(sections, R.string.flight_site_access, access(context, site));
    addSection(sections, R.string.flight_site_description, site.mDescription);
    addSection(sections, R.string.flight_site_comments, site.mComments);
    addSection(sections, R.string.flight_site_cable_car, site.mDetails.get(FlightSite.DETAIL_CABLE_CAR));
    addSection(sections, R.string.flight_site_other_takeoffs, site.mDetails.get(FlightSite.DETAIL_OTHER_TAKEOFFS));
    addSection(sections, R.string.flight_site_info, site.mDetails.get(FlightSite.DETAIL_INFO));
    addSection(sections, R.string.flight_site_weather_info, site.mDetails.get(FlightSite.DETAIL_WEATHER));
    addSection(sections, R.string.flight_site_webcams, site.mDetails.get(FlightSite.DETAIL_WEBCAMS));
    addSection(sections, R.string.flight_site_holder, site.mDetails.get(FlightSite.DETAIL_HOLDER));
    addSection(sections, R.string.flight_site_contact, site.mDetails.get(FlightSite.DETAIL_CONTACT));

    if (site.mUrl != null && (site.mSources & FlightSite.SOURCE_PGE) != 0)
      addLink(sections, context.getString(R.string.flight_site_link_pge), v -> Utils.openUrl(context, site.mUrl));
    if (site.mUrl != null && (site.mSources & FlightSite.SOURCE_DHV) != 0)
      addLink(sections, context.getString(R.string.flight_site_link_dhv), v -> Utils.openUrl(context, site.mUrl));
    if (site.mOsmUrl != null)
      addLink(sections, context.getString(R.string.flight_site_link_osm), v -> Utils.openUrl(context, site.mOsmUrl));
    if (site.mWebsite != null)
      addLink(sections, context.getString(R.string.flight_site_link_web), v -> Utils.openUrl(context, site.mWebsite));

    final List<String> credits = new ArrayList<>();
    if ((site.mSources & FlightSite.SOURCE_PGE) != 0)
      credits.add(context.getString(R.string.flight_attribution_pge));
    if ((site.mSources & FlightSite.SOURCE_OSM) != 0)
      credits.add(context.getString(R.string.flight_attribution_osm));
    if ((site.mSources & FlightSite.SOURCE_DHV) != 0)
      credits.add(context.getString(R.string.flight_attribution_dhv));
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
    if ((site.mSources & FlightSite.SOURCE_DHV) != 0)
      names.add("DHV");
    return String.join(", ", names);
  }

  private static void addDetail(@NonNull Context context, @NonNull List<String> facts, @StringRes int label,
                                @NonNull FlightSite site, @NonNull String key)
  {
    final String value = site.mDetails.get(key);
    if (value != null && !value.isEmpty())
      facts.add(context.getString(label, value));
  }

  /** How to get there, like "By car, on foot", and the remarks of the source. */
  @NonNull
  private static String access(@NonNull Context context, @NonNull FlightSite site)
  {
    final List<String> ways = new ArrayList<>();
    final String accessBy = site.mDetails.get(FlightSite.DETAIL_ACCESS_BY);
    if (accessBy != null)
    {
      for (String way : accessBy.split(","))
      {
        switch (way)
        {
        case FlightSite.ACCESS_CAR -> ways.add(context.getString(R.string.flight_site_access_car));
        case FlightSite.ACCESS_PUBLIC_TRANSPORT -> ways.add(context.getString(R.string.flight_site_access_public));
        case FlightSite.ACCESS_FOOT -> ways.add(context.getString(R.string.flight_site_access_foot));
        default -> {}
        }
      }
    }
    final List<String> lines = new ArrayList<>();
    if (!ways.isEmpty())
      lines.add(String.join(", ", ways));
    if (!site.mAccess.isEmpty())
      lines.add(site.mAccess);
    return String.join("\n", lines);
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
