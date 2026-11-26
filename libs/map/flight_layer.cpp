#include "map/flight_layer.hpp"

#include "drape_frontend/drape_api.hpp"
#include "drape_frontend/visual_params.hpp"

#include "geometry/mercator.hpp"

#include "base/math.hpp"

#include <algorithm>
#include <cmath>

namespace
{
std::string_view constexpr kTakeoffsId = "flight_takeoffs";
std::string_view constexpr kLandingsId = "flight_landings";
std::string_view constexpr kLabelPrefix = "flight_label_";
std::string_view constexpr kDirectionsPrefix = "flight_dirs_";
std::string_view constexpr kAreaPrefix = "flight_area_";
std::string_view constexpr kObstaclePrefix = "flight_obst_";
std::string_view constexpr kObstaclePointsId = "flight_obst_points";

dp::Color const kTakeoffColor(0, 100, 210, 255);
dp::Color const kLandingColor(225, 140, 0, 255);
dp::Color const kAreaOutlineColor(30, 150, 40, 230);
dp::Color const kAreaFillColor(60, 200, 60, 110);
dp::Color const kObstacleColor(220, 20, 20, 230);

// Keeps the number of DrapeApi entries small on a crowded screen.
size_t constexpr kMaxDetailedSites = 300;
size_t constexpr kMaxObstacleSites = 40;
size_t constexpr kMaxObstacleLines = 400;
// More cells are not requested at once, e.g. on a zoomed out screen.
size_t constexpr kMaxRequestedCells = 12;

double constexpr kDirectionLengthPx = 26.0;

int GetZoom(ScreenBase const & screen)
{
  return static_cast<int>(df::GetZoomLevel(screen.GetScale()));
}
}  // namespace

FlightLayer::FlightLayer(df::DrapeApi & drapeApi) : m_drapeApi(drapeApi) {}

void FlightLayer::SetEnabled(bool enabled)
{
  if (m_enabled == enabled)
    return;
  m_enabled = enabled;
  if (!m_enabled)
  {
    Clear();
    return;
  }
  RequestCells();
  Redraw();
}

void FlightLayer::SetCellsRequestFn(CellsRequestFn fn)
{
  m_requestCells = std::move(fn);
  m_requestedCells.clear();
  RequestCells();
}

void FlightLayer::SetSites(int cell, std::vector<Site> && sites)
{
  m_sites[cell] = std::move(sites);
  m_requestedCells.insert(cell);
  if (m_enabled)
    Redraw();
}

void FlightLayer::SetViewport(ScreenBase const & screen)
{
  m_screen = screen;
  if (!m_enabled)
    return;
  RequestCells();

  bool needsRedraw = GetDetail(screen) != m_drawnDetail;
  if (!needsRedraw && m_drawnDetail >= Detail::Details)
  {
    // Labels, directions and obstacles are built for the area around the screen and the current zoom.
    double const scaleRatio = screen.GetScale() / m_drawnScale;
    needsRedraw = !m_drawnRect.IsRectInside(screen.ClipRect()) || scaleRatio < 0.6 || scaleRatio > 1.7;
  }
  if (needsRedraw)
    Redraw();
}

std::optional<std::string> FlightLayer::HitTest(m2::PointD const & point, ScreenBase const & screen,
                                                double tolerancePx) const
{
  if (!m_enabled || GetDetail(screen) == Detail::Hidden)
    return {};

  m2::PointD const tapPx = screen.GtoP(point);
  double bestSqrPx = tolerancePx * tolerancePx;
  Site const * best = nullptr;
  Site const * inArea = nullptr;
  for (auto const & [cell, sites] : m_sites)
  {
    for (auto const & site : sites)
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
  }
  if (best)
    return best->m_id;
  if (inArea)
    return inArea->m_id;
  return {};
}

// static
int FlightLayer::CellKey(double lat, double lon)
{
  int const row = std::clamp(static_cast<int>(std::floor(lat + 90.0)), 0, 179);
  int const col = std::clamp(static_cast<int>(std::floor(lon + 180.0)), 0, 359);
  return row * 360 + col;
}

// static
std::vector<int> FlightLayer::CellsInRect(m2::RectD const & rect, size_t maxCells)
{
  auto const minLatLon = mercator::ToLatLon(rect.LeftBottom());
  auto const maxLatLon = mercator::ToLatLon(rect.RightTop());
  int const minRow = std::clamp(static_cast<int>(std::floor(minLatLon.m_lat + 90.0)), 0, 179);
  int const maxRow = std::clamp(static_cast<int>(std::floor(maxLatLon.m_lat + 90.0)), 0, 179);
  int const minCol = std::clamp(static_cast<int>(std::floor(minLatLon.m_lon + 180.0)), 0, 359);
  int const maxCol = std::clamp(static_cast<int>(std::floor(maxLatLon.m_lon + 180.0)), 0, 359);
  if (static_cast<size_t>(maxRow - minRow + 1) * static_cast<size_t>(maxCol - minCol + 1) > maxCells)
    return {};

  std::vector<int> cells;
  for (int row = minRow; row <= maxRow; ++row)
    for (int col = minCol; col <= maxCol; ++col)
      cells.push_back(row * 360 + col);
  return cells;
}

// static
std::vector<m2::PointD> FlightLayer::MakeDirectionStar(m2::PointD const & center, uint16_t directions, double lengthM)
{
  std::vector<m2::PointD> points;
  // Mercator is conformal, so the local scale is the same in all directions.
  double const length = mercator::RectByCenterXYAndSizeInMeters(center, lengthM).SizeX() / 2;
  for (int i = 0; i < 16; ++i)
  {
    if ((directions & (1 << i)) == 0)
      continue;
    double const azimuth = math::DegToRad(22.5 * i);
    if (points.empty())
      points.push_back(center);
    points.emplace_back(center.x + length * std::sin(azimuth), center.y + length * std::cos(azimuth));
    points.push_back(center);
  }
  return points;
}

// static
FlightLayer::Detail FlightLayer::GetDetail(ScreenBase const & screen)
{
  int const zoom = GetZoom(screen);
  if (zoom >= kObstaclesZoom)
    return Detail::Obstacles;
  if (zoom >= kDetailsZoom)
    return Detail::Details;
  if (zoom >= kSitesZoom)
    return Detail::Sites;
  return Detail::Hidden;
}

void FlightLayer::RequestCells()
{
  if (!m_enabled || !m_requestCells || !m_screen || GetZoom(*m_screen) < kLoadZoom)
    return;

  std::vector<int> cells;
  for (int const cell : CellsInRect(m_screen->ClipRect(), kMaxRequestedCells))
    if (m_requestedCells.insert(cell).second)
      cells.push_back(cell);
  if (!cells.empty())
    m_requestCells(cells);
}

void FlightLayer::Clear()
{
  for (auto const & id : m_drawnIds)
    m_drapeApi.RemoveLine(id);
  m_drawnIds.clear();
  m_drawnDetail = Detail::Hidden;
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

  if (m_drawnDetail != Detail::Hidden)
  {
    std::vector<m2::PointD> takeoffs;
    std::vector<m2::PointD> landings;
    for (auto const & [cell, sites] : m_sites)
      for (auto const & site : sites)
        (site.m_type == Site::Type::Takeoff ? takeoffs : landings).push_back(site.m_point);

    float const width = m_drawnDetail == Detail::Sites ? 2.5f : 3.5f;
    for (auto const & [id, points, color] :
         {std::tuple(kTakeoffsId, &takeoffs, kTakeoffColor), std::tuple(kLandingsId, &landings, kLandingColor)})
    {
      if (points->empty())
        continue;
      df::DrapeApiLineData data(*points, color);
      data.PointsOnly().Width(width);
      m_drapeApi.AddLine(std::string(id), data);
      m_drawnIds.emplace(id);
    }
  }

  if (m_drawnDetail >= Detail::Details)
    DrawDetails(m_drawnRect, m_drawnScale);
  if (m_drawnDetail >= Detail::Obstacles)
    DrawObstacles(m_drawnRect);

  for (auto const & id : previousIds)
    if (!m_drawnIds.contains(id))
      m_drapeApi.RemoveLine(id);
}

void FlightLayer::DrawDetails(m2::RectD const & rect, double scale)
{
  size_t count = 0;
  for (auto const & [cell, sites] : m_sites)
  {
    for (auto const & site : sites)
    {
      if (!rect.IsPointInside(site.m_point))
        continue;
      if (++count > kMaxDetailedSites)
        return;

      auto const & color = site.m_type == Site::Type::Takeoff ? kTakeoffColor : kLandingColor;
      if (site.m_area.size() > 2)
      {
        df::DrapeApiLineData area(site.m_area, kAreaOutlineColor);
        area.Width(2.0f).Fill(kAreaFillColor);
        std::string id = std::string(kAreaPrefix) + site.m_id;
        m_drapeApi.AddLine(id, area);
        m_drawnIds.insert(std::move(id));
      }
      if (site.m_directions != 0)
      {
        double const lengthM =
            mercator::DistanceOnEarth(site.m_point, site.m_point + m2::PointD(scale, 0.0)) *
            kDirectionLengthPx * m_visualScale;
        df::DrapeApiLineData star(MakeDirectionStar(site.m_point, site.m_directions, lengthM), color);
        star.Width(3.0f);
        std::string id = std::string(kDirectionsPrefix) + site.m_id;
        m_drapeApi.AddLine(id, star);
        m_drawnIds.insert(std::move(id));
      }
      if (!site.m_name.empty())
      {
        df::DrapeApiLineData label({site.m_point}, color);
        label.Label(site.m_name, true /* halo */);
        std::string id = std::string(kLabelPrefix) + site.m_id;
        m_drapeApi.AddLine(id, label);
        m_drawnIds.insert(std::move(id));
      }
    }
  }
}

void FlightLayer::DrawObstacles(m2::RectD const & rect)
{
  if (!m_forEachObstacle)
    return;

  // Obstacles near several sites are found more than once.
  std::set<std::pair<m2::PointD, m2::PointD>> seen;
  std::vector<m2::PointD> points;
  size_t lines = 0;
  size_t sitesCount = 0;
  for (auto const & [cell, sites] : m_sites)
  {
    for (auto const & site : sites)
    {
      if (!rect.IsPointInside(site.m_point) || ++sitesCount > kMaxObstacleSites)
        continue;
      auto const around = mercator::RectByCenterXYAndSizeInMeters(site.m_point, kObstacleRadiusM);
      m_forEachObstacle(around, [&](Obstacle && obstacle)
      {
        if (obstacle.m_points.empty() || !seen.emplace(obstacle.m_points.front(), obstacle.m_points.back()).second)
          return;
        if (obstacle.m_points.size() == 1)
        {
          points.push_back(obstacle.m_points.front());
          return;
        }
        if (lines >= kMaxObstacleLines)
          return;
        df::DrapeApiLineData line(obstacle.m_points, kObstacleColor);
        line.Width(4.0f);
        std::string id = std::string(kObstaclePrefix) + std::to_string(lines++);
        m_drapeApi.AddLine(id, line);
        m_drawnIds.insert(std::move(id));
      });
    }
  }
  if (!points.empty())
  {
    df::DrapeApiLineData data(points, kObstacleColor);
    data.PointsOnly().Width(3.0f);
    m_drapeApi.AddLine(std::string(kObstaclePointsId), data);
    m_drawnIds.emplace(kObstaclePointsId);
  }
}
