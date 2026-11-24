package app.organicmaps.sdk.maplayer.radar;

import androidx.annotation.MainThread;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.util.concurrency.ThreadPool;
import app.organicmaps.sdk.util.concurrency.UiThread;
import app.organicmaps.sdk.util.log.Logger;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

/**
 * Maps&Fly: the colour scales of the rain radar sources. RainViewer documents its scheme, the DWD legend is read
 * from its WMS, so it always matches the tiles.
 */
public final class RadarLegend
{
  private static final String TAG = RadarLegend.class.getSimpleName();

  // GeoServer legend of the radar layer as JSON (colour map entries with labels in mm/h).
  private static final String DWD_LEGEND_URL =
      "https://maps.dwd.de/geoserver/dwd/wms?service=WMS&version=1.3.0&request=GetLegendGraphic"
      + "&format=application/json&layer=dwd:Niederschlagsradar";

  // RainViewer colour scheme 2 "Universal Blue" (rainviewer.com/files/rainviewer_api_colors_table.csv),
  // every 5 dBZ from 15 dBZ (about 0.3 mm/h) to 55 dBZ (about 100 mm/h, hail).
  public static final int[] RAINVIEWER_RAIN = {0xFF88DDEE, 0xFF00A3E0, 0xFF0077AA, 0xFF005588, 0xFFFFEE00,
                                               0xFFFFAA00, 0xFFFF4400, 0xFFC10000, 0xFFFFAAFF};
  // Snow of the same scheme, every 10 dBZ from 10 to 60 dBZ.
  public static final int[] RAINVIEWER_SNOW = {0xFFBFFFFF, 0xFF7FBFFF, 0xFF4F8FFF, 0xFF2F6FFF, 0xFF0F4FFF, 0xFF002FFF};

  public static final class Entry
  {
    public final int mColor;
    @NonNull
    public final String mLabel;

    Entry(int color, @NonNull String label)
    {
      mColor = color;
      mLabel = label;
    }
  }

  public interface Listener
  {
    /** Empty when the legend could not be read. */
    void onLegend(@NonNull List<Entry> entries);
  }

  @Nullable
  private static List<Entry> sDwdLegend;

  private RadarLegend() {}

  @MainThread
  public static void getDwdLegend(@NonNull Listener listener)
  {
    if (sDwdLegend != null)
    {
      listener.onLegend(sDwdLegend);
      return;
    }
    ThreadPool.getWorker().execute(() -> {
      List<Entry> entries = Collections.emptyList();
      try
      {
        final String body = RainRadar.download(DWD_LEGEND_URL);
        if (body != null)
          entries = parseGeoServerLegend(body);
      }
      catch (Exception e)
      {
        Logger.e(TAG, "Failed to read the DWD legend", e);
      }
      final List<Entry> result = entries;
      UiThread.run(() -> {
        // A failed read is tried again the next time.
        if (!result.isEmpty())
          sDwdLegend = result;
        listener.onLegend(result);
      });
    });
  }

  /** The visible colour map entries of a GeoServer JSON legend, in their order. */
  @NonNull
  static List<Entry> parseGeoServerLegend(@NonNull String json) throws JSONException
  {
    final List<Entry> entries = new ArrayList<>();
    collect(new JSONObject(json), entries);
    return entries;
  }

  private static void collect(@NonNull Object node, @NonNull List<Entry> entries) throws JSONException
  {
    if (node instanceof JSONArray array)
    {
      for (int i = 0; i < array.length(); ++i)
        collect(array.get(i), entries);
      return;
    }
    if (!(node instanceof JSONObject object))
      return;

    final String color = object.optString("color", "");
    if (color.matches("#[0-9A-Fa-f]{6}") && object.has("label"))
    {
      final double opacity = object.optDouble("opacity", 1.0);
      final String label = object.optString("label", "").trim();
      if (opacity > 0 && !label.isEmpty())
        entries.add(new Entry(0xFF000000 | Integer.parseInt(color.substring(1), 16), label));
      return;
    }

    final JSONArray names = object.names();
    if (names == null)
      return;
    for (int i = 0; i < names.length(); ++i)
      collect(object.get(names.getString(i)), entries);
  }
}
