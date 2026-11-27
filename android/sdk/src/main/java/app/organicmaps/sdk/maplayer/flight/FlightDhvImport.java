package app.organicmaps.sdk.maplayer.flight;

import androidx.annotation.NonNull;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import javax.xml.parsers.DocumentBuilder;
import javax.xml.parsers.DocumentBuilderFactory;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.Node;
import org.w3c.dom.NodeList;

/**
 * Maps&Fly: reads the "DHV XML" site file that a pilot downloads at dhv.de, for private use on this phone
 * only. Its terms forbid passing the data on, so it never goes into the repository or the APK. The logic
 * is the one of flugwetter/karte.html: only slope sites (Hanggelände) with a paragliding takeoff, with
 * their landings, 16 takeoff sectors and the rules for members and guest pilots found in the texts.
 */
public final class FlightDhvImport
{
  public static final String FILE = "flight_dhv.json";

  // Rules for members only, or for guest pilots: the DHV data has no field for them, so the texts are read.
  private static final Pattern RX_STRICT = Pattern.compile(
      "(nur|ausschlie(ß|ss)lich)\\s+(für\\s+)?(vereins|club|klub)?mitglied|keine\\s+gast(flieger|piloten)"
          + "|members\\s+only|riservato\\s+ai\\s+soci|réservé\\s+aux\\s+membres",
      Pattern.CASE_INSENSITIVE | Pattern.UNICODE_CASE);
  private static final Pattern RX_SOFT = Pattern.compile(
      "vereinsmitglied|clubmitglied|tagesmitgliedschaft|gastflieger|gastpiloten|privatgelände"
          + "|einweisung\\s+(ist\\s+)?(erforderlich|pflicht|notwendig)|nur\\s+nach\\s+(absprache|anmeldung)"
          + "|anmeldung\\s+(für\\s+gast\\w*\\s+)?erforderlich",
      Pattern.CASE_INSENSITIVE | Pattern.UNICODE_CASE);

  public static final class Result
  {
    @NonNull
    public final List<FlightSite> mSites = new ArrayList<>();
    /** The date of the DHV file. */
    @NonNull
    public String mGenerated = "";
    public int mAreas;
    public int mTakeoffs;
    /** Towing, ultralight and landing-only areas, and areas for hang gliders only. */
    public int mSkippedTowing;
    public int mSkippedHangGliding;
  }

  /** Thrown for a file that is no DHV XML. */
  public static final class FormatException extends Exception
  {
    public enum Reason
    {
      NO_XML_IN_ZIP,
      INVALID_XML,
      NOT_DHV
    }

    @NonNull
    public final Reason mReason;

    FormatException(@NonNull Reason reason)
    {
      super(reason.name());
      mReason = reason;
    }
  }

  private FlightDhvImport() {}

  /** Reads a .xml file or a .zip file with one. */
  @NonNull
  public static Result read(@NonNull InputStream in) throws Exception
  {
    final byte[] data = readAll(in);
    // A zip file starts with "PK".
    if (data.length > 1 && data[0] == 'P' && data[1] == 'K')
    {
      try (ZipInputStream zip = new ZipInputStream(new ByteArrayInputStream(data)))
      {
        for (ZipEntry entry; (entry = zip.getNextEntry()) != null;)
          if (!entry.isDirectory() && entry.getName().toLowerCase(Locale.ROOT).endsWith(".xml"))
            return parseXml(new ByteArrayInputStream(readAll(zip)));
      }
      throw new FormatException(FormatException.Reason.NO_XML_IN_ZIP);
    }
    return parseXml(new ByteArrayInputStream(data));
  }

  @NonNull
  static Result parseXml(@NonNull InputStream xml) throws Exception
  {
    final DocumentBuilderFactory factory = DocumentBuilderFactory.newInstance();
    factory.setExpandEntityReferences(false);
    try
    {
      factory.setFeature("http://apache.org/xml/features/disallow-doctype-decl", true);
    }
    catch (Exception ignored)
    {
      // Not every parser knows the feature, the DHV file has no DTD anyway.
    }
    final DocumentBuilder builder = factory.newDocumentBuilder();
    final Document doc;
    try
    {
      doc = builder.parse(xml);
    }
    catch (Exception e)
    {
      throw new FormatException(FormatException.Reason.INVALID_XML);
    }
    final Element root = doc.getDocumentElement();
    if (!"DhvXml".equals(root.getNodeName()))
      throw new FormatException(FormatException.Reason.NOT_DHV);

    final Result result = new Result();
    for (Node n = root.getFirstChild(); n != null; n = n.getNextSibling())
      if ("GeneratedOn".equals(n.getNodeName()))
        result.mGenerated = n.getTextContent().trim();

    final NodeList areas = root.getElementsByTagName("FlyingSite");
    for (int i = 0; i < areas.getLength(); ++i)
    {
      final Element s = (Element) areas.item(i);
      if (!tx(s, "SiteType").toLowerCase(Locale.ROOT).contains("hanggelände"))
      {
        ++result.mSkippedTowing;
        continue;
      }
      final String areaId = "dhv:" + tx(s, "SiteID");
      final String areaName = tx(s, "SiteName");
      final List<FlightSite> takeoffs = new ArrayList<>();
      final List<FlightSite> landings = new ArrayList<>();
      final List<String> takeoffTexts = new ArrayList<>();
      final NodeList locations = s.getElementsByTagName("Location");
      for (int k = 0; k < locations.getLength(); ++k)
      {
        final Element loc = (Element) locations.item(k);
        final String type = tx(loc, "LocationType");
        final String[] coords = tx(loc, "Coordinates").split(",");
        if (coords.length < 2)
          continue;
        final double lon;
        final double lat;
        try
        {
          lon = Double.parseDouble(coords[0].trim());
          lat = Double.parseDouble(coords[1].trim());
        }
        catch (NumberFormatException e)
        {
          continue;
        }
        final String name = tx(loc, "LocationName");
        final int altitude = number(tx(loc, "Altitude"));
        final String remarks = tx(loc, "LocationRemarks");
        if ("1".equals(type))
        {
          // Hang gliding only takeoffs.
          if (!"true".equals(tx(loc, "Paragliding")))
            continue;
          final FlightSite t = new FlightSite(areaId + ":t" + k, name.isEmpty() ? areaName : name, lat, lon, false,
                                              altitude, directions(tx(loc, "Directions")),
                                              FlightSite.SOURCE_DHV, new double[0]);
          t.mDescription = join("\n", tx(s, "SiteRemarks"), remarks);
          t.mRules = tx(s, "Requirements");
          final List<String> access = new ArrayList<>();
          if ("true".equals(tx(loc, "AccessByCar")))
            access.add(FlightSite.ACCESS_CAR);
          if ("true".equals(tx(loc, "AccessByPublicTransport")))
            access.add(FlightSite.ACCESS_PUBLIC_TRANSPORT);
          if ("true".equals(tx(loc, "AccessByFoot")))
            access.add(FlightSite.ACCESS_FOOT);
          put(t, FlightSite.DETAIL_ACCESS_BY, String.join(",", access));
          t.mAccess = tx(loc, "AccessRemarks");
          final String url = tx(s, "SiteUrl");
          t.mUrl = url.isEmpty() ? null : url;
          final int heightDifference = number(tx(s, "HeightDifferenceMax"));
          if (heightDifference != FlightSite.NO_ALTITUDE)
            t.mDetails.put(FlightSite.DETAIL_HEIGHT_DIFFERENCE, heightDifference + " m");
          put(t, FlightSite.DETAIL_SUITABILITY, tx(loc, "SuitabilityPG"));
          put(t, FlightSite.DETAIL_CABLE_CAR, tx(s, "CableCar"));
          takeoffTexts.add(remarks + " " + tx(loc, "AccessRemarks"));
          takeoffs.add(t);
        }
        else if ("2".equals(type))
        {
          final FlightSite l = new FlightSite(areaId + ":l" + k, name.isEmpty() ? areaName : name, lat, lon, true,
                                              altitude, 0, FlightSite.SOURCE_DHV, new double[0]);
          l.mDescription = remarks;
          final String url = tx(s, "SiteUrl");
          l.mUrl = url.isEmpty() ? null : url;
          landings.add(l);
        }
        // Type 3, a towing track, is left out.
      }
      if (takeoffs.isEmpty())
      {
        ++result.mSkippedHangGliding;
        continue;
      }

      final String all = String.join(" ", tx(s, "SiteRemarks"), tx(s, "Requirements"), String.join(" ", takeoffTexts));
      final boolean restricted = RX_STRICT.matcher(all).find();
      final boolean guestRules = !restricted && RX_SOFT.matcher(all).find();
      final List<String> webcams = new ArrayList<>();
      for (String cam : new String[] {"WebCam1", "WebCam2", "WebCam3"})
        if (!tx(s, cam).isEmpty())
          webcams.add(tx(s, cam));
      final String[] landingIds = new String[landings.size()];
      for (int k = 0; k < landings.size(); ++k)
        landingIds[k] = landings.get(k).mId;
      final String[] takeoffIds = new String[takeoffs.size()];
      for (int k = 0; k < takeoffs.size(); ++k)
        takeoffIds[k] = takeoffs.get(k).mId;

      for (FlightSite t : takeoffs)
      {
        t.mRestricted = restricted;
        t.mGuestRules = guestRules;
        t.mLinks = landingIds;
        final List<String> others = new ArrayList<>();
        for (FlightSite o : takeoffs)
          if (o != t)
            others.add(o.mName + (o.hasAltitude() ? " (" + o.mAltitude + " m)" : ""));
        put(t, FlightSite.DETAIL_OTHER_TAKEOFFS, String.join("; ", others));
        put(t, FlightSite.DETAIL_INFO, tx(s, "SiteInformation"));
        put(t, FlightSite.DETAIL_WEATHER, tx(s, "WeatherInfo"));
        put(t, FlightSite.DETAIL_WEBCAMS, String.join(" · ", webcams));
        put(t, FlightSite.DETAIL_HOLDER, tx(s, "DECertificationHolder"));
        put(t, FlightSite.DETAIL_CONTACT, tx(s, "SiteContact"));
        result.mSites.add(t);
      }
      for (FlightSite l : landings)
      {
        l.mLinks = takeoffIds;
        result.mSites.add(l);
      }
      ++result.mAreas;
      result.mTakeoffs += takeoffs.size();
    }
    return result;
  }

  /** DHV directions are hex digits, 0 for NNE up to F for N, as sector i is centered at (i + 1) * 22.5°. */
  static int directions(@NonNull String hex)
  {
    int mask = 0;
    for (char c : hex.toUpperCase(Locale.ROOT).toCharArray())
    {
      final int i = Character.digit(c, 16);
      if (i >= 0)
        mask |= 1 << ((i + 1) % 16);
    }
    return mask;
  }

  @NonNull
  private static String tx(@NonNull Element parent, @NonNull String tag)
  {
    final NodeList nodes = parent.getElementsByTagName(tag);
    return nodes.getLength() == 0 ? "" : nodes.item(0).getTextContent().trim();
  }

  private static int number(@NonNull String value)
  {
    try
    {
      final int n = (int) Math.round(Double.parseDouble(value));
      return n == 0 ? FlightSite.NO_ALTITUDE : n;
    }
    catch (NumberFormatException e)
    {
      return FlightSite.NO_ALTITUDE;
    }
  }

  private static void put(@NonNull FlightSite site, @NonNull String key, @NonNull String value)
  {
    if (!value.isEmpty())
      site.mDetails.put(key, value);
  }

  @NonNull
  private static String join(@NonNull String separator, @NonNull String... parts)
  {
    final List<String> nonEmpty = new ArrayList<>();
    for (String p : parts)
      if (!p.trim().isEmpty())
        nonEmpty.add(p.trim());
    return String.join(separator, nonEmpty);
  }

  @NonNull
  private static byte[] readAll(@NonNull InputStream in) throws Exception
  {
    final ByteArrayOutputStream out = new ByteArrayOutputStream();
    final byte[] buffer = new byte[65536];
    int n;
    while ((n = in.read(buffer)) > 0)
      out.write(buffer, 0, n);
    return out.toByteArray();
  }
}
