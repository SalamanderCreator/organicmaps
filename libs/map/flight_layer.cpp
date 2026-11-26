#include "map/flight_layer.hpp"

#include "drape_frontend/drape_api.hpp"
#include "drape_frontend/visual_params.hpp"

#include "geometry/mercator.hpp"

#include "base/math.hpp"

#include <algorithm>
#include <cmath>

namespace
{
std::string_view constexpr kLabelPrefix = "flight_label_";

// The normal map: a colored circle with a white symbol, like the other map icons.
dp::Color const kTakeoffCircle(47, 126, 194, 255);
dp::Color const kLandingCircle(30, 158, 143, 255);
dp::Color const kWhite(255, 255, 255, 255);

// The flight preparation layer.
dp::Color const kDisc(255, 255, 255, 215);
dp::Color const kRing(29, 39, 51, 255);
dp::Color const kRestrictedRing(217, 130, 43, 255);
dp::Color const kSector(40, 170, 70, 235);
dp::Color const kSectorOutline(20, 90, 35, 255);
dp::Color const kWingGreen(40, 170, 70, 255);
dp::Color const kWingBlue(30, 110, 220, 255);
dp::Color const kWingBlack(25, 25, 30, 255);
dp::Color const kGliderLines(70, 75, 85, 255);
dp::Color const kHarness(35, 40, 50, 255);
dp::Color const kPilot(242, 140, 40, 255);
dp::Color const kPole(120, 128, 138, 255);
dp::Color const kSockRed(229, 51, 42, 255);
dp::Color const kSockOutline(150, 40, 35, 255);
dp::Color const kLabel(70, 70, 70, 255);
// Turquoise: bluer than the green of meadows and parks, greener than the blue of lakes.
dp::Color const kAreaOutline(0, 165, 155, 255);
dp::Color const kAreaFill(0, 190, 175, 80);
// Translucent, so that the map still shows the kind of line and its name below.
dp::Color const kObstacle(225, 20, 20, 150);

// Keeps the geometry small on a crowded screen.
size_t constexpr kMaxSymbols = 600;
size_t constexpr kMaxLabels = 150;
size_t constexpr kMaxObstacleSites = 40;
size_t constexpr kMaxObstacleLines = 600;

// Symbol sizes in dp.
double constexpr kIconRadius = 9.5;
double constexpr kDiscRadius = 14.5;
// The takeoff directions are a green wreath around the disc, so the paraglider does not hide them.
double constexpr kSectorRadius = 24.0;

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

std::optional<std::string> FlightLayer::HitTest(m2::PointD const & point, ScreenBase const & screen,
                                                double tolerancePx) const
{
  int const zoom = GetZoom(screen);
  m2::PointD const tapPx = screen.GtoP(point);
  double bestSqrPx = tolerancePx * tolerancePx;
  Site const * best = nullptr;
  Site const * inArea = nullptr;
  for (auto const & site : m_sites)
  {
    if (!IsVisible(site, zoom))
      continue;
    double const sqrPx = screen.GtoP(site.m_point).SquaredLength(tapPx);
    if (sqrPx <= bestSqrPx)
    {
      bestSqrPx = sqrPx;
      best = &site;
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
    return best->m_id;
  if (inArea)
    return inArea->m_id;
  return {};
}

// static
std::vector<std::vector<m2::PointD>> FlightLayer::MakeSectors(m2::PointD const & center, uint16_t directions,
                                                              double radius, double innerRadius)
{
  std::vector<std::vector<m2::PointD>> rings;
  if (directions == 0)
    return rings;

  // Runs of neighbouring directions; all 16 are split in two halves to keep each ring simple.
  std::vector<std::pair<int, int>> runs;
  if (directions == 0xFFFF)
  {
    runs = {{0, 8}, {8, 8}};
  }
  else
  {
    for (int first = 0; first < 16; ++first)
    {
      if ((directions & (1 << first)) == 0 || (directions & (1 << ((first + 15) % 16))) != 0)
        continue;
      int count = 0;
      while (count < 16 && (directions & (1 << ((first + count) % 16))) != 0)
        ++count;
      runs.emplace_back(first, count);
    }
  }

  auto const at = [&center](double azimuthDeg, double r)
  {
    double const a = math::DegToRad(azimuthDeg);
    return m2::PointD(center.x + r * std::sin(a), center.y + r * std::cos(a));
  };
  for (auto const & [first, count] : runs)
  {
    double const from = first * 22.5 - 11.25;
    double const to = from + 22.5 * count;
    int const steps = count * 4;
    std::vector<m2::PointD> ring;
    for (int i = 0; i <= steps; ++i)
      ring.push_back(at(from + (to - from) * i / steps, radius));
    if (innerRadius > 0)
    {
      for (int i = steps; i >= 0; --i)
        ring.push_back(at(from + (to - from) * i / steps, innerRadius));
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
  Rings takeoffCircles, landingCircles, canopies, risers, pilots, poles, socks;
  size_t count = 0;
  for (auto const & site : m_sites)
  {
    if (!IsVisible(site, zoom) || !rect.IsPointInside(site.m_point) || ++count > kMaxSymbols)
      continue;
    Symbol const s(site.m_point, unit);
    if (site.m_type == Site::Type::Takeoff)
    {
      takeoffCircles.push_back(MakeCircle(site.m_point, kIconRadius * unit));
      canopies.push_back(s.Arc(0, 0.2, 6.0, 4.0, 12, 168));
      risers.push_back({s.P(-5.8, 1.0), s.P(0, -4.2), s.P(5.8, 1.0)});
      pilots.push_back(MakeCircle(s.P(0, -5.0), 1.6 * unit, 8));
    }
    else
    {
      landingCircles.push_back(MakeCircle(site.m_point, kIconRadius * unit));
      poles.push_back({s.P(-4.5, -6.0), s.P(-4.5, 5.5)});
      socks.push_back({s.P(-4.5, 5.5), s.P(5.5, 4.3), s.P(5.5, 1.7), s.P(-4.5, 0.5), s.P(-4.5, 5.5)});
    }
  }

  Entries e([this](std::string const & id, df::DrapeApiLineData const & d) { Add(id, d); }, m_visualScale);
  e.Fills("icon_takeoffs", std::move(takeoffCircles), kTakeoffCircle);
  e.Fills("icon_landings", std::move(landingCircles), kLandingCircle);
  e.Lines("icon_canopies", std::move(canopies), kWhite, 2.0);
  e.Lines("icon_risers", std::move(risers), kWhite, 1.1);
  e.Fills("icon_pilots", std::move(pilots), kWhite);
  e.Lines("icon_poles", std::move(poles), kWhite, 1.5);
  e.Fills("icon_socks", std::move(socks), kWhite);
}

void FlightLayer::DrawLayerSymbols(m2::RectD const & rect, int zoom, double unit)
{
  Rings discs, rings, restrictedRings, sectors, sectorOutlines;
  // The wing has five cells: green, blue, black, blue, green.
  Rings wingGreen, wingBlue, wingBlack, gliderLines, harnesses, torsos, heads;
  Rings poles, sockRed, sockWhite, sockOutlines;
  size_t count = 0;
  for (auto const & site : m_sites)
  {
    if (!IsVisible(site, zoom) || !rect.IsPointInside(site.m_point) || ++count > kMaxSymbols)
      continue;
    Symbol const s(site.m_point, unit);
    if (site.m_type == Site::Type::Takeoff)
    {
      discs.push_back(MakeCircle(site.m_point, kDiscRadius * unit, 32));
      (site.m_restricted ? restrictedRings : rings).push_back(discs.back());
      for (auto & sector : MakeSectors(site.m_point, site.m_directions, kSectorRadius * unit, kDiscRadius * unit))
      {
        sectorOutlines.push_back(sector);
        sectors.push_back(std::move(sector));
      }

      // A paraglider sitting in the harness under the wing.
      Rings * const cellColors[] = {&wingGreen, &wingBlue, &wingBlack, &wingBlue, &wingGreen};
      for (int c = 0; c < 5; ++c)
        cellColors[c]->push_back(s.Arc(0, 2.0, 10.5, 6.0, 168 - c * 31.2, 168 - (c + 1) * 31.2));
      gliderLines.push_back({s.P(-10.2, 3.3), s.P(-1.3, -7.5), s.P(-4.0, 7.6)});
      gliderLines.push_back({s.P(10.2, 3.3), s.P(1.3, -7.5), s.P(4.0, 7.6)});
      heads.push_back(MakeCircle(s.P(0, -5.6), 1.9 * unit, 10));
      torsos.push_back({s.P(0, -7.6), s.P(0, -10.0)});
      harnesses.push_back({s.P(-2.2, -10.8), s.P(2.6, -10.8), s.P(4.6, -9.0)});
    }
    else
    {
      // A red and white windsock, wide at the pole, in four stripes.
      double constexpr kPoleX = -8.0, kTipX = 10.0, kTopWide = 8.5, kBottomWide = 2.5, kTopTip = 6.8,
                       kBottomTip = 4.2;
      poles.push_back({s.P(kPoleX, -4.5), s.P(kPoleX, kTopWide + 0.5)});
      for (int k = 0; k < 4; ++k)
      {
        double const t0 = k / 4.0, t1 = (k + 1) / 4.0;
        auto const x = [&](double t) { return kPoleX + (kTipX - kPoleX) * t; };
        auto const top = [&](double t) { return kTopWide + (kTopTip - kTopWide) * t; };
        auto const bottom = [&](double t) { return kBottomWide + (kBottomTip - kBottomWide) * t; };
        (k % 2 == 0 ? sockRed : sockWhite)
            .push_back({s.P(x(t0), top(t0)), s.P(x(t1), top(t1)), s.P(x(t1), bottom(t1)), s.P(x(t0), bottom(t0)),
                        s.P(x(t0), top(t0))});
      }
      sockOutlines.push_back({s.P(kPoleX, kTopWide), s.P(kTipX, kTopTip), s.P(kTipX, kBottomTip),
                              s.P(kPoleX, kBottomWide)});
    }
  }

  Entries e([this](std::string const & id, df::DrapeApiLineData const & d) { Add(id, d); }, m_visualScale);
  e.Fills("discs", std::move(discs), kDisc);
  e.Fills("sectors", std::move(sectors), kSector);
  e.Lines("sector_outlines", std::move(sectorOutlines), kSectorOutline, 1.0);
  e.Lines("rings", std::move(rings), kRing, 1.5);
  e.Lines("restricted_rings", std::move(restrictedRings), kRestrictedRing, 3.0);
  e.Lines("glider_lines", std::move(gliderLines), kGliderLines, 1.0);
  e.Lines("wing_green", std::move(wingGreen), kWingGreen, 4.5);
  e.Lines("wing_blue", std::move(wingBlue), kWingBlue, 4.5);
  e.Lines("wing_black", std::move(wingBlack), kWingBlack, 4.5);
  e.Lines("harnesses", std::move(harnesses), kHarness, 2.6);
  e.Lines("torsos", std::move(torsos), kPilot, 3.0);
  e.Fills("heads", std::move(heads), kPilot);
  e.Lines("poles", std::move(poles), kPole, 2.4);
  e.Fills("sock_red", std::move(sockRed), kSockRed);
  e.Fills("sock_white", std::move(sockWhite), kWhite);
  e.Lines("sock_outlines", std::move(sockOutlines), kSockOutline, 1.0);
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
  Rings areas;
  for (auto const & site : m_sites)
    if (site.m_area.size() > 2 && IsVisible(site, zoom) && rect.IsPointInside(site.m_point))
      areas.push_back(site.m_area);
  Rings outlines = areas;
  Entries e([this](std::string const & id, df::DrapeApiLineData const & d) { Add(id, d); }, m_visualScale);
  e.Fills("area_fills", std::move(areas), kAreaFill);
  e.Lines("area_outlines", std::move(outlines), kAreaOutline, 3.5);
}

void FlightLayer::DrawObstacles()
{
  if (!m_forEachObstacle)
    return;

  // Obstacles near several sites are found more than once.
  std::set<std::pair<m2::PointD, m2::PointD>> seen;
  Rings lines;
  std::vector<m2::PointD> points;
  m2::RectD near = m_screen->ClipRect();
  near.Scale(1.5);
  size_t sitesCount = 0;
  for (auto const & site : m_sites)
  {
    if (!near.IsPointInside(site.m_point) || ++sitesCount > kMaxObstacleSites)
      continue;
    auto const around = mercator::RectByCenterXYAndSizeInMeters(site.m_point, kObstacleRadiusM * 2);
    m_forEachObstacle(around, [&](Obstacle && obstacle)
    {
      if (obstacle.m_points.empty() || !seen.emplace(obstacle.m_points.front(), obstacle.m_points.back()).second)
        return;
      if (obstacle.m_points.size() == 1)
        points.push_back(obstacle.m_points.front());
      else if (lines.size() < kMaxObstacleLines)
        lines.push_back(std::move(obstacle.m_points));
    });
  }
  Entries e([this](std::string const & id, df::DrapeApiLineData const & d) { Add(id, d); }, m_visualScale);
  e.Lines("obstacle_lines", std::move(lines), kObstacle, 4.5);
  if (!points.empty())
  {
    df::DrapeApiLineData data(points, kObstacle);
    data.PointsOnly().Width(static_cast<float>(3.0 * m_visualScale));
    Add("flight_obstacle_points", data);
  }
}
