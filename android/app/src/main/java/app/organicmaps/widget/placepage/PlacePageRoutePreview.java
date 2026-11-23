package app.organicmaps.widget.placepage;

import android.content.SharedPreferences;
import android.location.Location;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.MwmApplication;
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
 */
final class PlacePageRoutePreview
{
  private static final String PREF_ROUTER = "RoutePreviewRouter";
  // Routes this long take the router too long on a phone.
  private static final float MAX_STRAIGHT_DISTANCE_M = 150_000;
  // A new position this far away from the last start calculates the route again.
  private static final float RECALCULATE_DISTANCE_M = 100;

  @NonNull
  private final TextView mView;
  @Nullable
  private MapObject mObject;
  @Nullable
  private Location mFrom;

  PlacePageRoutePreview(@NonNull TextView view)
  {
    mView = view;
    mView.setOnClickListener(v -> toggleRouter());
    UiUtils.hide(mView);
  }

  /** Call when the place or the position changes; calculates only when needed. */
  void update(@Nullable MapObject object, @Nullable Location location)
  {
    if (object == null || location == null || !isSupported(object))
    {
      clear();
      return;
    }
    if (object.sameAs(mObject) && mFrom != null && mFrom.distanceTo(location) < RECALCULATE_DISTANCE_M)
      return;

    final float[] straight = new float[1];
    Location.distanceBetween(location.getLatitude(), location.getLongitude(), object.getLat(), object.getLon(),
                             straight);
    if (straight[0] > MAX_STRAIGHT_DISTANCE_M)
    {
      clear();
      return;
    }

    // Keep the old numbers while the position moved; a new place starts empty.
    final boolean samePlace = object.sameAs(mObject);
    mObject = object;
    mFrom = location;
    final Router router = getRouter();
    if (!samePlace)
      mView.setText(getIcon(router) + " …");
    UiUtils.show(mView);
    RoutePreview.calculate(location.getLatitude(), location.getLongitude(), object.getLat(), object.getLon(), router,
                           result -> show(router, result));
  }

  /** Stops the calculation, e.g. when the place page closes. */
  void clear()
  {
    if (mObject != null)
      RoutePreview.cancel();
    mObject = null;
    mFrom = null;
    UiUtils.hide(mView);
  }

  private static boolean isSupported(@NonNull MapObject object)
  {
    return !object.isMyPosition() && !object.isTrack() && !object.isTrackRecording()
 && !RoutingController.get().isNavigating();
  }

  private void show(@NonNull Router router, @Nullable RoutePreview.Result result)
  {
    if (result == null)
    {
      UiUtils.hide(mView);
      return;
    }
    final StringBuilder text =
        new StringBuilder(getIcon(router))
            .append(' ')
            .append(StringUtils.nativeFormatDistance(result.distanceM()).toString(mView.getContext()));
    if (result.hasAltitudes())
    {
      text.append("  ↗ ")
          .append(Framework.nativeFormatAltitude(result.ascentM()))
          .append("  ↘ ")
          .append(Framework.nativeFormatAltitude(result.descentM()));
    }
    text.append("  ⏱ ").append(StringUtils.nativeFormatRoutingDuration((int) Math.round(result.timeSec())));
    mView.setText(text);
    UiUtils.show(mView);
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
  private SharedPreferences prefs()
  {
    return MwmApplication.prefs(mView.getContext());
  }
}
