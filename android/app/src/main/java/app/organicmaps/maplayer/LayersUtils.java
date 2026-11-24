package app.organicmaps.maplayer;

import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.maplayer.Mode;
import java.util.ArrayList;
import java.util.List;

public class LayersUtils
{
  public static List<Mode> getAvailableLayers()
  {
    List<Mode> availableLayers = new ArrayList<>();
    availableLayers.add(Mode.OUTDOORS);
    availableLayers.add(Mode.ISOLINES);
    availableLayers.add(Mode.HIKING);
    availableLayers.add(Mode.CYCLING);
    // Maps&Fly hides the subway layer; traffic is not offered upstream either.
    // The Satellite toggle is a quick on/off for an already-configured source; configuration lives in
    // Settings, so only offer the button once a server URL is set.
    if (!Framework.nativeGetBackgroundTilesUrl().isEmpty())
      availableLayers.add(Mode.SATELLITE);
    availableLayers.add(Mode.RAIN_RADAR);
    return availableLayers;
  }
}
