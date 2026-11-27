#include "map/flight_layer.hpp"

#include "drape_frontend/drape_api.hpp"
#include "drape_frontend/visual_params.hpp"

#include "geometry/mercator.hpp"

#include "base/math.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace
{
std::string_view constexpr kLabelPrefix = "flight_label_";

// The normal map: a colored circle with a white symbol, like the other map icons.
dp::Color const kIconHaloColor(255, 255, 255, 153);
dp::Color const kTakeoffCircle(47, 126, 194, 255);
dp::Color const kLandingCircle(30, 158, 143, 255);
dp::Color const kWhite(255, 255, 255, 255);
// Sites for members only are gray, on the map and in the layer.
dp::Color const kRestrictedCircle(160, 164, 168, 255);

// The flight preparation layer.
dp::Color const kDisc(255, 255, 255, 215);
dp::Color const kRing(29, 39, 51, 255);
dp::Color const kSector(40, 170, 70, 235);
// A thin light line between neighbouring directions.
dp::Color const kSectorOutline(235, 250, 235, 255);
dp::Color const kWingGreen(40, 170, 70, 255);
dp::Color const kWingBlue(30, 110, 220, 255);
dp::Color const kWingBlack(25, 25, 30, 255);
dp::Color const kGliderLines(70, 75, 85, 255);
dp::Color const kPilot(20, 20, 25, 255);
dp::Color const kPole(120, 128, 138, 255);
dp::Color const kSockRed(229, 51, 42, 255);
dp::Color const kSockOutline(150, 40, 35, 255);
dp::Color const kLabel(70, 70, 70, 255);

struct Palette
{
  dp::Color m_ring, m_sector, m_sectorOutline, m_wingOuter, m_wingMiddle, m_wingCenter, m_gliderLines, m_pilot,
      m_sockRed, m_sockOutline;
};
Palette const kColors{kRing,      kSector,      kSectorOutline, kWingGreen,   kWingBlue,
                      kWingBlack, kGliderLines, kPilot,         kSockRed,     kSockOutline};
Palette const kGrays{{150, 154, 160, 255}, {165, 168, 172, 220}, {242, 242, 242, 255}, {185, 188, 192, 255},
                     {145, 148, 152, 255}, {100, 103, 108, 255}, {160, 163, 168, 255}, {110, 113, 118, 255},
                     {160, 163, 168, 255}, {120, 123, 128, 255}};
// Turquoise: bluer than the green of meadows and parks, greener than the blue of lakes.
dp::Color const kAreaOutline(0, 165, 155, 255);
dp::Color const kAreaFill(0, 190, 175, 80);
// Takeoff areas in the blue of the takeoff icons.
dp::Color const kTakeoffAreaOutline(47, 126, 194, 255);
dp::Color const kTakeoffAreaFill(47, 126, 194, 70);
// Obstacles are thin or translucent, so that the map still shows the kind of line and its name below: power
// lines dotted red, aerial lifts dashed magenta, motorways a wide orange band, wind turbines red dots.
dp::Color const kPowerLine(230, 30, 30, 220);
dp::Color const kAerialLift(200, 0, 130, 220);
dp::Color const kMotorway(255, 120, 0, 110);
dp::Color const kWindTurbine(230, 30, 30, 220);

// Keeps the geometry small on a crowded screen.
size_t constexpr kMaxSymbols = 600;
size_t constexpr kMaxLabels = 150;
size_t constexpr kMaxObstacleSites = 40;
size_t constexpr kMaxObstacleLines = 600;

// Symbol sizes in dp. The map icons match the small and medium POI icons of the styles (12 and 18 dp with a
// light halo), the glyphs were drawn for kGlyphRadius.
double constexpr kSmallIconRadius = 6.0;
double constexpr kMediumIconRadius = 9.0;
double constexpr kIconHalo = 0.75;
double constexpr kGlyphRadius = 9.5;
double constexpr kDiscRadius = 14.5;
// The takeoff directions are a green wreath around the disc, so the paraglider does not hide them.
double constexpr kSectorRadius = 24.0;
// The number of takeoffs in one symbol, at the top right of the disc.
double constexpr kBadgeOffset = 11.5;
double constexpr kBadgeRadius = 6.5;

using Rings = std::vector<std::vector<m2::PointD>>;

int GetZoom(ScreenBase const & screen)
{
  return static_cast<int>(df::GetZoomLevel(screen.GetScale()));
}

// Points of a symbol in dp around |center|, y up.
class Symbol
{
public:
  Symbol(m2::PointD const & center, double unit) : m_center(center), m_unit(unit) {}

  m2::PointD P(double dx, double dy) const { return {m_center.x + dx * m_unit, m_center.y + dy * m_unit}; }

  std::vector<m2::PointD> Arc(double cx, double cy, double rx, double ry, double fromDeg, double toDeg) const
  {
    size_t constexpr kSteps = 10;
    std::vector<m2::PointD> points;
    for (size_t i = 0; i <= kSteps; ++i)
    {
      double const a = math::DegToRad(fromDeg + (toDeg - fromDeg) * static_cast<double>(i) / kSteps);
      points.push_back(P(cx + rx * std::cos(a), cy + ry * std::sin(a)));
    }
    return points;
  }

private:
  m2::PointD m_center;
  double m_unit;
};
}  // namespace

FlightLayer::FlightLayer(df::DrapeApi & drapeApi) : m_drapeApi(drapeApi) {}

void FlightLayer::SetEnabled(bool enabled)
{
  if (m_enabled == enabled)
    return;
  m_enabled = enabled;
  Redraw();
}

void FlightLayer::SetSites(std::vector<Site> && sites)
{
  m_sites = std::move(sites);
  m_drawnClusters.clear();
  Redraw();
}

void FlightLayer::SetCables(std::vector<std::vector<m2::PointD>> && cables)
{
  m_cables.clear();
  m_cableRects.clear();
  for (auto & cable : cables)
  {
    if (cable.size() < 2)
      continue;
    m2::RectD rect;
    for (auto const & p : cable)
      rect.Add(p);
    m_cableRects.push_back(rect);
    m_cables.push_back(std::move(cable));
  }
  Redraw();
}

void FlightLayer::SetViewport(ScreenBase const & screen)
{
  m_screen = screen;
  bool needsRedraw = GetZoom(screen) != m_drawnZoom;
  if (!needsRedraw)
  {
    // Symbols are built in the size of the current zoom and for the area around the screen.
    double const scaleRatio = screen.GetScale() / m_drawnScale;
    needsRedraw = !m_drawnRect.IsRectInside(screen.ClipRect()) || scaleRatio < 0.8 || scaleRatio > 1.25;
  }
  if (needsRedraw)
    Redraw();
}

std::optional<FlightLayer::Hit> FlightLayer::HitTest(m2::PointD const & point, ScreenBase const & screen,
                                                    double tolerancePx) const
{
  int const zoom = GetZoom(screen);
  m2::PointD const tapPx = screen.GtoP(point);
  double bestSqrPx = tolerancePx * tolerancePx;
  std::optional<Hit> best;
  Site const * inArea = nullptr;

  // In the layer takeoffs are tapped as they are drawn, maybe several in one symbol.
  if (m_enabled)
  {
    std::vector<Cluster> computed;
    bool const drawn = m_drawnZoom == zoom && m_drawnRect.IsPointInside(point);
    if (!drawn)
    {
      m2::RectD rect = screen.ClipRect();
      rect.Scale(3.0);
      computed = MakeTakeoffClusters(rect, zoom, screen.GetScale());
    }
    for (auto const & cluster : drawn ? m_drawnClusters : computed)
    {
      double const sqrPx = screen.GtoP(cluster.m_point).SquaredLength(tapPx);
      if (sqrPx > bestSqrPx)
        continue;
      bestSqrPx = sqrPx;
      if (cluster.m_count > 1)
        best = Hit{{}, cluster.m_point};
      else
        best = Hit{m_sites[cluster.m_first].m_id, {}};
    }
  }

  for (auto const & site : m_sites)
  {
    if (!IsVisible(site, zoom))
      continue;
    if (!m_enabled || site.m_type == Site::Type::Landing)
    {
      double const sqrPx = screen.GtoP(site.m_point).SquaredLength(tapPx);
      if (sqrPx <= bestSqrPx)
      {
        bestSqrPx = sqrPx;
        best = Hit{site.m_id, {}};
      }
    }
    if (!inArea && site.m_area.size() > 2)
    {
      // Even-odd rule on the ring.
      bool inside = false;
      auto const & ring = site.m_area;
      for (size_t i = 0, j = ring.size() - 1; i < ring.size(); j = i++)
      {
        if ((ring[i].y > point.y) != (ring[j].y > point.y) &&
            point.x < (ring[j].x - ring[i].x) * (point.y - ring[i].y) / (ring[j].y - ring[i].y) + ring[i].x)
        {
          inside = !inside;
        }
      }
      if (inside)
        inArea = &site;
    }
  }
  if (best)
    return best;
  if (inArea)
    return Hit{inArea->m_id, {}};
  return {};
}

std::vector<FlightLayer::Cluster> FlightLayer::MakeTakeoffClusters(m2::RectD const & rect, int zoom,
                                                                   double scale) const
{
  // Symbols overlap when their discs do.
  double const distance = m_enabled && zoom < kClustersMaxZoom ? 2 * kDiscRadius * m_visualScale * scale : 0;
  auto const cell = [distance](m2::PointD const & p, int dx, int dy)
  {
    auto const cx = static_cast<int64_t>(std::floor(p.x / distance)) + dx;
    auto const cy = static_cast<int64_t>(std::floor(p.y / distance)) + dy;
    return (static_cast<uint64_t>(cx) << 32) ^ static_cast<uint32_t>(cy);
  };

  std::vector<Cluster> clusters;
  std::vector<m2::PointD> sums;
  // Clusters by the cell of their first takeoff.
  std::unordered_map<uint64_t, std::vector<size_t>> grid;
  for (size_t i = 0; i < m_sites.size(); ++i)
  {
    auto const & site = m_sites[i];
    if (site.m_type != Site::Type::Takeoff || !IsVisible(site, zoom) || !rect.IsPointInside(site.m_point))
      continue;
    std::optional<size_t> target;
    if (distance > 0)
    {
      double bestSqr = distance * distance;
      for (int dx = -1; dx <= 1; ++dx)
      {
        for (int dy = -1; dy <= 1; ++dy)
        {
          auto const it = grid.find(cell(site.m_point, dx, dy));
          if (it == grid.end())
            continue;
          for (size_t const c : it->second)
          {
            double const sqr = m_sites[clusters[c].m_first].m_point.SquaredLength(site.m_point);
            if (sqr < bestSqr)
            {
              bestSqr = sqr;
              target = c;
            }
          }
        }
      }
    }
    if (!target)
    {
      target = clusters.size();
      if (distance > 0)
        grid[cell(site.m_point, 0, 0)].push_back(*target);
      clusters.push_back({});
      clusters.back().m_first = i;
      sums.emplace_back(0, 0);
    }
    auto & cluster = clusters[*target];
    ++cluster.m_count;
    cluster.m_directions |= site.m_directions;
    cluster.m_restricted = cluster.m_restricted && site.m_restricted;
    sums[*target] += site.m_point;
  }
  for (size_t c = 0; c < clusters.size(); ++c)
    clusters[c].m_point = sums[c] / static_cast<double>(clusters[c].m_count);
  return clusters;
}

// static
std::vector<std::vector<m2::PointD>> FlightLayer::MakeSectors(m2::PointD const & center, uint16_t directions,
                                                              double radius, double innerRadius)
{
  std::vector<std::vector<m2::PointD>> rings;
  if (directions == 0)
    return rings;

  // Directions of 8 winds (only even bits, like ParaglidingEarth) are 45 degrees wide, of 16 winds (like
  // the DHV) 22.5 degrees, so that neighbouring directions always touch.
  bool const sixteenWinds = (directions & 0xAAAA) != 0;
  double const halfWidth = sixteenWinds ? 11.25 : 22.5;
  int constexpr kSteps = 6;

  auto const at = [&center](double azimuthDeg, double r)
  {
    double const a = math::DegToRad(azimuthDeg);
    return m2::PointD(center.x + r * std::sin(a), center.y + r * std::cos(a));
  };
  for (int i = 0; i < 16; ++i)
  {
    if ((directions & (1 << i)) == 0)
      continue;
    double const from = i * 22.5 - halfWidth;
    double const to = i * 22.5 + halfWidth;
    std::vector<m2::PointD> ring;
    for (int k = 0; k <= kSteps; ++k)
      ring.push_back(at(from + (to - from) * k / kSteps, radius));
    if (innerRadius > 0)
    {
      for (int k = kSteps; k >= 0; --k)
        ring.push_back(at(from + (to - from) * k / kSteps, innerRadius));
    }
    else
    {
      ring.insert(ring.begin(), center);
      ring.push_back(center);
    }
    if (ring.front() != ring.back())
      ring.push_back(ring.front());
    rings.push_back(std::move(ring));
  }
  return rings;
}

// static
std::vector<m2::PointD> FlightLayer::MakeCircle(m2::PointD const & center, double radius, size_t pointsCount)
{
  std::vector<m2::PointD> points;
  points.reserve(pointsCount + 1);
  for (size_t i = 0; i <= pointsCount; ++i)
  {
    double const a = 2 * math::pi * static_cast<double>(i) / pointsCount;
    points.emplace_back(center.x + radius * std::sin(a), center.y + radius * std::cos(a));
  }
  return points;
}

bool FlightLayer::IsVisible(Site const & site, int zoom) const
{
  bool const takeoff = site.m_type == Site::Type::Takeoff;
  if (m_enabled)
    return zoom >= (takeoff ? kLayerTakeoffsZoom : kLayerLandingsZoom);
  return zoom >= (takeoff ? kTakeoffsZoom : kLandingsZoom);
}

void FlightLayer::Add(std::string const & id, df::DrapeApiLineData const & data)
{
  m_drapeApi.AddLine(id, data);
  m_drawnIds.insert(id);
}

void FlightLayer::Redraw()
{
  if (!m_screen)
    return;

  auto const previousIds = std::move(m_drawnIds);
  m_drawnIds.clear();
  int const zoom = GetZoom(*m_screen);
  m_drawnZoom = zoom;
  m_drawnScale = m_screen->GetScale();
  m_drawnRect = m_screen->ClipRect();
  m_drawnRect.Scale(3.0);

  // Bottom up: areas, obstacles, symbols, labels.
  DrawAreas(m_drawnRect, zoom);
  if (m_enabled && zoom >= kObstaclesZoom)
    DrawObstacles();
  double const unit = m_drawnScale * m_visualScale;
  m_drawnClusters.clear();
  if (m_enabled)
    DrawLayerSymbols(m_drawnRect, zoom, unit);
  else
    DrawMapIcons(m_drawnRect, zoom, unit);
  DrawLabels(m_drawnRect, zoom);

  for (auto const & id : previousIds)
    if (!m_drawnIds.contains(id))
      m_drapeApi.RemoveLine(id);
}

namespace
{
class Entries
{
public:
  Entries(std::function<void(std::string const &, df::DrapeApiLineData const &)> add, double vs)
    : m_add(std::move(add)), m_vs(vs)
  {}

  void Fills(std::string_view name, Rings && rings, dp::Color const & color)
  {
    if (rings.empty())
      return;
    df::DrapeApiLineData data({}, color);
    data.FillRings(std::move(rings), color);
    m_add(std::string("flight_") + std::string(name), data);
  }

  // |widthDp| in density-independent pixels, the DrapeApi takes pixels.
  void Lines(std::string_view name, Rings && lines, dp::Color const & color, double widthDp)
  {
    if (lines.empty())
      return;
    df::DrapeApiLineData data({}, color);
    data.Polylines(std::move(lines)).Width(static_cast<float>(widthDp * m_vs));
    m_add(std::string("flight_") + std::string(name), data);
  }

private:
  std::function<void(std::string const &, df::DrapeApiLineData const &)> m_add;
  double m_vs;
};
}  // namespace

void FlightLayer::DrawMapIcons(m2::RectD const & rect, int zoom, double unit)
{
  Rings halos, takeoffCircles, landingCircles, restrictedCircles;
  // Glyph lines of small and medium icons have different widths.
  Rings canopies[2], risers[2], pilots, poles[2], socks;
  size_t count = 0;
  for (auto const & site : m_sites)
  {
    if (!IsVisible(site, zoom) || !rect.IsPointInside(site.m_point) || ++count > kMaxSymbols)
      continue;
    bool const takeoff = site.m_type == Site::Type::Takeoff;
    bool const medium = zoom >= (takeoff ? kMediumTakeoffsZoom : kMediumLandingsZoom);
    double const radius = medium ? kMediumIconRadius : kSmallIconRadius;
    double const k = (radius - kIconHalo) / kGlyphRadius;
    halos.push_back(MakeCircle(site.m_point, radius * unit));
    Symbol const s(site.m_point, unit * k);
    if (takeoff)
    {
      (site.m_restricted ? restrictedCircles : takeoffCircles)
          .push_back(MakeCircle(site.m_point, (radius - kIconHalo) * unit));
      canopies[medium].push_back(s.Arc(0, 0.2, 6.0, 4.0, 12, 168));
      risers[medium].push_back({s.P(-5.8, 1.0), s.P(0, -4.2), s.P(5.8, 1.0)});
      pilots.push_back(MakeCircle(s.P(0, -5.0), 1.6 * k * unit, 8));
    }
    else
    {
      (site.m_restricted ? restrictedCircles : landingCircles)
          .push_back(MakeCircle(site.m_point, (radius - kIconHalo) * unit));
      poles[medium].push_back({s.P(-4.5, -6.0), s.P(-4.5, 5.5)});
      socks.push_back({s.P(-4.5, 5.5), s.P(5.5, 4.3), s.P(5.5, 1.7), s.P(-4.5, 0.5), s.P(-4.5, 5.5)});
    }
  }

  Entries e([this](std::string const & id, df::DrapeApiLineData const & d) { Add(id, d); }, m_visualScale);
  e.Fills("icon_halos", std::move(halos), kIconHaloColor);
  e.Fills("icon_takeoffs", std::move(takeoffCircles), kTakeoffCircle);
  e.Fills("icon_landings", std::move(landingCircles), kLandingCircle);
  e.Fills("icon_restricted", std::move(restrictedCircles), kRestrictedCircle);
  for (int m = 0; m < 2; ++m)
  {
    double const k = ((m ? kMediumIconRadius : kSmallIconRadius) - kIconHalo) / kGlyphRadius;
    std::string const size = m ? "_m" : "_s";
    e.Lines("icon_canopies" + size, std::move(canopies[m]), kWhite, 2.0 * k);
    e.Lines("icon_risers" + size, std::move(risers[m]), kWhite, 1.1 * k);
    e.Lines("icon_poles" + size, std::move(poles[m]), kWhite, 1.5 * k);
  }
  e.Fills("icon_pilots", std::move(pilots), kWhite);
  e.Fills("icon_socks", std::move(socks), kWhite);
}

void FlightLayer::DrawLayerSymbols(m2::RectD const & rect, int zoom, double unit)
{
  // Colored, and gray for members only.
  struct Parts
  {
    Rings discs, rings, sectors, sectorOutlines;
    // The wing has five cells: green, blue, black, blue, green.
    Rings wingOuter, wingMiddle, wingCenter, gliderLines, torsos, heads;
    Rings poles, sockRed, sockWhite, sockOutlines;
  } parts[2];
  Rings badges;
  std::vector<std::pair<m2::PointD, size_t>> counts;
  m_drawnClusters = MakeTakeoffClusters(rect, zoom, m_drawnScale);
  size_t count = 0;
  for (auto const & cluster : m_drawnClusters)
  {
    if (++count > kMaxSymbols)
      break;
    auto & p = parts[cluster.m_restricted ? 1 : 0];
    m2::PointD const & center = cluster.m_point;
    Symbol const s(center, unit);
    p.discs.push_back(MakeCircle(center, kDiscRadius * unit, 32));
    p.rings.push_back(p.discs.back());
    for (auto & sector : MakeSectors(center, cluster.m_directions, kSectorRadius * unit, kDiscRadius * unit))
    {
      p.sectorOutlines.push_back(sector);
      p.sectors.push_back(std::move(sector));
    }

    // A paraglider with the pilot hanging under the wing.
    Rings * const cellColors[] = {&p.wingOuter, &p.wingMiddle, &p.wingCenter, &p.wingMiddle, &p.wingOuter};
    for (int c = 0; c < 5; ++c)
      cellColors[c]->push_back(s.Arc(0, 2.0, 10.5, 6.0, 168 - c * 31.2, 168 - (c + 1) * 31.2));
    p.gliderLines.push_back({s.P(-10.2, 3.3), s.P(-1.3, -7.5), s.P(-4.0, 7.6)});
    p.gliderLines.push_back({s.P(10.2, 3.3), s.P(1.3, -7.5), s.P(4.0, 7.6)});
    p.heads.push_back(MakeCircle(s.P(0, -5.6), 1.9 * unit, 10));
    p.torsos.push_back({s.P(0, -7.6), s.P(0, -10.6)});
    if (cluster.m_count > 1)
    {
      badges.push_back(MakeCircle(s.P(kBadgeOffset, kBadgeOffset), kBadgeRadius * unit, 16));
      counts.emplace_back(s.P(kBadgeOffset, kBadgeOffset), cluster.m_count);
    }
  }

  count = 0;
  for (auto const & site : m_sites)
  {
    if (site.m_type != Site::Type::Landing || !IsVisible(site, zoom) || !rect.IsPointInside(site.m_point) ||
        ++count > kMaxSymbols)
    {
      continue;
    }
    auto & p = parts[site.m_restricted ? 1 : 0];
    Symbol const s(site.m_point, unit);
    // A red and white windsock, wide at the pole, in four stripes.
    double constexpr kPoleX = -8.0, kTipX = 10.0, kTopWide = 8.5, kBottomWide = 2.5, kTopTip = 6.8, kBottomTip = 4.2;
    p.poles.push_back({s.P(kPoleX, -4.5), s.P(kPoleX, kTopWide + 0.5)});
    for (int k = 0; k < 4; ++k)
    {
      double const t0 = k / 4.0, t1 = (k + 1) / 4.0;
      auto const x = [&](double t) { return kPoleX + (kTipX - kPoleX) * t; };
      auto const top = [&](double t) { return kTopWide + (kTopTip - kTopWide) * t; };
      auto const bottom = [&](double t) { return kBottomWide + (kBottomTip - kBottomWide) * t; };
      (k % 2 == 0 ? p.sockRed : p.sockWhite)
          .push_back({s.P(x(t0), top(t0)), s.P(x(t1), top(t1)), s.P(x(t1), bottom(t1)), s.P(x(t0), bottom(t0)),
                      s.P(x(t0), top(t0))});
    }
    p.sockOutlines.push_back(
        {s.P(kPoleX, kTopWide), s.P(kTipX, kTopTip), s.P(kTipX, kBottomTip), s.P(kPoleX, kBottomWide)});
  }

  Entries e([this](std::string const & id, df::DrapeApiLineData const & d) { Add(id, d); }, m_visualScale);
  // The gray ones below the colored ones.
  for (int g = 1; g >= 0; --g)
  {
    auto & p = parts[g];
    Palette const & c = g ? kGrays : kColors;
    std::string const suffix = g ? "_gray" : "";
    e.Fills("discs" + suffix, std::move(p.discs), kDisc);
    e.Fills("sectors" + suffix, std::move(p.sectors), c.m_sector);
    e.Lines("sector_outlines" + suffix, std::move(p.sectorOutlines), c.m_sectorOutline, 1.0);
    e.Lines("rings" + suffix, std::move(p.rings), c.m_ring, 1.5);
    e.Lines("glider_lines" + suffix, std::move(p.gliderLines), c.m_gliderLines, 1.0);
    e.Lines("wing_outer" + suffix, std::move(p.wingOuter), c.m_wingOuter, 4.5);
    e.Lines("wing_middle" + suffix, std::move(p.wingMiddle), c.m_wingMiddle, 4.5);
    e.Lines("wing_center" + suffix, std::move(p.wingCenter), c.m_wingCenter, 4.5);
    e.Lines("torsos" + suffix, std::move(p.torsos), c.m_pilot, 3.0);
    e.Fills("heads" + suffix, std::move(p.heads), c.m_pilot);
    e.Lines("poles" + suffix, std::move(p.poles), kPole, 2.4);
    e.Fills("sock_red" + suffix, std::move(p.sockRed), c.m_sockRed);
    e.Fills("sock_white" + suffix, std::move(p.sockWhite), kWhite);
    e.Lines("sock_outlines" + suffix, std::move(p.sockOutlines), c.m_sockOutline, 1.0);
  }
  Rings badgeOutlines = badges;
  e.Fills("badges", std::move(badges), kWhite);
  e.Lines("badge_outlines", std::move(badgeOutlines), kPilot, 1.2);
  for (auto const & [point, number] : counts)
  {
    df::DrapeApiLineData label({point}, kPilot);
    label.Label(std::to_string(number)).LabelSize(10).LabelCentered();
    Add("flight_count_" + std::to_string(point.x) + "_" + std::to_string(point.y), label);
  }
}

void FlightLayer::DrawLabels(m2::RectD const & rect, int zoom)
{
  if (zoom < kLabelsZoom)
    return;
  m2::RectD near = m_screen->ClipRect();
  near.Scale(1.5);
  size_t count = 0;
  for (auto const & site : m_sites)
  {
    if (!site.m_labeled || site.m_name.empty() || !IsVisible(site, zoom) || !near.IsPointInside(site.m_point) ||
        !rect.IsPointInside(site.m_point))
    {
      continue;
    }
    if (++count > kMaxLabels)
      return;
    df::DrapeApiLineData label({site.m_point}, kLabel);
    label.Label(site.m_name, true /* halo */).LabelSize(11);
    Add(std::string(kLabelPrefix) + site.m_id, label);
  }
}

void FlightLayer::DrawAreas(m2::RectD const & rect, int zoom)
{
  if (zoom < kAreasZoom)
    return;
  Rings landings, takeoffs;
  for (auto const & site : m_sites)
    if (site.m_area.size() > 2 && rect.IsPointInside(site.m_point))
      (site.m_type == Site::Type::Landing ? landings : takeoffs).push_back(site.m_area);
  Rings landingOutlines = landings, takeoffOutlines = takeoffs;
  Entries e([this](std::string const & id, df::DrapeApiLineData const & d) { Add(id, d); }, m_visualScale);
  e.Fills("area_fills", std::move(landings), kAreaFill);
  e.Lines("area_outlines", std::move(landingOutlines), kAreaOutline, 3.5);
  e.Fills("takeoff_area_fills", std::move(takeoffs), kTakeoffAreaFill);
  e.Lines("takeoff_area_outlines", std::move(takeoffOutlines), kTakeoffAreaOutline, 3.5);
}

void FlightLayer::DrawObstacles()
{
  if (!m_forEachObstacle)
    return;

  // Obstacles near several sites are found more than once.
  std::set<std::pair<m2::PointD, m2::PointD>> seen;
  Rings powerLines, aerialLifts, motorways;
  // Wind turbines and masts.
  std::vector<m2::PointD> points;
  m2::RectD near = m_screen->ClipRect();
  near.Scale(1.5);
  size_t sitesCount = 0;
  size_t linesCount = 0;
  for (auto const & site : m_sites)
  {
    if (!near.IsPointInside(site.m_point) || ++sitesCount > kMaxObstacleSites)
      continue;
    auto const around = mercator::RectByCenterXYAndSizeInMeters(site.m_point, kObstacleRadiusM * 2);
    m_forEachObstacle(around, [&](Obstacle && obstacle)
    {
      if (obstacle.m_points.empty() || !seen.emplace(obstacle.m_points.front(), obstacle.m_points.back()).second)
        return;
      if (obstacle.m_kind == Obstacle::Kind::WindTurbine || obstacle.m_kind == Obstacle::Kind::Mast)
      {
        points.push_back(obstacle.m_points.front());
        return;
      }
      if (obstacle.m_points.size() < 2 || ++linesCount > kMaxObstacleLines)
        return;
      auto & lines = obstacle.m_kind == Obstacle::Kind::AerialLift ? aerialLifts
                   : obstacle.m_kind == Obstacle::Kind::Motorway   ? motorways
                                                                   : powerLines;
      lines.push_back(std::move(obstacle.m_points));
    });
    for (size_t i = 0; i < m_cables.size(); ++i)
    {
      if (m_cableRects[i].IsIntersect(around) && seen.emplace(m_cables[i].front(), m_cables[i].back()).second &&
          ++linesCount <= kMaxObstacleLines)
      {
        aerialLifts.push_back(m_cables[i]);
      }
    }
  }

  auto const add = [this](std::string_view name, Rings && lines, dp::Color const & color, double widthDp,
                          std::vector<double> const & dashDp)
  {
    if (lines.empty())
      return;
    df::DrapeApiLineData data({}, color);
    data.Polylines(std::move(lines)).Width(static_cast<float>(widthDp * m_visualScale));
    if (!dashDp.empty())
      data.Dashed(dashDp, 1.0 / m_drawnScale);
    Add(std::string("flight_") + std::string(name), data);
  };
  add("obstacle_motorways", std::move(motorways), kMotorway, 9.0, {});
  add("obstacle_power_lines", std::move(powerLines), kPowerLine, 5.5, {3.0, 3.0});
  add("obstacle_aerial_lifts", std::move(aerialLifts), kAerialLift, 3.0, {10.0, 3.5});
  if (!points.empty())
  {
    df::DrapeApiLineData data(points, kWindTurbine);
    data.PointsOnly().Width(static_cast<float>(3.0 * m_visualScale));
    Add("flight_obstacle_wind_turbines", data);
  }
}
