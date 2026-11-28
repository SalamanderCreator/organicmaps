package app.organicmaps.maplayer;

import android.annotation.SuppressLint;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.webkit.JavascriptInterface;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import androidx.annotation.MainThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.maplayer.flight.FlightDirections;
import app.organicmaps.sdk.maplayer.flight.FlightLayer;
import app.organicmaps.sdk.maplayer.flight.FlightSite;
import app.organicmaps.sdk.maplayer.flight.FlightWeatherCache;
import app.organicmaps.sdk.util.log.Logger;
import org.json.JSONArray;
import org.json.JSONObject;

/**
 * Maps&Fly: rates how flyable today is at the takeoffs around the map center in the flight layer. The
 * weather page of the APK does the rating in a WebView that is never shown, with the same data and code as
 * when a takeoff is opened. Its answers are stored, so the ratings and the weather pages work offline too.
 */
public final class FlightRater
{
  private static final String TAG = FlightRater.class.getSimpleName();
  private static final String PAGE = "file:///android_asset/flugwetter/index.html?rate=1";
  private static final int MIN_ZOOM = 10;
  private static final double RADIUS_M = 20_000;
  private static final int MAX_SITES = 25;
  private static final long MAX_AGE_MS = 60 * 60 * 1000;
  // Waits until the map stands still.
  private static final long DELAY_MS = 1500;

  @NonNull
  private final Context mContext;
  @NonNull
  private final Handler mHandler = new Handler(Looper.getMainLooper());
  @Nullable
  private WebView mWeb;
  private boolean mReady;
  private boolean mBusy;
  private double mLat;
  private double mLon;
  private boolean mPending;
  private final Runnable mRun = this::rate;

  public FlightRater(@NonNull Context context)
  {
    mContext = context;
  }

  /** The flight layer moved: rate the takeoffs around once the map stands still. */
  @MainThread
  public void onViewport(double lat, double lon, int zoom)
  {
    if (zoom < MIN_ZOOM)
      return;
    mLat = lat;
    mLon = lon;
    mPending = true;
    mHandler.removeCallbacks(mRun);
    mHandler.postDelayed(mRun, DELAY_MS);
  }

  @MainThread
  public void destroy()
  {
    mHandler.removeCallbacks(mRun);
    if (mWeb != null)
    {
      mWeb.destroy();
      mWeb = null;
    }
    mReady = false;
    mBusy = false;
  }

  @MainThread
  private void rate()
  {
    if (!mPending)
      return;
    if (mWeb == null)
    {
      createWebView();
      return;
    }
    if (!mReady || mBusy)
      return;
    mPending = false;

    final long now = System.currentTimeMillis();
    final JSONArray list = new JSONArray();
    try
    {
      for (FlightSite site : FlightLayer.takeoffsNear(mLat, mLon, RADIUS_M, MAX_SITES))
      {
        final FlightLayer.Rating rating = FlightLayer.getRating(site.mId);
        if (rating != null && now - rating.mRatedAtMs < MAX_AGE_MS)
          continue;
        final JSONObject o = new JSONObject();
        o.put("id", site.mId);
        o.put("lat", site.mLat);
        o.put("lon", site.mLon);
        o.put("alt", site.hasAltitude() ? site.mAltitude : 0);
        o.put("dir", FlightDirections.format(site.mDirections).replace('E', 'O'));
        list.put(o);
      }
    }
    catch (Exception e)
    {
      Logger.e(TAG, "Cannot list the takeoffs", e);
      return;
    }
    if (list.length() == 0)
      return;
    mBusy = true;
    mWeb.evaluateJavascript("rateSites(" + list + ")", null);
  }

  @SuppressLint({"SetJavaScriptEnabled", "AddJavascriptInterface"})
  @MainThread
  private void createWebView()
  {
    final FlightWeatherCache cache = new FlightWeatherCache(mContext, null);
    mWeb = new WebView(mContext);
    mWeb.getSettings().setJavaScriptEnabled(true);
    mWeb.setWebViewClient(new WebViewClient() {
      @Nullable
      @Override
      public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request)
      {
        return cache.intercept(request.getUrl().toString());
      }

      @Override
      public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request)
      {
        return true;
      }
    });
    mWeb.addJavascriptInterface(new Bridge(), "MapsFly");
    mWeb.loadUrl(PAGE);
  }

  // Called by the page on a background thread.
  private final class Bridge
  {
    @JavascriptInterface
    public void onReady()
    {
      mHandler.post(() -> {
        mReady = true;
        rate();
      });
    }

    @JavascriptInterface
    public void onRating(String id, int level, boolean star)
    {
      mHandler.post(() -> FlightLayer.setRating(id, level, star));
    }

    @JavascriptInterface
    public void onDone()
    {
      mHandler.post(() -> {
        mBusy = false;
        rate();
      });
    }
  }
}
