#pragma once

#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"
#include "geometry/screenbase.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <tuple>
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
    /// Drawn gray, e.g. a site for club members only.
    bool m_restricted = false;
    /// Drawn darker gray: a takeoff known only from OpenStreetMap, not confirmed by a flying site database.
    bool m_unconfirmed = false;
    /// How flyable the day is, a Rating, and whether it is especially good: the ring and star in the layer.
    uint8_t m_rating = 0;
    bool m_star = false;
    /// Gets a label, e.g. not when the map shows the site's name already.
    bool m_labeled = false;
    /// The closed ring of a known landing or takeoff area, or empty.
    std::vector<m2::PointD> m_area;
  };

  struct Obstacle
  {
    /// Each kind is drawn differently, so that the map still shows what it is below.
    enum class Kind : uint8_t
    {
      PowerLine,
      AerialLift,
      Motorway,
      WindTurbine,
      /// Masts, towers and chimneys.
      Mast
    };

    Kind m_kind = Kind::PowerLine;
    /// A line, or a single point for a wind turbine or a mast.
    std::vector<m2::PointD> m_points;
  };

  using ForEachObstacleFn = std::function<void(m2::RectD const & rect, std::function<void(Obstacle &&)> const & fn)>;

  /// Zooms from which takeoffs and landings show up, on the normal map like railway stations and camp sites
  /// (small icons first, medium ones closer, takeoffs sooner), in the layer from further out. Labels come like
  /// the ones of restaurants, areas from a 500 m scale bar, obstacles in the layer only.
  static int constexpr kTakeoffsZoom = 12;
  static int constexpr kMediumTakeoffsZoom = 13;
  static int constexpr kLandingsZoom = 13;
  static int constexpr kMediumLandingsZoom = 14;
  static int constexpr kAreasZoom = 14;
  static int constexpr kLayerTakeoffsZoom = 10;
  static int constexpr kLayerLandingsZoom = 12;
  static int constexpr kLabelsZoom = 16;
  static int constexpr kObstaclesZoom = 13;
  /// In the layer, takeoffs whose symbols would overlap are one symbol with their number, up to this zoom.
  static int constexpr kClustersMaxZoom = 15;
  static double constexpr kObstacleRadiusM = 1500.0;

  explicit FlightLayer(df::DrapeApi & drapeApi);

  /// Symbol sizes are in density-independent pixels.
  void SetVisualScale(double visualScale) { m_visualScale = visualScale; }

  /// The flight preparation layer: bigger colored symbols with sectors, and obstacles.
  void SetEnabled(bool enabled);
  bool IsEnabled() const { return m_enabled; }

  void SetForEachObstacleFn(ForEachObstacleFn fn) { m_forEachObstacle = std::move(fn); }

  /// How flyable a day is, from the weather of the site.
  enum Rating : uint8_t
  {
    kNotRated = 0,
    kRed,
    kYellow,
    kLightGreen,
    kGreen
  };

  /// Replaces all sites.
  void SetSites(std::vector<Site> && sites);
  /// Sets the rating of the sites with these ids, keeping the others.
  void SetRatings(std::vector<std::tuple<std::string, uint8_t, bool>> const & ratings);
  /// Told the center of the map and the zoom after each redraw in the layer, to rate the sites around.
  using ViewportFn = std::function<void(m2::PointD const & center, int zoom)>;
  void SetViewportFn(ViewportFn fn) { m_viewportFn = std::move(fn); }
  /// Aerial lifts missing in the maps, like the cableways for goods, from the flight sites file.
  void SetCables(std::vector<std::vector<m2::PointD>> && cables);
  void SetViewport(ScreenBase const & screen);

  /// A tapped site, or a symbol of several takeoffs to zoom into.
  struct Hit
  {
    std::string m_siteId;
    std::optional<m2::PointD> m_cluster;
  };

  /// The site or takeoffs symbol at |point|, within |tolerancePx| on the screen or inside a landing area.
  std::optional<Hit> HitTest(m2::PointD const & point, ScreenBase const & screen, double tolerancePx) const;

  /// A closed ring for each direction around |center|: a wedge from the center or, with |innerRadius|, a
  /// piece of a ring between both radii (in mercator). Neighbouring directions touch: 45 degrees each for
  /// 8 winds, 22.5 degrees for 16 winds.
  static std::vector<std::vector<m2::PointD>> MakeSectors(m2::PointD const & center, uint16_t directions,
                                                          double radius, double innerRadius = 0);
  /// A closed circle of |radius| (in mercator).
  static std::vector<m2::PointD> MakeCircle(m2::PointD const & center, double radius, size_t pointsCount = 24);

private:
  /// Takeoffs drawn as one symbol: their directions together, restricted only if all are.
  struct Cluster
  {
    m2::PointD m_point;
    size_t m_first = 0;
    size_t m_count = 0;
    uint16_t m_directions = 0;
    /// 0 colored, 1 gray, 2 dark gray: the lightest of its takeoffs.
    int m_shade = 2;
    uint8_t m_rating = 0;
    bool m_star = false;
  };

  bool IsVisible(Site const & site, int zoom) const;
  /// The visible takeoffs in |rect|, near ones together in the layer; |scale| in mercator per pixel.
  std::vector<Cluster> MakeTakeoffClusters(m2::RectD const & rect, int zoom, double scale) const;

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
  ViewportFn m_viewportFn;
  std::vector<Site> m_sites;
  std::vector<std::vector<m2::PointD>> m_cables;
  std::vector<m2::RectD> m_cableRects;
  std::optional<ScreenBase> m_screen;
  double m_visualScale = 1.0;

  // What is drawn now, for which area and scale.
  std::set<std::string> m_drawnIds;
  std::optional<int> m_drawnZoom;
  m2::RectD m_drawnRect;
  double m_drawnScale = 0;
  std::vector<Cluster> m_drawnClusters;
};
