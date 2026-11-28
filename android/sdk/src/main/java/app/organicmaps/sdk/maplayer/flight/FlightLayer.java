package app.organicmaps.sdk.maplayer.flight;

import android.content.Context;
import androidx.annotation.MainThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.util.concurrency.ThreadPool;
import app.organicmaps.sdk.util.concurrency.UiThread;
import app.organicmaps.sdk.util.log.Logger;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;

/**
 * Maps&Fly: the paragliding sites on the map and the flight preparation layer. The sites come from the
 * file of the APK (ParaglidingEarth and OpenStreetMap) and from a private DHV import on the phone, and are
 * always shown; the layer shows them bigger with their takeoff directions and the obstacles around them.
 * They are no bookmarks.
 */
public final class FlightLayer
{
  private static final String TAG = FlightLayer.class.getSimpleName();

  private static final Map<String, FlightSite> sSites = new LinkedHashMap<>();
  private static boolean sInitialized;
  @Nullable
  private static Consumer<FlightSite> sTapListener;
  @Nullable
  private static ViewportListener sViewportListener;
  // The weather ratings of the takeoffs, kept when the sites are read again.
  private static final Map<String, Rating> sRatings = new HashMap<>();

  /** Ratings as in the weather page: 0 none, 1 red, 2 yellow, 3 light green, 4 green. */
  public static final class Rating
  {
    public final int mLevel;
    public final boolean mStar;
    public final long mRatedAtMs;

    Rating(int level, boolean star, long ratedAtMs)
    {
      mLevel = level;
      mStar = star;
      mRatedAtMs = ratedAtMs;
    }
  }

  /** After each redraw of the flight layer, on the UI thread: the map center and zoom. */
  public interface ViewportListener
  {
    void onViewport(double lat, double lon, int zoom);
  }

  private FlightLayer() {}

  // Same sites of other sources this close to a DHV site are left out, the DHV ones are more detailed.
  private static final double DHV_DUPLICATE_M = 300;

  private static int sDhvTakeoffs;
  @NonNull
  private static String sDhvGenerated = "";

  /** The end of a DHV import, on the UI thread. */
  public interface ImportListener
  {
    void onImported(int takeoffs);
    void onFailed(@NonNull Exception e);
  }

  /** Reads the sites and shows them, once per process. */
  @MainThread
  public static void init(@NonNull Context context)
  {
    if (sInitialized)
      return;
    sInitialized = true;
    nativeSetListener(new NativeListener());
    reload(context.getApplicationContext());
  }

  /** Reads the sites of the APK and the ones imported on the phone and shows them. */
  @MainThread
  private static void reload(@NonNull Context appContext)
  {
    ThreadPool.getWorker().execute(() -> {
      final List<FlightSite> sites = new ArrayList<>(FlightSources.testSites());
      final List<double[]> cables = new ArrayList<>();
      try (InputStream in = appContext.getAssets().open(FlightSources.ASSET))
      {
        final String json = readAll(in);
        sites.addAll(FlightSources.parseFile(json));
        cables.addAll(FlightSources.parseCables(json));
      }
      catch (FileNotFoundException e)
      {
        Logger.w(TAG, "The APK has no flight sites");
      }
      catch (Exception e)
      {
        Logger.e(TAG, "Cannot read the flight sites", e);
      }

      List<FlightSite> dhv = new ArrayList<>();
      String generated = "";
      final File file = dhvFile(appContext);
      if (file.exists())
      {
        try (InputStream in = new FileInputStream(file))
        {
          final String json = readAll(in);
          dhv = FlightSources.parseFile(json);
          generated = FlightSources.generated(json);
        }
        catch (Exception e)
        {
          Logger.e(TAG, "Cannot read the imported DHV sites", e);
        }
      }
      final List<FlightSite> all = new ArrayList<>(FlightSources.withoutDuplicates(sites, dhv, DHV_DUPLICATE_M));
      all.addAll(dhv);
      int takeoffs = 0;
      for (FlightSite s : dhv)
        if (!s.mLanding)
          ++takeoffs;
      final int dhvTakeoffs = takeoffs;
      final String dhvGenerated = generated;
      UiThread.run(() -> {
        sDhvTakeoffs = dhvTakeoffs;
        sDhvGenerated = dhvGenerated;
        setSites(all);
        setCables(cables);
      });
    });
  }

  /**
   * Imports the "DHV XML" file of |in| (.xml or .zip) for this phone only, replacing an earlier import, and
   * shows its sites.
   */
  @MainThread
  public static void importDhv(@NonNull Context context, @NonNull java.util.concurrent.Callable<InputStream> in,
                               @NonNull ImportListener listener)
  {
    final Context appContext = context.getApplicationContext();
    ThreadPool.getWorker().execute(() -> {
      try (InputStream stream = in.call())
      {
        final FlightDhvImport.Result result = FlightDhvImport.read(stream);
        final File file = dhvFile(appContext);
        final File tmp = new File(file.getPath() + ".tmp");
        try (FileOutputStream out = new FileOutputStream(tmp))
        {
          out.write(FlightSources.toFile(result.mSites, result.mGenerated).getBytes(StandardCharsets.UTF_8));
        }
        if (!tmp.renameTo(file))
          throw new java.io.IOException("Cannot replace " + file);
        UiThread.run(() -> {
          reload(appContext);
          listener.onImported(result.mTakeoffs);
        });
      }
      catch (Exception e)
      {
        Logger.e(TAG, "Cannot import the DHV file", e);
        UiThread.run(() -> listener.onFailed(e));
      }
    });
  }

  /** Deletes the imported DHV sites from the phone. */
  @MainThread
  public static void removeDhv(@NonNull Context context)
  {
    final Context appContext = context.getApplicationContext();
    if (!dhvFile(appContext).delete())
      Logger.w(TAG, "No DHV sites to delete");
    reload(appContext);
  }

  /** The number of imported DHV takeoffs, 0 without an import. */
  @MainThread
  public static int getDhvTakeoffs()
  {
    return sDhvTakeoffs;
  }

  /** The date of the imported DHV file, or empty. */
  @NonNull
  @MainThread
  public static String getDhvGenerated()
  {
    return sDhvGenerated;
  }

  @NonNull
  private static File dhvFile(@NonNull Context appContext)
  {
    return new File(appContext.getFilesDir(), FlightDhvImport.FILE);
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

  @MainThread
  public static void setViewportListener(@Nullable ViewportListener listener)
  {
    sViewportListener = listener;
  }

  /** Shows how flyable the day is at a takeoff, in the flight layer. */
  @MainThread
  public static void setRating(@NonNull String id, int level, boolean star)
  {
    sRatings.put(id, new Rating(level, star, System.currentTimeMillis()));
    nativeSetRatings(new String[] {id}, new int[] {level}, new boolean[] {star});
  }

  @Nullable
  @MainThread
  public static Rating getRating(@NonNull String id)
  {
    return sRatings.get(id);
  }

  /** Up to |max| takeoffs within |radiusM| of a point, the nearest first. */
  @NonNull
  @MainThread
  public static List<FlightSite> takeoffsNear(double lat, double lon, double radiusM, int max)
  {
    final List<FlightSite> near = new ArrayList<>();
    for (FlightSite s : sSites.values())
      if (!s.mLanding && FlightSources.distanceM(lat, lon, s.mLat, s.mLon) <= radiusM)
        near.add(s);
    near.sort(Comparator.comparingDouble(s -> FlightSources.distanceM(lat, lon, s.mLat, s.mLon)));
    return near.size() > max ? new ArrayList<>(near.subList(0, max)) : near;
  }

  private static final class NativeListener
  {
    // Called by the core on the UI thread.
    @SuppressWarnings("unused")
    public void onViewport(double lat, double lon, int zoom)
    {
      if (sViewportListener != null)
        sViewportListener.onViewport(lat, lon, zoom);
    }

    // Called by the core on the UI thread.
    @SuppressWarnings("unused")
    public void onSiteTapped(@NonNull String id)
    {
      final FlightSite site = sSites.get(id);
      if (site == null)
        return;
      // The terrain of the region may have been downloaded after the sites were shown.
      fillAltitudeFromTerrain(site);
      if (sTapListener != null)
        sTapListener.accept(site);
    }
  }

  @MainThread
  private static void setSites(@NonNull List<FlightSite> sites)
  {
    sSites.clear();
    final int n = sites.size();
    final String[] ids = new String[n];
    final String[] names = new String[n];
    final double[] lats = new double[n];
    final double[] lons = new double[n];
    final boolean[] landings = new boolean[n];
    final int[] directions = new int[n];
    final boolean[] restricted = new boolean[n];
    final boolean[] unconfirmed = new boolean[n];
    final boolean[] labeled = new boolean[n];
    final int[] areaOffsets = new int[n + 1];
    int areaSize = 0;
    for (FlightSite s : sites)
      areaSize += s.mArea.length;
    final double[] areaLatLons = new double[areaSize];
    int areaPos = 0;
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
      restricted[i] = s.mRestricted;
      // Takeoffs only known from OpenStreetMap are not confirmed by a flying site database.
      unconfirmed[i] = !s.mLanding && s.mSources == FlightSite.SOURCE_OSM;
      // Only landings get a label, and the map labels the ones from OpenStreetMap already.
      labeled[i] = s.mLanding && (s.mSources & FlightSite.SOURCE_OSM) == 0;
      areaOffsets[i] = areaPos / 2;
      System.arraycopy(s.mArea, 0, areaLatLons, areaPos, s.mArea.length);
      areaPos += s.mArea.length;
    }
    areaOffsets[n] = areaPos / 2;
    nativeSetSites(ids, names, lats, lons, landings, directions, restricted, unconfirmed, labeled, areaOffsets,
                   areaLatLons);
    // Ratings of sites that are still there.
    final List<String> ratedIds = new ArrayList<>();
    for (String id : sRatings.keySet())
      if (sSites.containsKey(id))
        ratedIds.add(id);
    final String[] rIds = ratedIds.toArray(new String[0]);
    final int[] levels = new int[rIds.length];
    final boolean[] stars = new boolean[rIds.length];
    for (int i = 0; i < rIds.length; ++i)
    {
      final Rating r = sRatings.get(rIds[i]);
      levels[i] = r.mLevel;
      stars[i] = r.mStar;
    }
    nativeSetRatings(rIds, levels, stars);
  }

  @MainThread
  private static void setCables(@NonNull List<double[]> cables)
  {
    final int[] offsets = new int[cables.size() + 1];
    int size = 0;
    for (double[] c : cables)
      size += c.length;
    final double[] latLons = new double[size];
    int pos = 0;
    for (int i = 0; i < cables.size(); ++i)
    {
      offsets[i] = pos / 2;
      System.arraycopy(cables.get(i), 0, latLons, pos, cables.get(i).length);
      pos += cables.get(i).length;
    }
    offsets[cables.size()] = pos / 2;
    nativeSetCables(offsets, latLons);
  }

  /** Takes the height of a site without one from the downloaded terrain, if there is terrain. */
  @MainThread
  private static void fillAltitudeFromTerrain(@NonNull FlightSite site)
  {
    if (site.hasAltitude())
      return;
    final double elevation = nativeGetTerrainElevation(site.mLat, site.mLon);
    // Below any terrain: none there.
    if (elevation < -20000)
      return;
    site.mAltitude = (int) Math.round(elevation);
    site.mAltitudeFromTerrain = true;
  }

  @NonNull
  private static String readAll(@NonNull InputStream in) throws Exception
  {
    try (ByteArrayOutputStream out = new ByteArrayOutputStream())
    {
      final byte[] buffer = new byte[65536];
      int n;
      while ((n = in.read(buffer)) > 0)
        out.write(buffer, 0, n);
      return out.toString("UTF-8");
    }
  }

  private static native void nativeSetEnabled(boolean enabled);
  private static native boolean nativeIsEnabled();
  private static native double nativeGetTerrainElevation(double lat, double lon);
  private static native void nativeSetListener(@NonNull Object listener);
  private static native void nativeSetSites(@NonNull String[] ids, @NonNull String[] names, @NonNull double[] lats,
                                            @NonNull double[] lons, @NonNull boolean[] landings,
                                            @NonNull int[] directions, @NonNull boolean[] restricted,
                                            @NonNull boolean[] unconfirmed, @NonNull boolean[] labeled,
                                            @NonNull int[] areaOffsets, @NonNull double[] areaLatLons);
  private static native void nativeSetRatings(@NonNull String[] ids, @NonNull int[] levels,
                                              @NonNull boolean[] stars);
  private static native void nativeSetCables(@NonNull int[] offsets, @NonNull double[] latLons);
}
