package app.organicmaps.sdk.maplayer.flight;

import android.content.Context;
import androidx.annotation.MainThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.util.concurrency.UiThread;
import app.organicmaps.sdk.util.log.Logger;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;

/**
 * Maps&Fly: the flight preparation layer. The core asks for the sites of the 1 x 1 degree cells in view,
 * this class reads them from its cache or loads them from ParaglidingEarth and OpenStreetMap, and shows
 * the place of a tapped site. The sites are no bookmarks, they live only in the cache files.
 */
public final class FlightLayer
{
  private static final String TAG = FlightLayer.class.getSimpleName();
  private static final String CACHE_DIR = "flight_sites";
  // Sites change rarely, older cells are reloaded when online.
  private static final long CACHE_MAX_AGE_MS = TimeUnit.DAYS.toMillis(14);

  // One request after the other, as Overpass allows few at once.
  private static final ExecutorService sLoader = Executors.newSingleThreadExecutor();
  private static final Map<String, FlightSite> sSites = new HashMap<>();
  private static final Set<Integer> sLoadedCells = new HashSet<>();
  private static final Set<Integer> sLoadingCells = new HashSet<>();
  @Nullable
  private static Context sContext;
  @Nullable
  private static Consumer<FlightSite> sTapListener;

  private FlightLayer() {}

  /** Connects the core with the loader, once per process. */
  @MainThread
  public static void init(@NonNull Context context)
  {
    if (sContext != null)
      return;
    sContext = context.getApplicationContext();
    nativeSetListener(new NativeListener());
  }

  public static boolean isEnabled()
  {
    return nativeIsEnabled();
  }

  @MainThread
  public static void setEnabled(@NonNull Context context, boolean enabled)
  {
    init(context);
    nativeSetEnabled(enabled);
    // Asks again for cells that failed to load, e.g. when offline the last time.
    if (enabled)
      nativeSetListener(new NativeListener());
  }

  /** Called with a tapped site instead of the place page. */
  @MainThread
  public static void setTapListener(@Nullable Consumer<FlightSite> listener)
  {
    sTapListener = listener;
  }

  @Nullable
  @MainThread
  public static FlightSite getSite(@NonNull String id)
  {
    return sSites.get(id);
  }

  private static final class NativeListener
  {
    // Called by the core on the UI thread.
    @SuppressWarnings("unused")
    public void onCellsNeeded(@NonNull int[] cells)
    {
      final Context context = sContext;
      if (context == null)
        return;
      for (int cell : cells)
      {
        if (sLoadedCells.contains(cell) || !sLoadingCells.add(cell))
          continue;
        sLoader.execute(() -> {
          final List<FlightSite> sites = load(context, cell);
          UiThread.run(() -> onCellLoaded(cell, sites));
        });
      }
    }

    @SuppressWarnings("unused")
    public void onSiteTapped(@NonNull String id)
    {
      final FlightSite site = sSites.get(id);
      if (site != null && sTapListener != null)
        sTapListener.accept(site);
    }
  }

  @MainThread
  private static void onCellLoaded(int cell, @Nullable List<FlightSite> sites)
  {
    sLoadingCells.remove(cell);
    if (sites == null)
      return;
    sLoadedCells.add(cell);

    final int n = sites.size();
    final String[] ids = new String[n];
    final String[] names = new String[n];
    final double[] lats = new double[n];
    final double[] lons = new double[n];
    final boolean[] landings = new boolean[n];
    final int[] directions = new int[n];
    final int[] areaOffsets = new int[n + 1];
    final List<Double> area = new ArrayList<>();
    for (int i = 0; i < n; ++i)
    {
      final FlightSite s = sites.get(i);
      sSites.put(s.mId, s);
      ids[i] = s.mId;
      names[i] = s.mName;
      lats[i] = s.mLat;
      lons[i] = s.mLon;
      landings[i] = s.mLanding;
      directions[i] = s.mDirections;
      areaOffsets[i] = area.size() / 2;
      for (double v : s.mArea)
        area.add(v);
    }
    areaOffsets[n] = area.size() / 2;
    final double[] areaLatLons = new double[area.size()];
    for (int i = 0; i < areaLatLons.length; ++i)
      areaLatLons[i] = area.get(i);
    nativeSetSites(cell, ids, names, lats, lons, landings, directions, areaOffsets, areaLatLons);
  }

  /** The sites of a cell from a fresh cache, else from the sources, else from an old cache. Null if none. */
  @Nullable
  private static List<FlightSite> load(@NonNull Context context, int cell)
  {
    final File file = new File(new File(context.getFilesDir(), CACHE_DIR), "cell_" + cell + ".json");
    if (file.exists() && System.currentTimeMillis() - file.lastModified() < CACHE_MAX_AGE_MS)
    {
      final List<FlightSite> cached = readCache(file);
      if (cached != null)
        return cached;
    }

    try
    {
      final String pge = download(FlightSources.pgeUrl(cell));
      final String osm = download(FlightSources.overpassUrl(cell));
      // Both sources are needed for a complete cell in the cache.
      if (pge != null && osm != null)
      {
        final List<FlightSite> sites =
            FlightSources.merge(FlightSources.parsePge(pge), FlightSources.parseOverpass(osm));
        writeCache(file, FlightSources.toCache(sites));
        return sites;
      }
    }
    catch (Exception e)
    {
      Logger.w(TAG, "Cannot load the flight sites of cell " + cell, e);
    }
    return file.exists() ? readCache(file) : null;
  }

  @Nullable
  private static List<FlightSite> readCache(@NonNull File file)
  {
    try (InputStream in = new java.io.FileInputStream(file))
    {
      return FlightSources.fromCache(readAll(in));
    }
    catch (Exception e)
    {
      Logger.w(TAG, "Cannot read " + file, e);
      return null;
    }
  }

  private static void writeCache(@NonNull File file, @NonNull String json)
  {
    final File dir = file.getParentFile();
    if (dir != null && !dir.exists() && !dir.mkdirs())
      return;
    final File tmp = new File(file.getPath() + ".tmp");
    try (FileOutputStream out = new FileOutputStream(tmp))
    {
      out.write(json.getBytes(StandardCharsets.UTF_8));
    }
    catch (Exception e)
    {
      Logger.w(TAG, "Cannot write " + file, e);
      return;
    }
    if (!tmp.renameTo(file))
      Logger.w(TAG, "Cannot replace " + file);
  }

  /** The body of a successful GET, null for another HTTP status. */
  @Nullable
  static String download(@NonNull String url) throws Exception
  {
    final HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
    try
    {
      connection.setConnectTimeout(15000);
      connection.setReadTimeout(30000);
      connection.setRequestProperty("User-Agent", "Maps&Fly (private Organic Maps fork)");
      if (connection.getResponseCode() != HttpURLConnection.HTTP_OK)
      {
        Logger.w(TAG, "HTTP " + connection.getResponseCode() + " for " + url);
        return null;
      }
      try (InputStream in = connection.getInputStream())
      {
        return readAll(in);
      }
    }
    finally
    {
      connection.disconnect();
    }
  }

  @NonNull
  private static String readAll(@NonNull InputStream in) throws Exception
  {
    try (ByteArrayOutputStream out = new ByteArrayOutputStream())
    {
      final byte[] buffer = new byte[8192];
      int n;
      while ((n = in.read(buffer)) > 0)
        out.write(buffer, 0, n);
      return out.toString("UTF-8");
    }
  }

  private static native void nativeSetEnabled(boolean enabled);
  private static native boolean nativeIsEnabled();
  private static native void nativeSetListener(@NonNull Object listener);
  private static native void nativeSetSites(int cell, @NonNull String[] ids, @NonNull String[] names,
                                            @NonNull double[] lats, @NonNull double[] lons,
                                            @NonNull boolean[] landings, @NonNull int[] directions,
                                            @NonNull int[] areaOffsets, @NonNull double[] areaLatLons);
}
