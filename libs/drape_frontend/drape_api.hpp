#pragma once

#include "drape_frontend/drape_engine_safe_ptr.hpp"

#include "drape/color.hpp"
#include "drape/pointers.hpp"

#include "geometry/point2d.hpp"

#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace df
{
struct DrapeApiLineData
{
  DrapeApiLineData() = default;

  DrapeApiLineData(std::vector<m2::PointD> const & points, dp::Color const & color) : m_points(points), m_color(color)
  {}

  DrapeApiLineData & ShowPoints(bool markPoints)
  {
    m_showPoints = true;
    m_markPoints = markPoints;
    return *this;
  }

  DrapeApiLineData & Width(float width)
  {
    m_width = width;
    return *this;
  }

  DrapeApiLineData & ShowId()
  {
    m_showId = true;
    return *this;
  }

  /// Dashes of one of the patterns in data/patterns.txt (in dp). |baseGtoPScale| is the 1 / scale of
  /// the screen the line is built for, as dashes keep their pixel length only around that zoom.
  DrapeApiLineData & Dashed(std::vector<double> const & pattern, double baseGtoPScale)
  {
    m_pattern = pattern;
    m_baseGtoPScale = baseGtoPScale;
    return *this;
  }

  /// Only the points, no line through them: one entry for many unconnected point symbols.
  DrapeApiLineData & PointsOnly()
  {
    m_showPoints = true;
    m_noLine = true;
    return *this;
  }

  /// Fills the closed ring of the points with |color| below the line, e.g. a translucent area.
  DrapeApiLineData & Fill(dp::Color const & color)
  {
    m_fillColor = color;
    m_fill = true;
    return *this;
  }

  /// More polylines drawn like the line through the points, e.g. the same symbol at many places.
  DrapeApiLineData & Polylines(std::vector<std::vector<m2::PointD>> lines)
  {
    m_polylines = std::move(lines);
    return *this;
  }

  /// Rings filled with |color|, e.g. many symbol backgrounds in one entry.
  DrapeApiLineData & FillRings(std::vector<std::vector<m2::PointD>> rings, dp::Color const & color)
  {
    m_fillRings = std::move(rings);
    m_fillColor = color;
    return *this;
  }

  /// A text drawn next to the first point, optionally with a white halo.
  DrapeApiLineData & Label(std::string const & label, bool halo = false)
  {
    m_label = label;
    m_labelHalo = halo;
    return *this;
  }

  /// Font size of the label, 14 by default.
  DrapeApiLineData & LabelSize(uint8_t size)
  {
    m_labelSize = size;
    return *this;
  }

  std::vector<m2::PointD> m_points;
  float m_width = 1.0f;
  dp::Color m_color;

  std::vector<double> m_pattern;
  double m_baseGtoPScale = 1.0;
  std::string m_label;
  bool m_labelHalo = false;

  uint8_t m_labelSize = 14;
  std::vector<std::vector<m2::PointD>> m_polylines;
  std::vector<std::vector<m2::PointD>> m_fillRings;
  dp::Color m_fillColor;
  bool m_fill = false;
  bool m_noLine = false;

  bool m_showPoints = false;
  bool m_markPoints = false;
  bool m_showId = false;
};

class DrapeApi
{
public:
  using TLines = std::unordered_map<std::string, DrapeApiLineData>;

  DrapeApi() = default;

  void SetDrapeEngine(ref_ptr<DrapeEngine> engine);

  void AddLine(std::string const & id, DrapeApiLineData const & data);
  void RemoveLine(std::string const & id);
  void Clear();
  void Invalidate();

private:
  DrapeEngineSafePtr m_engine;
  TLines m_lines;
};
}  // namespace df
