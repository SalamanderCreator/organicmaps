package app.organicmaps.maplayer;

import android.annotation.SuppressLint;
import android.content.Context;
import android.net.Uri;
import android.text.format.DateFormat;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.WebResourceRequest;
import android.webkit.WebResourceResponse;
import android.webkit.WebSettings;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.R;
import app.organicmaps.sdk.maplayer.flight.FlightSite;
import app.organicmaps.sdk.maplayer.flight.FlightWeatherCache;
import app.organicmaps.sdk.util.concurrency.UiThread;
import app.organicmaps.util.Utils;
import com.google.android.material.bottomsheet.BottomSheetBehavior;
import com.google.android.material.bottomsheet.BottomSheetDialog;
import java.util.Date;

/**
 * Maps&Fly: the weather of a takeoff in a window over the map. It shows the weather page of the APK
 * (a copy of flugwetter/index.html) for the site; its forecasts are kept, so that the last one is shown
 * offline too.
 */
public final class FlightWeatherSheet
{
  // Of the screen height, the map stays visible above.
  private static final float HEIGHT = 0.85f;

  private FlightWeatherSheet() {}

  /** |onRoute| builds a route to the site in the app, for the navigation button of the page. */
  @SuppressLint("SetJavaScriptEnabled")
  public static void show(@NonNull Context context, @NonNull FlightSite site, @NonNull Runnable onRoute)
  {
    final BottomSheetDialog dialog = new BottomSheetDialog(context);
    final View view = LayoutInflater.from(context).inflate(R.layout.flight_weather_sheet, null);
    dialog.setContentView(view);

    view.findViewById(R.id.flight_weather_info).setOnClickListener(v -> FlightSiteSheet.show(context, site));
    view.findViewById(R.id.flight_weather_close).setOnClickListener(v -> dialog.dismiss());
    final FlightDirectionsView directions = view.findViewById(R.id.flight_weather_directions);
    directions.setDirections(site.mDirections);

    final TextView offline = view.findViewById(R.id.flight_weather_offline);
    final FlightWeatherCache cache = new FlightWeatherCache(context, storedAtMs -> UiThread.run(() -> {
      final Date stored = new Date(storedAtMs);
      offline.setText(context.getString(R.string.flight_weather_offline,
                                        DateFormat.getDateFormat(context).format(stored) + " "
                                            + DateFormat.getTimeFormat(context).format(stored)));
      offline.setVisibility(View.VISIBLE);
    }));

    final WebView web = view.findViewById(R.id.flight_weather_web);
    final ViewGroup.LayoutParams params = web.getLayoutParams();
    params.height = Math.round(context.getResources().getDisplayMetrics().heightPixels * HEIGHT);
    web.setLayoutParams(params);
    final WebSettings settings = web.getSettings();
    settings.setJavaScriptEnabled(true);
    settings.setDomStorageEnabled(true);
    web.setWebViewClient(new WebViewClient() {
      @Nullable
      @Override
      public WebResourceResponse shouldInterceptRequest(WebView view, WebResourceRequest request)
      {
        return cache.intercept(request.getUrl().toString());
      }

      @Override
      public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request)
      {
        final Uri uri = request.getUrl();
        if ("file".equals(uri.getScheme()))
          return false;
        // The navigation button of the page builds the route in the app, other links open outside.
        if (isNavigation(uri))
        {
          dialog.dismiss();
          onRoute.run();
        }
        else
        {
          Utils.openUrl(context, uri.toString());
        }
        return true;
      }
    });
    // Links with target="_blank" load in this WebView and so come to shouldOverrideUrlLoading.
    settings.setSupportMultipleWindows(false);
    web.loadUrl(FlightWeatherCache.pageUrl(site));

    // The page scrolls itself, the window must not move with it.
    final BottomSheetBehavior<?> behavior = dialog.getBehavior();
    behavior.setState(BottomSheetBehavior.STATE_EXPANDED);
    behavior.setSkipCollapsed(true);
    behavior.setDraggable(false);
    dialog.setOnDismissListener(d -> web.destroy());
    dialog.show();
  }

  /** The page links its navigation button to Google Maps directions. */
  static boolean isNavigation(@NonNull Uri uri)
  {
    final String host = uri.getHost();
    final String path = uri.getPath();
    return host != null && path != null && host.endsWith("google.com") && path.startsWith("/maps/dir");
  }
}
