#pragma once

#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"
#include "geometry/screenbase.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace df
{
class DrapeApi;
struct DrapeApiLineData;
}  // namespace df

/// Maps&Fly: paragliding takeoffs and landings on the map, drawn with the DrapeApi. On the normal map they
/// look like the other map icons: a colored circle with a white paraglider or windsock. The "Flight
/// preparation" layer shows colored symbols instead, takeoffs with their direction sectors, from further
/// out, and highlights obstacles around the sites (power lines, aerial lifts, motorways, wind turbines).
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
    /// Drawn with an orange ring, e.g. a site for club members only.
    bool m_restricted = false;
    /// Gets a label, e.g. not when the map shows the site's name already.
    bool m_labeled = false;
    /// The closed ring of a known landing area, or empty.
    std::vector<m2::PointD> m_area;
  };

  struct Obstacle
  {
    /// A line (power line, aerial lift, motorway) or a single point (wind turbine).
    std::vector<m2::PointD> m_points;
  };

  using ForEachObstacleFn = std::function<void(m2::RectD const & rect, std::function<void(Obstacle &&)> const & fn)>;

  /// Zooms from which takeoffs and landings show up, on the normal map like camp sites and restaurants,
  /// in the layer from further out. Labels come like the ones of restaurants, obstacles in the layer only.
  static int constexpr kTakeoffsZoom = 13;
  static int constexpr kLandingsZoom = 15;
  static int constexpr kLayerTakeoffsZoom = 10;
  static int constexpr kLayerLandingsZoom = 12;
  static int constexpr kLabelsZoom = 16;
  static int constexpr kObstaclesZoom = 13;
  static double constexpr kObstacleRadiusM = 1500.0;

  explicit FlightLayer(df::DrapeApi & drapeApi);

  /// Symbol sizes are in density-independent pixels.
  void SetVisualScale(double visualScale) { m_visualScale = visualScale; }

  /// The flight preparation layer: bigger colored symbols with sectors, and obstacles.
  void SetEnabled(bool enabled);
  bool IsEnabled() const { return m_enabled; }

  void SetForEachObstacleFn(ForEachObstacleFn fn) { m_forEachObstacle = std::move(fn); }

  /// Replaces all sites.
  void SetSites(std::vector<Site> && sites);
  void SetViewport(ScreenBase const & screen);

  /// The id of the site at |point|, within |tolerancePx| on the screen or inside its landing area.
  std::optional<std::string> HitTest(m2::PointD const & point, ScreenBase const & screen, double tolerancePx) const;

  /// Closed rings for the runs of neighbouring directions around |center|: wedges from the center or, with
  /// |innerRadius|, pieces of a ring between both radii (in mercator).
  static std::vector<std::vector<m2::PointD>> MakeSectors(m2::PointD const & center, uint16_t directions,
                                                          double radius, double innerRadius = 0);
  /// A closed circle of |radius| (in mercator).
  static std::vector<m2::PointD> MakeCircle(m2::PointD const & center, double radius, size_t pointsCount = 24);

private:
  bool IsVisible(Site const & site, int zoom) const;

  void Redraw();
  void Add(std::string const & id, df::DrapeApiLineData const & data);
  void DrawMapIcons(m2::RectD const & rect, int zoom, double unit);
  void DrawLayerSymbols(m2::RectD const & rect, int zoom, double unit);
  void DrawLabels(m2::RectD const & rect, int zoom);
  void DrawAreas(m2::RectD const & rect, int zoom);
  void DrawObstacles();

  df::DrapeApi & m_drapeApi;
  bool m_enabled = false;
  ForEachObstacleFn m_forEachObstacle;
  std::vector<Site> m_sites;
  std::optional<ScreenBase> m_screen;
  double m_visualScale = 1.0;

  // What is drawn now, for which area and scale.
  std::set<std::string> m_drawnIds;
  std::optional<int> m_drawnZoom;
  m2::RectD m_drawnRect;
  double m_drawnScale = 0;
};
