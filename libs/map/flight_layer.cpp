#include "map/flight_layer.hpp"

#include "drape_frontend/drape_api.hpp"
#include "drape_frontend/visual_params.hpp"

#include "geometry/mercator.hpp"

#include "base/math.hpp"

#include <algorithm>
#include <cmath>

namespace
{
std::string_view constexpr kPrefix = "flight_";
std::string_view constexpr kLabelPrefix = "flight_label_";

dp::Color const kDark(40, 50, 65, 255);
dp::Color const kGrey(110, 120, 132, 255);
dp::Color const kHalo(255, 255, 255, 170);
dp::Color const kDisc(255, 255, 255, 220);
dp::Color const kRing(29, 39, 51, 255);
dp::Color const kRestrictedRing(217, 130, 43, 255);
dp::Color const kSector(30, 138, 60, 230);
dp::Color const kCanopy(225, 50, 40, 255);
dp::Color const kSock(245, 120, 0, 255);
dp::Color const kLabel(30, 30, 30, 255);
dp::Color const kAreaOutline(30, 150, 40, 230);
dp::Color const kAreaFill(60, 200, 60, 90);
dp::Color const kObstacle(220, 20, 20, 200);

// Keeps the geometry small on a crowded screen.
size_t constexpr kMaxSymbols = 600;
size_t constexpr kMaxLabels = 200;
size_t constexpr kMaxObstacleSites = 40;
size_t constexpr kMaxObstacleLines = 600;

// Symbol sizes in dp, the layer draws them bigger.
double constexpr kLayerFactor = 1.6;
double constexpr kHaloRadius = 9.0;
double constexpr kDiscRadius = 10.0;

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
  bool needsRedraw = GetDetail(screen) != m_drawnDetail;
  if (!needsRedraw && m_drawnDetail >= Detail::Symbols)
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
  if (GetDetail(screen) == Detail::Hidden)
    return {};

  m2::PointD const tapPx = screen.GtoP(point);
  double bestSqrPx = tolerancePx * tolerancePx;
  Site const * best = nullptr;
  Site const * inArea = nullptr;
  for (auto const & site : m_sites)
  {
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
                                                              double radius)
{
  std::vector<std::vector<m2::PointD>> rings;
  if (directions == 0)
    return rings;
  if (directions == 0xFFFF)
  {
    rings.push_back(MakeCircle(center, radius, 32));
    return rings;
  }

  // Start each run after a missing direction, so that a run across north stays one wedge.
  int start = 0;
  while ((directions & (1 << start)) != 0)
    ++start;
  for (int k = 1; k <= 16; ++k)
  {
    int const first = (start + k) % 16;
    if ((directions & (1 << first)) == 0 || (directions & (1 << ((first + 15) % 16))) != 0)
      continue;
    int count = 0;
    while (count < 16 && (directions & (1 << ((first + count) % 16))) != 0)
      ++count;

    std::vector<m2::PointD> ring = {center};
    int const steps = count * 4;
    for (int i = 0; i <= steps; ++i)
    {
      double const azimuth = math::DegToRad(first * 22.5 - 11.25 + 22.5 * count * i / steps);
      ring.emplace_back(center.x + radius * std::sin(azimuth), center.y + radius * std::cos(azimuth));
    }
    ring.push_back(center);
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

// static
FlightLayer::Detail FlightLayer::GetDetail(ScreenBase const & screen)
{
  int const zoom = GetZoom(screen);
  if (zoom >= kObstaclesZoom)
    return Detail::Obstacles;
  if (zoom >= kLabelsZoom)
    return Detail::Labels;
  if (zoom >= kSymbolsZoom)
    return Detail::Symbols;
  if (zoom >= kDotsZoom)
    return Detail::Dots;
  return Detail::Hidden;
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
  m_drawnDetail = GetDetail(*m_screen);
  m_drawnScale = m_screen->GetScale();
  m_drawnRect = m_screen->ClipRect();
  m_drawnRect.Scale(3.0);

  auto const detail = *m_drawnDetail;
  if (detail == Detail::Dots)
  {
    std::vector<m2::PointD> takeoffs;
    std::vector<m2::PointD> landings;
    for (auto const & site : m_sites)
      (site.m_type == Site::Type::Takeoff ? takeoffs : landings).push_back(site.m_point);
    for (auto const & [name, points, color] :
         {std::tuple("dots_takeoffs", &takeoffs, m_enabled ? kCanopy : kDark),
          std::tuple("dots_landings", &landings, m_enabled ? kSock : kGrey)})
    {
      if (points->empty())
        continue;
      df::DrapeApiLineData data(*points, color);
      data.PointsOnly().Width(m_enabled ? 2.5f : 1.8f);
      Add(std::string(kPrefix) + name, data);
    }
  }
  if (detail >= Detail::Labels)
    DrawAreas(m_drawnRect);
  if (detail >= Detail::Obstacles && m_enabled)
    DrawObstacles();
  if (detail >= Detail::Symbols)
    DrawSymbols(m_drawnRect, m_drawnScale * m_visualScale);
  if (detail >= Detail::Labels)
    DrawLabels(m_drawnRect);

  for (auto const & id : previousIds)
    if (!m_drawnIds.contains(id))
      m_drapeApi.RemoveLine(id);
}

void FlightLayer::DrawSymbols(m2::RectD const & rect, double unit)
{
  double const f = m_enabled ? kLayerFactor : 1.0;
  std::vector<std::vector<m2::PointD>> halos, discs, rings, restrictedRings, sectors, canopies, risers, pilots, poles,
      socks;
  size_t count = 0;
  for (auto const & site : m_sites)
  {
    if (!rect.IsPointInside(site.m_point) || ++count > kMaxSymbols)
      continue;
    Symbol const s(site.m_point, unit * f);
    if (m_enabled && site.m_type == Site::Type::Takeoff)
    {
      discs.push_back(MakeCircle(site.m_point, kDiscRadius * unit * f));
      (site.m_restricted ? restrictedRings : rings).push_back(discs.back());
      for (auto & sector : MakeSectors(site.m_point, site.m_directions, kDiscRadius * unit * f))
        sectors.push_back(std::move(sector));
    }
    else
    {
      halos.push_back(MakeCircle(site.m_point, kHaloRadius * unit * f));
    }

    if (site.m_type == Site::Type::Takeoff)
    {
      // A paraglider: the canopy, the lines and the pilot.
      canopies.push_back(s.Arc(0, 1.5, 7.0, 4.5, 15, 165));
      risers.push_back({s.P(-6.8, 2.7), s.P(0, -5.0), s.P(6.8, 2.7)});
      pilots.push_back(MakeCircle(s.P(0, -5.6), 1.6 * unit * f, 8));
    }
    else
    {
      // A windsock on its pole.
      poles.push_back({s.P(-5.0, -7.0), s.P(-5.0, 7.0)});
      socks.push_back({s.P(-5.0, 7.0), s.P(6.0, 5.5), s.P(6.0, 2.5), s.P(-5.0, 1.0), s.P(-5.0, 7.0)});
    }
  }

  auto const addFills = [this](std::string_view name, std::vector<std::vector<m2::PointD>> & fills,
                               dp::Color const & color)
  {
    if (fills.empty())
      return;
    df::DrapeApiLineData data({}, color);
    data.FillRings(std::move(fills), color);
    Add(std::string(kPrefix) + std::string(name), data);
  };
  auto const addLines = [this](std::string_view name, std::vector<std::vector<m2::PointD>> & lines,
                               dp::Color const & color, float width)
  {
    if (lines.empty())
      return;
    df::DrapeApiLineData data({}, color);
    data.Polylines(std::move(lines)).Width(width);
    Add(std::string(kPrefix) + std::string(name), data);
  };

  addFills("halos", halos, kHalo);
  addFills("discs", discs, kDisc);
  addFills("sectors", sectors, kSector);
  addLines("rings", rings, kRing, 1.5f);
  addLines("restricted_rings", restrictedRings, kRestrictedRing, 2.5f);
  addLines("canopies", canopies, m_enabled ? kCanopy : kDark, m_enabled ? 3.5f : 2.5f);
  addLines("risers", risers, kDark, 1.0f);
  addFills("pilots", pilots, kDark);
  addLines("poles", poles, kDark, m_enabled ? 2.0f : 1.5f);
  if (m_enabled)
  {
    std::vector<std::vector<m2::PointD>> outlines = socks;
    addFills("socks", socks, kSock);
    addLines("sock_outlines", outlines, kDark, 1.0f);
  }
  else
  {
    addFills("socks", socks, kGrey);
  }
}

void FlightLayer::DrawLabels(m2::RectD const & rect)
{
  // Labels close to the screen first, as there are fewer of them.
  m2::RectD near = m_screen->ClipRect();
  near.Scale(1.5);
  size_t count = 0;
  for (auto const & site : m_sites)
  {
    if (site.m_name.empty() || !near.IsPointInside(site.m_point) || !rect.IsPointInside(site.m_point))
      continue;
    if (++count > kMaxLabels)
      return;
    df::DrapeApiLineData label({site.m_point}, kLabel);
    label.Label(site.m_name, true /* halo */);
    Add(std::string(kLabelPrefix) + site.m_id, label);
  }
}

void FlightLayer::DrawAreas(m2::RectD const & rect)
{
  std::vector<std::vector<m2::PointD>> areas;
  for (auto const & site : m_sites)
    if (site.m_area.size() > 2 && rect.IsPointInside(site.m_point))
      areas.push_back(site.m_area);
  if (areas.empty())
    return;
  std::vector<std::vector<m2::PointD>> outlines = areas;
  df::DrapeApiLineData fill({}, kAreaFill);
  fill.FillRings(std::move(areas), kAreaFill);
  Add(std::string(kPrefix) + "area_fills", fill);
  df::DrapeApiLineData outline({}, kAreaOutline);
  outline.Polylines(std::move(outlines)).Width(2.0f);
  Add(std::string(kPrefix) + "area_outlines", outline);
}

void FlightLayer::DrawObstacles()
{
  if (!m_forEachObstacle)
    return;

  // Obstacles near several sites are found more than once.
  std::set<std::pair<m2::PointD, m2::PointD>> seen;
  std::vector<std::vector<m2::PointD>> lines;
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
  if (!lines.empty())
  {
    df::DrapeApiLineData data({}, kObstacle);
    data.Polylines(std::move(lines)).Width(4.0f);
    Add(std::string(kPrefix) + "obstacle_lines", data);
  }
  if (!points.empty())
  {
    df::DrapeApiLineData data(points, kObstacle);
    data.PointsOnly().Width(3.0f);
    Add(std::string(kPrefix) + "obstacle_points", data);
  }
}
