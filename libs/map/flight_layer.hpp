#pragma once

#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"
#include "geometry/screenbase.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace df
{
class DrapeApi;
}  // namespace df

/// Maps&Fly: the "Flight preparation" layer for paragliding. Shows takeoff and landing sites with their
/// takeoff directions and names, known landing areas in green and obstacles (power lines, cable cars,
/// masts) around the sites, drawn with the DrapeApi. The sites come from the app (ParaglidingEarth and
/// OpenStreetMap, loaded per 1 x 1 degree cell), obstacles from the downloaded maps.
class FlightLayer
{
public:
  struct Site
  {
    enum class Type : uint8_t
    {
      Takeoff,
      Landing
    };

    std::string m_id;
    std::string m_name;
    m2::PointD m_point;
    Type m_type = Type::Takeoff;
    /// Bit i is the direction i * 22.5 degrees clockwise from north, as the wind should blow from.
    uint16_t m_directions = 0;
    /// The closed ring of a known landing area, or empty.
    std::vector<m2::PointD> m_area;
  };

  struct Obstacle
  {
    /// A line (power line, cable car) or a single point (mast, tower, wind turbine).
    std::vector<m2::PointD> m_points;
  };

  using CellsRequestFn = std::function<void(std::vector<int> const & cells)>;
  using ForEachObstacleFn = std::function<void(m2::RectD const & rect, std::function<void(Obstacle &&)> const & fn)>;

  /// Sites are drawn from this zoom on, labels, directions and landing areas from kDetailsZoom on and
  /// obstacles from kObstaclesZoom on. Sites are loaded from kLoadZoom on.
  static int constexpr kSitesZoom = 7;
  static int constexpr kLoadZoom = 8;
  static int constexpr kDetailsZoom = 11;
  static int constexpr kObstaclesZoom = 13;
  static double constexpr kObstacleRadiusM = 1500.0;

  explicit FlightLayer(df::DrapeApi & drapeApi);

  /// Symbol sizes are in density-independent pixels.
  void SetVisualScale(double visualScale) { m_visualScale = visualScale; }

  void SetEnabled(bool enabled);
  bool IsEnabled() const { return m_enabled; }

  /// Asks for the sites of the cells that come into view. A new function gets all cells in view again.
  void SetCellsRequestFn(CellsRequestFn fn);
  void SetForEachObstacleFn(ForEachObstacleFn fn) { m_forEachObstacle = std::move(fn); }

  /// The sites of one cell, which replace the earlier ones of this cell.
  void SetSites(int cell, std::vector<Site> && sites);
  void SetViewport(ScreenBase const & screen);

  /// The id of the site at |point|, within |tolerancePx| on the screen or inside its landing area.
  std::optional<std::string> HitTest(m2::PointD const & point, ScreenBase const & screen, double tolerancePx) const;

  static int CellKey(double lat, double lon);
  /// The cells that cover |rect|, empty if more than |maxCells| would be needed.
  static std::vector<int> CellsInRect(m2::RectD const & rect, size_t maxCells);
  /// Rays from |center| of |lengthM| in each direction of |directions|, as one polyline back and forth.
  static std::vector<m2::PointD> MakeDirectionStar(m2::PointD const & center, uint16_t directions, double lengthM);

private:
  enum class Detail
  {
    Hidden,
    Sites,
    Details,
    Obstacles
  };

  static Detail GetDetail(ScreenBase const & screen);

  void RequestCells();
  void Redraw();
  void Clear();
  void DrawDetails(m2::RectD const & rect, double scale);
  void DrawObstacles(m2::RectD const & rect);

  df::DrapeApi & m_drapeApi;
  bool m_enabled = false;
  CellsRequestFn m_requestCells;
  ForEachObstacleFn m_forEachObstacle;
  std::map<int, std::vector<Site>> m_sites;
  std::set<int> m_requestedCells;
  std::optional<ScreenBase> m_screen;
  double m_visualScale = 1.0;

  // What is drawn now, for which area and scale.
  std::set<std::string> m_drawnIds;
  Detail m_drawnDetail = Detail::Hidden;
  m2::RectD m_drawnRect;
  double m_drawnScale = 0;
};
