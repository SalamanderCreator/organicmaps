package app.organicmaps.sdk.maplayer.radar;

import android.content.Context;
import android.content.SharedPreferences;
import androidx.annotation.MainThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.util.concurrency.ThreadPool;
import app.organicmaps.sdk.util.concurrency.UiThread;
import app.organicmaps.sdk.util.log.Logger;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;
import java.util.concurrent.TimeUnit;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Maps&Fly: the rain radar layer over the map. Keeps the list of radar time frames of the chosen
 * source and shows the selected one through Framework.nativeSetTileOverlay.
 *
 * Sources (free, no key, see the B3 notes in the plan):
 * - DWD: WMS of the Deutscher Wetterdienst, Germany and the border regions, 5 minute steps from
 *   2 hours ago up to the 2 hour nowcast. Attribution "Deutscher Wetterdienst" is required.
 * - RainViewer: worldwide, the past 2 hours in 10 minute steps, no forecast, zoom 7 at most, free
 *   for personal use with the credit "RainViewer".
 */
public final class RainRadar
{
  private static final String TAG = RainRadar.class.getSimpleName();

  // Shared with the app settings screen (default SharedPreferences).
  public static final String PREF_ENABLED = "RainRadarEnabled";
  public static final String PREF_SOURCE = "RainRadarSource";
  public static final String PREF_OPACITY = "RainRadarOpacity";
  public static final String SOURCE_DWD = "dwd";
  public static final String SOURCE_RAINVIEWER = "rainviewer";
  public static final int DEFAULT_OPACITY = 70;

  private static final long STEP_DWD_MS = TimeUnit.MINUTES.toMillis(5);
  // The newest DWD scan is usually published a few minutes after its time.
  private static final long DWD_DELAY_MS = TimeUnit.MINUTES.toMillis(10);
  private static final long DWD_RANGE_MS = TimeUnit.HOURS.toMillis(2);
  private static final String DWD_URL = "https://maps.dwd.de/geoserver/dwd/wms?service=WMS&version=1.3.0"
                                      + "&request=GetMap&layers=dwd:Niederschlagsradar&styles=&format=image/png"
                                      + "&transparent=true&crs=EPSG:3857&width=256&height=256&bbox={bbox}&time=";
  private static final int DWD_MAX_ZOOM = 8;
  private static final String RAINVIEWER_MAPS = "https://api.rainviewer.com/public/weather-maps.json";
  // 512 px tiles at the source's zoom limit 7, colour scheme 2 (the only free one), smoothed, snow shown.
  private static final String RAINVIEWER_TILE = "/512/{z}/{x}/{y}/2/1_1.png";
  private static final int RAINVIEWER_MAX_ZOOM = 7;
  // A newer RainViewer list is fetched when the layer is re-enabled after this time.
  private static final long REFRESH_AFTER_MS = TimeUnit.MINUTES.toMillis(5);

  public static final class Frame
  {
    public final long mTimeMs;
    public final boolean mForecast;
    @NonNull
    final String mUrl;

    Frame(long timeMs, boolean forecast, @NonNull String url)
    {
      mTimeMs = timeMs;
      mForecast = forecast;
      mUrl = url;
    }
  }

  public static final class State
  {
    public final boolean mEnabled;
    @NonNull
    public final String mSource;
    @NonNull
    public final List<Frame> mFrames;
    // The frame shown, -1 while the list loads or failed.
    public final int mIndex;
    // The frame closest to the present: the slider start and the "now" mark.
    public final int mNowIndex;
    public final boolean mLoading;
    public final boolean mFailed;

    State(boolean enabled, @NonNull String source, @NonNull List<Frame> frames, int index, int nowIndex,
          boolean loading, boolean failed)
    {
      mEnabled = enabled;
      mSource = source;
      mFrames = frames;
      mIndex = index;
      mNowIndex = nowIndex;
      mLoading = loading;
      mFailed = failed;
    }
  }

  private static final MutableLiveData<State> sState =
      new MutableLiveData<>(new State(false, SOURCE_DWD, Collections.emptyList(), -1, -1, false, false));
  private static long sLoadedAtMs;
  // Drops the result of an older load when the source changed or the layer was switched off meanwhile.
  private static int sLoadGeneration;

  private RainRadar() {}

  @NonNull
  public static LiveData<State> getState()
  {
    return sState;
  }

  @NonNull
  private static SharedPreferences prefs(@NonNull Context context)
  {
    return context.getSharedPreferences(context.getPackageName() + "_preferences", Context.MODE_PRIVATE);
  }

  public static boolean isEnabled(@NonNull Context context)
  {
    return prefs(context).getBoolean(PREF_ENABLED, false);
  }

  @NonNull
  public static String getSource(@NonNull Context context)
  {
    return prefs(context).getString(PREF_SOURCE, SOURCE_DWD);
  }

  public static int getOpacity(@NonNull Context context)
  {
    return prefs(context).getInt(PREF_OPACITY, DEFAULT_OPACITY);
  }

  @MainThread
  public static void setEnabled(@NonNull Context context, boolean enabled)
  {
    prefs(context).edit().putBoolean(PREF_ENABLED, enabled).apply();
    if (enabled)
    {
      reload(context);
    }
    else
    {
      ++sLoadGeneration;
      Framework.nativeSetTileOverlay("", 0, 0, false);
      sState.setValue(new State(false, getSource(context), Collections.emptyList(), -1, -1, false, false));
    }
  }

  /** Re-reads the frames when the radar is on and the list is older than a few minutes (app resume). */
  @MainThread
  public static void refreshIfStale(@NonNull Context context)
  {
    if (!isEnabled(context))
      return;
    final State state = sState.getValue();
    if (state == null || !state.mEnabled || System.currentTimeMillis() - sLoadedAtMs > REFRESH_AFTER_MS)
      reload(context);
  }

  @MainThread
  public static void setSource(@NonNull Context context, @NonNull String source)
  {
    if (source.equals(getSource(context)))
      return;
    prefs(context).edit().putString(PREF_SOURCE, source).apply();
    if (isEnabled(context))
      reload(context);
  }

  /** Re-shows the current frame with the new opacity (from the disk cache, no new list). */
  @MainThread
  public static void setOpacity(@NonNull Context context, int opacityPct)
  {
    prefs(context).edit().putInt(PREF_OPACITY, opacityPct).apply();
    final State state = sState.getValue();
    if (state != null && state.mEnabled && state.mIndex >= 0)
      show(context, state.mSource, state.mFrames.get(state.mIndex));
  }

  @MainThread
  public static void selectFrame(@NonNull Context context, int index)
  {
    final State state = sState.getValue();
    if (state == null || !state.mEnabled || index < 0 || index >= state.mFrames.size() || index == state.mIndex)
      return;
    sState.setValue(new State(true, state.mSource, state.mFrames, index, state.mNowIndex, false, false));
    show(context, state.mSource, state.mFrames.get(index));
  }

  @MainThread
  private static void reload(@NonNull Context context)
  {
    final String source = getSource(context);
    final int generation = ++sLoadGeneration;
    sState.setValue(new State(true, source, Collections.emptyList(), -1, -1, true, false));
    final Context appContext = context.getApplicationContext();
    if (SOURCE_RAINVIEWER.equals(source))
    {
      ThreadPool.getWorker().execute(() -> {
        final List<Frame> frames = loadRainViewerFrames();
        UiThread.run(() -> onFramesLoaded(appContext, generation, source, frames, frames.size() - 1));
      });
    }
    else
    {
      final long now = System.currentTimeMillis();
      final List<Frame> frames = makeDwdFrames(now);
      onFramesLoaded(appContext, generation, source, frames, (int) (DWD_RANGE_MS / STEP_DWD_MS));
    }
  }

  @MainThread
  private static void onFramesLoaded(@NonNull Context context, int generation, @NonNull String source,
                                     @NonNull List<Frame> frames, int nowIndex)
  {
    if (generation != sLoadGeneration)
      return;
    if (frames.isEmpty())
    {
      Framework.nativeSetTileOverlay("", 0, 0, false);
      sState.setValue(new State(true, source, frames, -1, -1, false, true));
      return;
    }
    sLoadedAtMs = System.currentTimeMillis();
    sState.setValue(new State(true, source, frames, nowIndex, nowIndex, false, false));
    show(context, source, frames.get(nowIndex));
  }

  private static void show(@NonNull Context context, @NonNull String source, @NonNull Frame frame)
  {
    final boolean dwd = SOURCE_DWD.equals(source);
    Framework.nativeSetTileOverlay(frame.mUrl, dwd ? DWD_MAX_ZOOM : RAINVIEWER_MAX_ZOOM, getOpacity(context), dwd);
  }

  @NonNull
  static List<Frame> makeDwdFrames(long nowMs)
  {
    final long latest = (nowMs - DWD_DELAY_MS) / STEP_DWD_MS * STEP_DWD_MS;
    final SimpleDateFormat iso = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'.000Z'", Locale.US);
    iso.setTimeZone(TimeZone.getTimeZone("UTC"));
    final List<Frame> frames = new ArrayList<>();
    for (long t = latest - DWD_RANGE_MS; t <= latest + DWD_RANGE_MS; t += STEP_DWD_MS)
      frames.add(new Frame(t, t > latest, DWD_URL + iso.format(new Date(t))));
    return frames;
  }

  @NonNull
  private static List<Frame> loadRainViewerFrames()
  {
    final List<Frame> frames = new ArrayList<>();
    try
    {
      final String body = download(RAINVIEWER_MAPS);
      if (body == null)
        return frames;
      final JSONObject json = new JSONObject(body);
      final String host = json.getString("host");
      final JSONArray past = json.getJSONObject("radar").getJSONArray("past");
      for (int i = 0; i < past.length(); ++i)
      {
        final JSONObject item = past.getJSONObject(i);
        frames.add(new Frame(item.getLong("time") * 1000, false, host + item.getString("path") + RAINVIEWER_TILE));
      }
    }
    catch (Exception e)
    {
      Logger.e(TAG, "Failed to read the RainViewer frames", e);
      frames.clear();
    }
    return frames;
  }

  @Nullable
  static String download(@NonNull String url) throws Exception
  {
    final HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
    try
    {
      connection.setConnectTimeout(10000);
      connection.setReadTimeout(10000);
      if (connection.getResponseCode() != HttpURLConnection.HTTP_OK)
      {
        Logger.w(TAG, "HTTP " + connection.getResponseCode() + " for " + url);
        return null;
      }
      try (InputStream in = connection.getInputStream(); ByteArrayOutputStream out = new ByteArrayOutputStream())
      {
        final byte[] buffer = new byte[8192];
        int n;
        while ((n = in.read(buffer)) > 0)
          out.write(buffer, 0, n);
        return out.toString("UTF-8");
      }
    }
    finally
    {
      connection.disconnect();
    }
  }
}
