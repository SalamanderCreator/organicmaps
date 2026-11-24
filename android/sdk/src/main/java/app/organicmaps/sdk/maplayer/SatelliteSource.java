package app.organicmaps.sdk.maplayer;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import app.organicmaps.sdk.Framework;

/**
 * Maps&Fly: ready-made imagery for the satellite layer, free for private use.
 * Esri World Imagery (credit "Esri, Maxar, Earthstar Geographics") and Sentinel-2 cloudless 2020
 * by EOX (CC BY-NC-SA 4.0, credit "Sentinel-2 cloudless by EOX IT Services GmbH").
 */
public enum SatelliteSource
{
  ESRI("https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile/{z}/{y}/{x}"),
  SENTINEL("https://tiles.maps.eox.at/wmts/1.0.0/s2cloudless-2020_3857/default/g/{z}/{y}/{x}.jpg");

  @NonNull
  private final String mUrl;

  SatelliteSource(@NonNull String url)
  {
    mUrl = url;
  }

  /** The source of the configured URL, null for a custom one. */
  @Nullable
  public static SatelliteSource current()
  {
    final String url = Framework.nativeGetBackgroundTilesUrl();
    for (SatelliteSource source : values())
      if (source.mUrl.equals(url))
        return source;
    return null;
  }

  public void apply(boolean enabled)
  {
    applyWithAreaOpacity(enabled, Framework.nativeGetBackgroundTilesAreaOpacity());
  }

  public void applyWithAreaOpacity(boolean enabled, int areaOpacityPct)
  {
    Framework.nativeSetBackgroundTiles(enabled, mUrl, Framework.nativeGetBackgroundTilesCacheSizeMB(), areaOpacityPct);
  }
}
