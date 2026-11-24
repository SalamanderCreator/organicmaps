package app.organicmaps.sdk.maplayer.radar;

import static org.junit.Assert.assertEquals;

import java.util.List;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class RadarLegendTest
{
  @Test
  public void readsTheVisibleColourMapEntriesInOrder() throws Exception
  {
    // The shape of a GeoServer GetLegendGraphic JSON answer for a raster layer.
    final String json =
        "{\"Legend\":[{\"layerName\":\"Niederschlagsradar\",\"rules\":[{\"symbolizers\":[{\"Raster\":{"
        + "\"colormap\":{\"type\":\"intervals\",\"entries\":["
        + "{\"label\":\"ausserhalb\",\"color\":\"#C0C0C0\",\"opacity\":\"0.0\",\"quantity\":\"-1\"},"
        + "{\"label\":\"0.1 - 0.4\",\"color\":\"#B4F0FA\",\"quantity\":\"0.4\"},"
        + "{\"label\":\"> 150\",\"color\":\"#ff00ff\",\"opacity\":\"1.0\",\"quantity\":\"9999\"}]}}}]}]}]}";

    final List<RadarLegend.Entry> entries = RadarLegend.parseGeoServerLegend(json);

    assertEquals(2, entries.size());
    assertEquals(0xFFB4F0FA, entries.get(0).mColor);
    assertEquals("0.1 - 0.4", entries.get(0).mLabel);
    assertEquals(0xFFFF00FF, entries.get(1).mColor);
    assertEquals("> 150", entries.get(1).mLabel);
  }
}
