package app.organicmaps.sdk.maplayer.flight;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.RobolectricTestRunner;

@RunWith(RobolectricTestRunner.class)
public class FlightDhvImportTest
{
  // Made-up areas in the shape of the "DHV XML" file.
  private static final String XML = "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<DhvXml>"
                                  + "<GeneratedOn>2026-11-20</GeneratedOn><FlyingSites>"
                                  + "<FlyingSite><SiteID>7</SiteID><SiteName>Testberg</SiteName>"
                                  + "<SiteType>Hanggelände</SiteType><SiteUrl>https://example.org/7</SiteUrl>"
                                  + "<HeightDifferenceMax>900</HeightDifferenceMax><CableCar>Testbahn</CableCar>"
                                  + "<SiteRemarks>Fliegen nur für Vereinsmitglieder.</SiteRemarks>"
                                  + "<Requirements>A-Schein</Requirements><Location><LocationType>1</LocationType>"
                                  + "<LocationName>Startplatz Süd</LocationName><Coordinates>11.5,47.5</Coordinates>"
                                  + "<Altitude>1600</Altitude><Paragliding>true</Paragliding><Directions>67</Directions>"
                                  + "<AccessByCar>true</AccessByCar><AccessByFoot>true</AccessByFoot>"
                                  + "<AccessRemarks>Forstweg</AccessRemarks><SuitabilityPG>A-Schein</SuitabilityPG>"
                                  + "</Location><Location><LocationType>1</LocationType><LocationName>Drachenrampe"
                                  + "</LocationName><Coordinates>11.51,47.5</Coordinates><Paragliding>false"
                                  + "</Paragliding></Location><Location><LocationType>2</LocationType><LocationName>"
                                  + "Talwiese</LocationName><Coordinates>11.52,47.45</Coordinates><Altitude>700"
                                  + "</Altitude></Location></FlyingSite>"
                                  + "<FlyingSite><SiteID>8</SiteID><SiteName>Schleppwiese</SiteName><SiteType>"
                                  + "Schleppgelände</SiteType></FlyingSite>"
                                  + "<FlyingSite><SiteID>9</SiteID><SiteName>Drachenhang</SiteName><SiteType>"
                                  + "Hanggelände</SiteType><Location><LocationType>1</LocationType><Coordinates>"
                                  + "12,47</Coordinates><Paragliding>false</Paragliding></Location></FlyingSite>"
                                  + "</FlyingSites></DhvXml>";

  @Test
  public void readsSlopeSitesWithParaglidingTakeoffs() throws Exception
  {
    final FlightDhvImport.Result r = FlightDhvImport.read(new ByteArrayInputStream(XML.getBytes(StandardCharsets.UTF_8)));
    assertEquals("2026-11-20", r.mGenerated);
    assertEquals(1, r.mAreas);
    assertEquals(1, r.mTakeoffs);
    assertEquals(1, r.mSkippedTowing);
    assertEquals(1, r.mSkippedHangGliding);
    assertEquals(2, r.mSites.size());

    final FlightSite takeoff = r.mSites.get(0);
    assertEquals("Startplatz Süd", takeoff.mName);
    assertEquals(47.5, takeoff.mLat, 0);
    assertEquals(11.5, takeoff.mLon, 0);
    assertEquals(1600, takeoff.mAltitude);
    // Hex 6 and 7 are the sectors centered at 157.5° (SSE) and 180° (S).
    assertEquals((1 << 7) | (1 << 8), takeoff.mDirections);
    assertTrue(takeoff.mRestricted);
    assertFalse(takeoff.mGuestRules);
    assertEquals("A-Schein", takeoff.mRules);
    assertEquals("Forstweg", takeoff.mAccess);
    assertEquals("car,foot", takeoff.mDetails.get(FlightSite.DETAIL_ACCESS_BY));
    assertEquals("900 m", takeoff.mDetails.get(FlightSite.DETAIL_HEIGHT_DIFFERENCE));
    assertEquals("Testbahn", takeoff.mDetails.get(FlightSite.DETAIL_CABLE_CAR));
    assertEquals("https://example.org/7", takeoff.mUrl);
    assertEquals(FlightSite.SOURCE_DHV, takeoff.mSources);

    final FlightSite landing = r.mSites.get(1);
    assertTrue(landing.mLanding);
    assertEquals("Talwiese", landing.mName);
    assertEquals(landing.mId, takeoff.mLinks[0]);
    assertEquals(takeoff.mId, landing.mLinks[0]);
  }

  @Test
  public void directionsOfTheDhv()
  {
    assertEquals(1 << 1, FlightDhvImport.directions("0"));
    assertEquals(1, FlightDhvImport.directions("f"));
    assertEquals(0, FlightDhvImport.directions(""));
  }

  @Test
  public void readsAZipAndRejectsOtherFiles() throws Exception
  {
    final ByteArrayOutputStream zip = new ByteArrayOutputStream();
    try (ZipOutputStream out = new ZipOutputStream(zip))
    {
      out.putNextEntry(new ZipEntry("dhvgelaende_dhvxml_de.xml"));
      out.write(XML.getBytes(StandardCharsets.UTF_8));
      out.closeEntry();
    }
    assertEquals(1, FlightDhvImport.read(new ByteArrayInputStream(zip.toByteArray())).mTakeoffs);

    try
    {
      FlightDhvImport.read(new ByteArrayInputStream("<gpx/>".getBytes(StandardCharsets.UTF_8)));
      fail("no DHV file");
    }
    catch (FlightDhvImport.FormatException e)
    {
      assertEquals(FlightDhvImport.FormatException.Reason.NOT_DHV, e.mReason);
    }
  }

  @Test
  public void keepsTheImportInTheSitesFile() throws Exception
  {
    final FlightDhvImport.Result r = FlightDhvImport.read(new ByteArrayInputStream(XML.getBytes(StandardCharsets.UTF_8)));
    final String json = FlightSources.toFile(r.mSites, r.mGenerated);
    assertEquals("2026-11-20", FlightSources.generated(json));
    final List<FlightSite> read = FlightSources.parseFile(json);
    assertEquals(2, read.size());
    final FlightSite takeoff = read.get(0);
    assertEquals(r.mSites.get(0).mId, takeoff.mId);
    assertEquals(r.mSites.get(0).mDirections, takeoff.mDirections);
    assertTrue(takeoff.mRestricted);
    assertEquals("Testbahn", takeoff.mDetails.get(FlightSite.DETAIL_CABLE_CAR));
    assertEquals(r.mSites.get(1).mId, takeoff.mLinks[0]);
  }

  @Test
  public void dhvSitesReplaceTheSameSitesOfOtherSources()
  {
    final FlightSite dhv = new FlightSite("dhv:1:t0", "A", 47.5, 11.5, false, 1600, 0, FlightSite.SOURCE_DHV,
                                          new double[0]);
    final List<FlightSite> others = new ArrayList<>();
    // 110 m away: the same takeoff.
    others.add(new FlightSite("osm:n1", "A", 47.501, 11.5, false, 1600, 0, FlightSite.SOURCE_OSM, new double[0]));
    // A landing at the same place stays, as does a takeoff 1.1 km away.
    others.add(new FlightSite("osm:n2", "L", 47.5, 11.5, true, 700, 0, FlightSite.SOURCE_OSM, new double[0]));
    others.add(new FlightSite("osm:n3", "B", 47.51, 11.5, false, 1500, 0, FlightSite.SOURCE_OSM, new double[0]));
    final List<FlightSite> left = FlightSources.withoutDuplicates(others, List.of(dhv), 300);
    assertEquals(2, left.size());
    assertEquals("osm:n2", left.get(0).mId);
    assertEquals("osm:n3", left.get(1).mId);
  }
}
