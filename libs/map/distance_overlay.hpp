#pragma once

#include "geometry/point2d.hpp"
#include "geometry/rect2d.hpp"
#include "geometry/screenbase.hpp"

#include <optional>
#include <string>
#include <vector>

namespace df
{
class DrapeApi;
}  // namespace df

/// Maps&Fly: distance circles (500 m, 1 km, 2 km) around the own position and the selected place,
/// and a dashed line from the selected place to the own position with the straight-line distance
/// at the place. Both can be switched on and off and are drawn with the DrapeApi.
class DistanceOverlay
{
public:
  static double constexpr kCircleRadiiM[] = {500.0, 1000.0, 2000.0};

  explicit DistanceOverlay(df::DrapeApi & drapeApi);

  void SetCirclesEnabled(bool enabled);
  bool AreCirclesEnabled() const { return m_circlesEnabled; }
  void SetLineEnabled(bool enabled);
  bool IsLineEnabled() const { return m_lineEnabled; }

  void SetMyPosition(std::optional<m2::PointD> const & position);
  /// The selected place, or nullopt for no selection or a selected track.
  void SetSelection(std::optional<m2::PointD> const & point);
  void SetViewport(ScreenBase const & screen);

  /// A closed polygon starting in the north of |center|.
  static std::vector<m2::PointD> MakeCircle(m2::PointD const & center, double radiusM);

private:
  void UpdateCircles(std::string const & prefix, std::optional<m2::PointD> const & center, bool isSelection);
  void UpdateLine();
  void ClearLine();

  df::DrapeApi & m_drapeApi;
  bool m_circlesEnabled = false;
  bool m_lineEnabled = false;
  std::optional<m2::PointD> m_myPosition;
  std::optional<m2::PointD> m_selection;
  std::optional<ScreenBase> m_screen;
  // The area and scale the dashed line was clipped and dashed for.
  m2::RectD m_lineRect;
  double m_lineScale = 0;
  bool m_hasLine = false;
};
