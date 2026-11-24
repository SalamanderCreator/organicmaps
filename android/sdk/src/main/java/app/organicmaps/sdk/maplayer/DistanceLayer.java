package app.organicmaps.sdk.maplayer;

import android.content.Context;
import android.content.SharedPreferences;
import androidx.annotation.NonNull;
import app.organicmaps.sdk.Framework;

/**
 * Maps&Fly: the "Distance" layer shows the circles, the straight line or both. Which parts it shows is
 * kept here while the layer is off; the core keeps what is shown right now.
 */
public final class DistanceLayer
{
  private static final String PREF_CIRCLES = "DistanceLayerCircles";
  private static final String PREF_LINE = "DistanceLayerLine";

  private DistanceLayer() {}

  public static boolean isEnabled()
  {
    return Framework.nativeAreDistanceCirclesEnabled() || Framework.nativeIsDistanceLineEnabled();
  }

  public static void setEnabled(@NonNull Context context, boolean enabled)
  {
    Framework.nativeSetDistanceCirclesEnabled(enabled && hasCircles(context));
    Framework.nativeSetDistanceLineEnabled(enabled && hasLine(context));
  }

  public static boolean hasCircles(@NonNull Context context)
  {
    return prefs(context).getBoolean(PREF_CIRCLES, true);
  }

  public static boolean hasLine(@NonNull Context context)
  {
    return prefs(context).getBoolean(PREF_LINE, true);
  }

  /** At least one part must stay chosen. Switches the layer on. */
  public static void setParts(@NonNull Context context, boolean circles, boolean line)
  {
    if (!circles && !line)
      throw new IllegalArgumentException("The distance layer needs circles or a line");
    prefs(context).edit().putBoolean(PREF_CIRCLES, circles).putBoolean(PREF_LINE, line).apply();
    Framework.nativeSetDistanceCirclesEnabled(circles);
    Framework.nativeSetDistanceLineEnabled(line);
  }

  @NonNull
  private static SharedPreferences prefs(@NonNull Context context)
  {
    return context.getSharedPreferences(context.getPackageName() + "_preferences", Context.MODE_PRIVATE);
  }
}
