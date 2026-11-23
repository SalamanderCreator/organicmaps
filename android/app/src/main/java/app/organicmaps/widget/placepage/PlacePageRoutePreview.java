package app.organicmaps.widget.placepage;

import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.location.Location;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.style.ForegroundColorSpan;
import android.view.View;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import app.organicmaps.MwmApplication;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.Router;
import app.organicmaps.sdk.bookmarks.data.MapObject;
import app.organicmaps.sdk.routing.RoutePreview;
import app.organicmaps.sdk.routing.RoutingController;
import app.organicmaps.sdk.util.StringUtils;
import app.organicmaps.util.UiUtils;

/**
 * Maps&amp;Fly: shows in the place page how far the place is along the paths from the current position,
 * with the climb and the walking or cycling time. A tap switches between walking and cycling.
 * While a walking or cycling route is planned or followed, it offers two ways to add the place instead: inserted where
 * the route gets shortest, or as a side trip that keeps the route. Both are drawn on the map in their colors.
 */
final class PlacePageRoutePreview
{
  private static final String PREF_ROUTER = "RoutePreviewRouter";
  // Routes this long take the router too long on a phone.
  private static final float MAX_STRAIGHT_DISTANCE_M = 150_000;
  // A new position this far away from the last start calculates the route again.
  private static final float RECALCULATE_DISTANCE_M = 100;
  // The route colors TrafficG4 and RoadShieldOrangeBackground of the light map styles.
  private static final int OPTIMAL_COLOR = Color.rgb(0x39, 0x96, 0x2E);
  private static final int DETOUR_COLOR = Color.rgb(0xFF, 0xBE, 0x00);

  @NonNull
  private final TextView mPreviewView;
  @NonNull
  private final TextView mOptimalView;
  @NonNull
  private final TextView mDetourView;
  @Nullable
  private MapObject mObject;
  @Nullable
  private Location mFrom;
  private boolean mShowsVariants;

  PlacePageRoutePreview(@NonNull View root)
  {
    mPreviewView = root.findViewById(R.id.tv__route_preview);
    mOptimalView = root.findViewById(R.id.tv__route_insert_optimal);
    mDetourView = root.findViewById(R.id.tv__route_insert_detour);
    mPreviewView.setOnClickListener(v -> toggleRouter());
    mOptimalView.setOnClickListener(v -> addOptimal());
    mDetourView.setOnClickListener(v -> addDetour());
    hideAll();
  }

  /** Call when the place or the position changes; calculates only when needed. */
  void update(@Nullable MapObject object, @Nullable Location location)
  {
    if (object == null || !isSupported(object))
    {
      clear();
      return;
    }
    if (canInsertIntoRoute())
      updateVariants(object);
    else
      updatePreview(object, location);
  }

  /** Stops the calculation and hides the drawn routes, e.g. when the place page closes. */
  void clear()
  {
    if (mObject != null)
      RoutePreview.cancel();
    mObject = null;
    mFrom = null;
    mShowsVariants = false;
    hideAll();
  }

  private void updatePreview(@NonNull MapObject object, @Nullable Location location)
  {
    if (location == null)
    {
      clear();
      return;
    }
    final boolean samePlace = !mShowsVariants && object.sameAs(mObject);
    if (samePlace && mFrom != null && mFrom.distanceTo(location) < RECALCULATE_DISTANCE_M)
      return;

    final float[] straight = new float[1];
    Location.distanceBetween(location.getLatitude(), location.getLongitude(), object.getLat(), object.getLon(),
                             straight);
    if (straight[0] > MAX_STRAIGHT_DISTANCE_M)
    {
      clear();
      return;
    }

    hideAll();
    mObject = object;
    mFrom = location;
    mShowsVariants = false;
    final Router router = getRouter();
    // Keep the old numbers while the position moved; a new place starts empty.
    if (!samePlace)
      mPreviewView.setText(getIcon(router) + " …");
    UiUtils.show(mPreviewView);
    RoutePreview.calculate(location.getLatitude(), location.getLongitude(), object.getLat(), object.getLon(), router,
                           result -> showPreview(router, result));
  }

  private void updateVariants(@NonNull MapObject object)
  {
    if (mShowsVariants && object.sameAs(mObject))
      return;

    hideAll();
    if (!RoutePreview.calculateInsertVariants(object.getLat(), object.getLon(), this::showVariants))
    {
      clear();
      return;
    }
    mObject = object;
    mFrom = null;
    mShowsVariants = true;
    mOptimalView.setText(makeVariantTitle(OPTIMAL_COLOR, R.string.route_insert_optimal).append("  …"));
    mDetourView.setText(makeVariantTitle(DETOUR_COLOR, R.string.route_insert_detour).append("  …"));
    UiUtils.show(mOptimalView, mDetourView);
  }

  private static boolean isSupported(@NonNull MapObject object)
  {
    return !object.isMyPosition() && !object.isTrack() && !object.isTrackRecording();
  }

  private static boolean canInsertIntoRoute()
  {
    final RoutingController controller = RoutingController.get();
    final Router router = controller.getLastRouterType();
    final boolean hasRoute = controller.isNavigating() || (controller.isPlanning() && controller.isBuilt());
    return hasRoute && (router == Router.Pedestrian || router == Router.Bicycle);
  }

  private void showPreview(@NonNull Router router, @Nullable RoutePreview.Result result)
  {
    if (result == null)
    {
      UiUtils.hide(mPreviewView);
      return;
    }
    final StringBuilder text =
        new StringBuilder(getIcon(router)).append(' ').append(formatDistance(result.distanceM()));
    if (result.hasAltitudes())
    {
      text.append("  ↗ ")
          .append(Framework.nativeFormatAltitude(result.ascentM()))
          .append("  ↘ ")
          .append(Framework.nativeFormatAltitude(result.descentM()));
    }
    text.append("  ⏱ ").append(StringUtils.nativeFormatRoutingDuration((int) Math.round(result.timeSec())));
    mPreviewView.setText(text);
    UiUtils.show(mPreviewView);
  }

  private void showVariants(@NonNull RoutePreview.Result current, @Nullable RoutePreview.Result optimal,
                            @Nullable RoutePreview.Result detour)
  {
    showVariant(mOptimalView, OPTIMAL_COLOR, R.string.route_insert_optimal, current, optimal);
    showVariant(mDetourView, DETOUR_COLOR, R.string.route_insert_detour, current, detour);
  }

  private void showVariant(@NonNull TextView view, int color, @StringRes int title,
                           @NonNull RoutePreview.Result current, @Nullable RoutePreview.Result variant)
  {
    if (variant == null)
    {
      UiUtils.hide(view);
      return;
    }
    final SpannableStringBuilder text = makeVariantTitle(color, title);
    text.append("  ").append(formatDelta(formatDistance(Math.abs(variant.distanceM() - current.distanceM())),
                                         variant.distanceM() - current.distanceM()));
    if (variant.hasAltitudes() && current.hasAltitudes())
    {
      final int ascent = variant.ascentM() - current.ascentM();
      final int descent = variant.descentM() - current.descentM();
      text.append("  ↗ ")
          .append(formatDelta(Framework.nativeFormatAltitude(Math.abs(ascent)), ascent))
          .append("  ↘ ")
          .append(formatDelta(Framework.nativeFormatAltitude(Math.abs(descent)), descent));
    }
    final double time = variant.timeSec() - current.timeSec();
    text.append("  ⏱ ").append(
        formatDelta(StringUtils.nativeFormatRoutingDuration((int) Math.round(Math.abs(time))), time));
    view.setText(text);
    UiUtils.show(view);
  }

  @NonNull
  private SpannableStringBuilder makeVariantTitle(int color, @StringRes int title)
  {
    final SpannableStringBuilder text = new SpannableStringBuilder("●");
    text.setSpan(new ForegroundColorSpan(color), 0, 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
    return text.append(' ').append(getContext().getString(title));
  }

  @NonNull
  private static String formatDelta(@NonNull String absolute, double delta)
  {
    return (delta < 0 ? "−" : "+") + absolute;
  }

  @NonNull
  private String formatDistance(double meters)
  {
    return StringUtils.nativeFormatDistance(meters).toString(getContext());
  }

  private void addOptimal()
  {
    final MapObject object = mObject;
    if (object == null)
      return;
    clear();
    // A new stop is inserted at the same place as in the preview.
    RoutingController.get().addStop(object);
  }

  private void addDetour()
  {
    final MapObject object = mObject;
    if (object == null)
      return;
    // The detour is kept until the next calculation, so add it before clear() hides it.
    RoutingController.get().addDetourStop(object, getContext().getString(R.string.route_detour_entry));
    clear();
  }

  private void toggleRouter()
  {
    final Router next = getRouter() == Router.Bicycle ? Router.Pedestrian : Router.Bicycle;
    prefs().edit().putInt(PREF_ROUTER, next.getType()).apply();
    final MapObject object = mObject;
    final Location from = mFrom;
    mObject = null;
    update(object, from);
  }

  private void hideAll()
  {
    UiUtils.hide(mPreviewView, mOptimalView, mDetourView);
  }

  @NonNull
  private Router getRouter()
  {
    final int saved = prefs().getInt(PREF_ROUTER, -1);
    if (saved == Router.Bicycle.getType() || saved == Router.Pedestrian.getType())
      return Router.valueOf(saved);
    return Router.getLastUsed() == Router.Bicycle ? Router.Bicycle : Router.Pedestrian;
  }

  @NonNull
  private static String getIcon(@NonNull Router router)
  {
    return router == Router.Bicycle ? "🚲" : "🥾";
  }

  @NonNull
  private Context getContext()
  {
    return mPreviewView.getContext();
  }

  @NonNull
  private SharedPreferences prefs()
  {
    return MwmApplication.prefs(getContext());
  }
}
