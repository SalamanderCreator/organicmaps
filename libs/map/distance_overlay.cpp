#include "map/distance_overlay.hpp"

#include "drape_frontend/drape_api.hpp"

#include "platform/distance.hpp"

#include "geometry/mercator.hpp"
#include "geometry/rect_intersect.hpp"

#include "base/math.hpp"

#include <cmath>

namespace
{
std::string_view constexpr kMyPositionPrefix = "distance_circle_me_";
std::string_view constexpr kSelectionPrefix = "distance_circle_sel_";
std::string_view constexpr kLineId = "distance_line";
std::string_view constexpr kLineLabelId = "distance_line_label";

dp::Color const kMyPositionColor(30, 100, 200, 220);
dp::Color const kSelectionColor(220, 90, 0, 220);
dp::Color const kLineColor(30, 100, 200, 255);

// One of the patterns in data/patterns.txt.
std::vector<double> const kLinePattern = {10.0, 10.0};

// Smaller moves of the own position are not worth redrawing.
double constexpr kMinPositionChangeM = 2.0;

std::string MakeId(std::string_view prefix, size_t index)
{
  return std::string(prefix) + std::to_string(index);
}
}  // namespace

DistanceOverlay::DistanceOverlay(df::DrapeApi & drapeApi) : m_drapeApi(drapeApi) {}

void DistanceOverlay::SetCirclesEnabled(bool enabled)
{
  if (m_circlesEnabled == enabled)
    return;
  m_circlesEnabled = enabled;
  UpdateCircles(std::string(kMyPositionPrefix), m_myPosition, false /* isSelection */);
  UpdateCircles(std::string(kSelectionPrefix), m_selection, true /* isSelection */);
}

void DistanceOverlay::SetLineEnabled(bool enabled)
{
  if (m_lineEnabled == enabled)
    return;
  m_lineEnabled = enabled;
  UpdateLine();
}

void DistanceOverlay::SetMyPosition(std::optional<m2::PointD> const & position)
{
  if (position && m_myPosition && mercator::DistanceOnEarth(*position, *m_myPosition) < kMinPositionChangeM)
    return;
  if (!position && !m_myPosition)
    return;

  m_myPosition = position;
  UpdateCircles(std::string(kMyPositionPrefix), m_myPosition, false /* isSelection */);
  UpdateLine();
}

void DistanceOverlay::SetSelection(std::optional<m2::PointD> const & point)
{
  if (point == m_selection)
    return;
  m_selection = point;
  UpdateCircles(std::string(kSelectionPrefix), m_selection, true /* isSelection */);
  UpdateLine();
}

void DistanceOverlay::SetViewport(ScreenBase const & screen)
{
  m_screen = screen;
  if (!m_hasLine)
    return;
  // Rebuild the line when the screen leaves the clipped area or the dashes changed too much in size.
  double const scaleRatio = screen.GetScale() / m_lineScale;
  if (!m_lineRect.IsRectInside(screen.ClipRect()) || scaleRatio < 0.7 || scaleRatio > 1.4)
    UpdateLine();
}

// static
std::vector<m2::PointD> DistanceOverlay::MakeCircle(m2::PointD const & center, double radiusM)
{
  size_t constexpr kPointsCount = 96;
  // Mercator is conformal, so a small circle stays a circle with the local scale.
  double const radius = mercator::RectByCenterXYAndSizeInMeters(center, radiusM).SizeX() / 2;
  std::vector<m2::PointD> points;
  points.reserve(kPointsCount + 1);
  for (size_t i = 0; i <= kPointsCount; ++i)
  {
    double const angle = math::pi2 - 2 * math::pi * static_cast<double>(i) / kPointsCount;
    points.emplace_back(center.x + radius * std::cos(angle), center.y + radius * std::sin(angle));
  }
  return points;
}

void DistanceOverlay::UpdateCircles(std::string const & prefix, std::optional<m2::PointD> const & center,
                                    bool isSelection)
{
  for (size_t i = 0; i < std::size(kCircleRadiiM); ++i)
  {
    auto const id = MakeId(prefix, i);
    if (!m_circlesEnabled || !center)
    {
      m_drapeApi.RemoveLine(id);
      continue;
    }
    df::DrapeApiLineData data(MakeCircle(*center, kCircleRadiiM[i]), isSelection ? kSelectionColor : kMyPositionColor);
    data.Width(2.0f).Label(platform::Distance::CreateFormatted(kCircleRadiiM[i]).ToString());
    m_drapeApi.AddLine(id, data);
  }
}

void DistanceOverlay::ClearLine()
{
  if (!m_hasLine)
    return;
  m_drapeApi.RemoveLine(std::string(kLineId));
  m_drapeApi.RemoveLine(std::string(kLineLabelId));
  m_hasLine = false;
}

void DistanceOverlay::UpdateLine()
{
  if (!m_lineEnabled || !m_selection || !m_myPosition || !m_screen)
  {
    ClearLine();
    return;
  }

  // The label at the place, also when the line is outside the screen.
  double const distanceM = mercator::DistanceOnEarth(*m_selection, *m_myPosition);
  df::DrapeApiLineData label({*m_selection}, kLineColor);
  label.Label(platform::Distance::CreateFormatted(distanceM).ToString(), true /* halo */);
  m_drapeApi.AddLine(std::string(kLineLabelId), label);

  // Dashes are built for the current zoom, so only the part around the screen is drawn.
  m_lineRect = m_screen->ClipRect();
  m_lineRect.Scale(3.0);
  m_lineScale = m_screen->GetScale();
  m2::PointD from = *m_selection;
  m2::PointD to = *m_myPosition;
  if (m2::Intersect(m_lineRect, from, to) && !from.EqualDxDy(to, kMwmPointAccuracy))
  {
    df::DrapeApiLineData line({from, to}, kLineColor);
    line.Width(3.0f).Dashed(kLinePattern, 1.0 / m_lineScale);
    m_drapeApi.AddLine(std::string(kLineId), line);
  }
  else
  {
    m_drapeApi.RemoveLine(std::string(kLineId));
  }
  m_hasLine = true;
}
