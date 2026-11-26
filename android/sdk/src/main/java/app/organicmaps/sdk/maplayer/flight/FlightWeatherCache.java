package app.organicmaps.sdk.maplayer.flight;

import android.content.Context;
import android.webkit.WebResourceResponse;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.util.log.Logger;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.security.MessageDigest;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.Locale;
import java.util.Map;

/**
 * Maps&Fly: keeps the Open-Meteo answers of the weather page, so that the last forecast of a site is shown
 * offline too. The page asks the network through the WebView, which hands its requests to this class.
 */
public final class FlightWeatherCache
{
  private static final String TAG = FlightWeatherCache.class.getSimpleName();
  private static final String DIR = "flight_weather";
  private static final String HOST = "api.open-meteo.com";
  // Enough for many sites with their several model requests each.
  private static final int MAX_FILES = 400;

  /** Told on a background thread when an answer comes from the cache, with the time it was stored. */
  public interface OfflineListener
  {
    void onOffline(long storedAtMs);
  }

  @NonNull
  private final File mDir;
  @Nullable
  private final OfflineListener mListener;

  public FlightWeatherCache(@NonNull Context context, @Nullable OfflineListener listener)
  {
    mDir = new File(context.getFilesDir(), DIR);
    mListener = listener;
  }

  /** The answer for a request of the page, or null to let the WebView load it itself. */
  @Nullable
  public WebResourceResponse intercept(@NonNull String url)
  {
    if (!url.startsWith("https://" + HOST + "/"))
      return null;

    final File file = new File(mDir, key(url) + ".json");
    byte[] body = null;
    try
    {
      body = download(url);
      if (body != null)
        store(file, body);
    }
    catch (Exception e)
    {
      Logger.w(TAG, "Offline, using the stored forecast if any: " + e.getMessage());
    }
    if (body == null && file.exists())
    {
      body = read(file);
      if (body != null && mListener != null)
        mListener.onOffline(file.lastModified());
    }
    if (body == null)
      return null;

    final Map<String, String> headers = new HashMap<>();
    // The page runs from the assets of the APK, which is another origin.
    headers.put("Access-Control-Allow-Origin", "*");
    return new WebResourceResponse("application/json", "UTF-8", 200, "OK", headers, new ByteArrayInputStream(body));
  }

  @Nullable
  private static byte[] download(@NonNull String url) throws Exception
  {
    final HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
    try
    {
      connection.setConnectTimeout(10000);
      connection.setReadTimeout(30000);
      // Open-Meteo answers errors like an unknown model with a JSON body, which the page reads.
      final int code = connection.getResponseCode();
      try (InputStream in = code < 400 ? connection.getInputStream() : connection.getErrorStream())
      {
        if (in == null)
          return null;
        final byte[] body = readAll(in);
        return code < 400 ? body : null;
      }
    }
    finally
    {
      connection.disconnect();
    }
  }

  private void store(@NonNull File file, @NonNull byte[] body)
  {
    if (!mDir.exists() && !mDir.mkdirs())
      return;
    final File tmp = new File(file.getPath() + ".tmp");
    try (FileOutputStream out = new FileOutputStream(tmp))
    {
      out.write(body);
    }
    catch (Exception e)
    {
      Logger.w(TAG, "Cannot store the forecast", e);
      return;
    }
    if (!tmp.renameTo(file))
      Logger.w(TAG, "Cannot replace " + file);
    prune();
  }

  private synchronized void prune()
  {
    final File[] files = mDir.listFiles();
    if (files == null || files.length <= MAX_FILES)
      return;
    Arrays.sort(files, Comparator.comparingLong(File::lastModified));
    for (int i = 0; i < files.length - MAX_FILES; ++i)
      if (!files[i].delete())
        Logger.w(TAG, "Cannot delete " + files[i]);
  }

  @Nullable
  private static byte[] read(@NonNull File file)
  {
    try (InputStream in = new FileInputStream(file))
    {
      return readAll(in);
    }
    catch (Exception e)
    {
      return null;
    }
  }

  @NonNull
  private static byte[] readAll(@NonNull InputStream in) throws Exception
  {
    try (ByteArrayOutputStream out = new ByteArrayOutputStream())
    {
      final byte[] buffer = new byte[65536];
      int n;
      while ((n = in.read(buffer)) > 0)
        out.write(buffer, 0, n);
      return out.toByteArray();
    }
  }

  @NonNull
  static String key(@NonNull String url)
  {
    try
    {
      final byte[] hash = MessageDigest.getInstance("SHA-1").digest(url.getBytes("UTF-8"));
      final StringBuilder sb = new StringBuilder();
      for (byte b : hash)
        sb.append(String.format(Locale.ROOT, "%02x", b));
      return sb.toString();
    }
    catch (Exception e)
    {
      return Integer.toHexString(url.hashCode());
    }
  }

  /** The address of the weather page of the APK for a takeoff. */
  @NonNull
  public static String pageUrl(@NonNull FlightSite site)
  {
    final StringBuilder sb = new StringBuilder("file:///android_asset/flugwetter/index.html");
    sb.append(String.format(Locale.US, "?lat=%.6f&lon=%.6f", site.mLat, site.mLon));
    if (site.hasAltitude())
      sb.append("&alt=").append(site.mAltitude);
    sb.append("&name=").append(encode(site.mName.isEmpty() ? "Startplatz" : site.mName));
    if (site.mDirections != 0)
      sb.append("&dir=").append(encode(FlightDirections.format(site.mDirections).replace('E', 'O')));
    return sb.toString();
  }

  @NonNull
  private static String encode(@NonNull String value)
  {
    try
    {
      return java.net.URLEncoder.encode(value, "UTF-8").replace("+", "%20");
    }
    catch (Exception e)
    {
      return "";
    }
  }
}
