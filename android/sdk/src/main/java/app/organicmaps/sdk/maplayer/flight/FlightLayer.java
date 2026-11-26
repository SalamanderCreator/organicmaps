package app.organicmaps.sdk.maplayer.flight;

import android.content.Context;
import androidx.annotation.MainThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.util.concurrency.ThreadPool;
import app.organicmaps.sdk.util.concurrency.UiThread;
import app.organicmaps.sdk.util.log.Logger;
import java.io.ByteArrayOutputStream;
import java.io.FileNotFoundException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.function.Consumer;

/**
 * Maps&Fly: the paragliding sites on the map and the flight preparation layer. The sites come from the
 * file of the APK (ParaglidingEarth and OpenStreetMap) and are always shown; the layer shows them bigger
 * with their takeoff directions and the obstacles around them. They are no bookmarks.
 */
public final class FlightLayer
{
  private static final String TAG = FlightLayer.class.getSimpleName();

  private static final Map<String, FlightSite> sSites = new LinkedHashMap<>();
  private static boolean sInitialized;
  @Nullable
  private static Consumer<FlightSite> sTapListener;

  /** An aerial lift station or parking near a site, from the maps. */
  public static final class Service
  {
    public final boolean mCableCar;
    @NonNull
    public final String mName;
    public final double mDistanceM;
    public final double mLat;
    public final double mLon;

    Service(boolean cableCar, @NonNull String name, double distanceM, double lat, double lon)
    {
      mCableCar = cableCar;
      mName = name;
      mDistanceM = distanceM;
      mLat = lat;
      mLon = lon;
    }
  }

  private FlightLayer() {}

  /** Reads the sites and shows them, once per process. */
  @MainThread
  public static void init(@NonNull Context context)
  {
    if (sInitialized)
      return;
    sInitialized = true;
    nativeSetListener(new NativeListener());
    final Context appContext = context.getApplicationContext();
    ThreadPool.getWorker().execute(() -> {
      final List<FlightSite> sites = new ArrayList<>(FlightSources.testSites());
      try (InputStream in = appContext.getAssets().open(FlightSources.ASSET))
      {
        sites.addAll(FlightSources.parseFile(readAll(in)));
      }
      catch (FileNotFoundException e)
      {
        Logger.w(TAG, "The APK has no flight sites");
      }
      catch (Exception e)
      {
        Logger.e(TAG, "Cannot read the flight sites", e);
      }
      UiThread.run(() -> setSites(sites));
    });
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

  /** The landings of a takeoff or the takeoffs of a landing, closest first. */
  @NonNull
  @MainThread
  public static List<FlightSources.Related> getRelated(@NonNull FlightSite site)
  {
    final List<FlightSources.Related> related = FlightSources.related(site, sSites);
    for (FlightSources.Related r : related)
      fillAltitudeFromTerrain(r.mSite);
    return related;
  }

  /** The nearest aerial lift station and parking within 3 km, from the downloaded maps. */
  @NonNull
  @MainThread
  public static List<Service> findServices(@NonNull FlightSite site)
  {
    final List<Service> services = new ArrayList<>();
    for (String item : nativeFindServices(site.mLat, site.mLon))
    {
      final String[] f = item.split("\t", -1);
      if (f.length == 5)
        services.add(new Service("cablecar".equals(f[0]), f[1], Double.parseDouble(f[2]), Double.parseDouble(f[3]),
                                 Double.parseDouble(f[4])));
    }
    return services;
  }

  private static final class NativeListener
  {
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
      // Only landings get a label, and the map labels the ones from OpenStreetMap already.
      labeled[i] = s.mLanding && (s.mSources & FlightSite.SOURCE_OSM) == 0;
      areaOffsets[i] = areaPos / 2;
      System.arraycopy(s.mArea, 0, areaLatLons, areaPos, s.mArea.length);
      areaPos += s.mArea.length;
    }
    areaOffsets[n] = areaPos / 2;
    nativeSetSites(ids, names, lats, lons, landings, directions, restricted, labeled, areaOffsets, areaLatLons);
  }

  /** Takes the height of a site without one from the downloaded terrain, if there is terrain. */
  @MainThread
  private static void fillAltitudeFromTerrain(@NonNull FlightSite site)
  {
    if (site.hasAltitude())
      return;
    final double elevation = nativeGetTerrainElevation(site.mLat, site.mLon);
    if (Double.isNaN(elevation))
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
  @NonNull
  private static native String[] nativeFindServices(double lat, double lon);
  private static native void nativeSetListener(@NonNull Object listener);
  private static native void nativeSetSites(@NonNull String[] ids, @NonNull String[] names, @NonNull double[] lats,
                                            @NonNull double[] lons, @NonNull boolean[] landings,
                                            @NonNull int[] directions, @NonNull boolean[] restricted,
                                            @NonNull boolean[] labeled, @NonNull int[] areaOffsets,
                                            @NonNull double[] areaLatLons);
}
