#include "map/track.hpp"

#include "map/bookmark_helpers.hpp"
#include "map/user_mark_id_storage.hpp"

#include "geometry/mercator.hpp"
#include "geometry/parametrized_segment.hpp"

#include <algorithm>
#include <atomic>

namespace
{
Track::TerrainElevationsFn g_terrainElevationsFn;
std::atomic<bool> g_preferTerrainElevation = false;
}  // namespace

void Track::SetTerrainElevationsFn(TerrainElevationsFn fn)
{
  g_terrainElevationsFn = std::move(fn);
}

void Track::SetPreferTerrainElevation(bool prefer)
{
  g_preferTerrainElevation = prefer;
}

bool Track::IsTerrainElevationPreferred()
{
  return g_preferTerrainElevation;
}

Track::Track(kml::TrackData && data)
  : Base(data.m_id == kml::kInvalidTrackId ? UserMarkIdStorage::Instance().GetNextTrackId() : data.m_id)
  , m_data(std::move(data))
{
  m_data.m_id = GetId();
  CHECK(m_data.m_geometry.HasLines(), (m_data.m_id));
  ASSERT(m_data.m_geometry.IsValid(), (m_data.m_id));
}

void Track::CacheDataForInteraction() const
{
  m_interactionData = InteractionData{GetLengthsImpl(), GetLimitRectImpl()};
}

std::vector<Track::Lengths> Track::GetLengthsImpl() const
{
  double distance = 0;
  std::vector<Lengths> lengths;
  for (auto const & line : m_data.m_geometry.m_lines)
  {
    Lengths lineLengths;
    lineLengths.emplace_back(distance);
    for (size_t j = 1; j < line.size(); ++j)
    {
      auto const & pt1 = line[j - 1].GetPoint();
      auto const & pt2 = line[j].GetPoint();
      distance += mercator::DistanceOnEarth(pt1, pt2);
      lineLengths.emplace_back(distance);
    }
    lengths.emplace_back(std::move(lineLengths));
  }
  return lengths;
}

m2::RectD Track::GetLimitRectImpl() const
{
  m2::RectD limitRect;
  for (auto const & line : m_data.m_geometry.m_lines)
    for (auto const & pt : line)
      limitRect.Add(pt.GetPoint());
  return limitRect;
}

bool Track::HasAltitudes() const
{
  bool hasNonDefaultAltitude = false;

  for (auto const & line : m_data.m_geometry.m_lines)
  {
    for (auto const & pt : line)
    {
      if (pt.GetAltitude() == geometry::kInvalidAltitude)
        return false;
      if (!hasNonDefaultAltitude && pt.GetAltitude() != geometry::kDefaultAltitudeMeters)
        hasNonDefaultAltitude = true;
    }
  }

  return hasNonDefaultAltitude;
}

std::string Track::GetName() const
{
  return GetPreferredBookmarkStr(m_data.m_name);
}

void Track::SetName(std::string const & name)
{
  m_isDirty = true;
  kml::SetDefaultStr(m_data.m_name, name);
}

std::string Track::GetDescription() const
{
  return GetPreferredBookmarkStr(m_data.m_description);
}

void Track::SetDescription(std::string const & description)
{
  m_isDirty = true;
  kml::SetDefaultStr(m_data.m_description, description);
}

m2::RectD Track::GetLimitRect() const
{
  if (m_interactionData)
    return m_interactionData->m_limitRect;
  return GetLimitRectImpl();
}

double Track::GetLengthMeters() const
{
  if (!m_interactionData)
    CacheDataForInteraction();
  return m_interactionData->m_lengths.back().back();
}

double Track::GetLengthMetersImpl(size_t lineIndex, size_t ptIndex) const
{
  if (!m_interactionData)
    CacheDataForInteraction();
  auto const & lineLengths = m_interactionData->m_lengths[lineIndex];
  return lineLengths[ptIndex];
}

void Track::UpdateSelectionInfo(m2::PointD const & tapPoint, TrackSelectionInfo & info) const
{
  // Caller sets info.m_squareDist to the max allowed squared distance (mercator). Any segment
  // whose closest point is strictly closer replaces the current best.
  if (m_interactionData && m_interactionData->m_limitRect.SquaredDistance(tapPoint) >= info.m_squareDist)
    return;

  for (size_t lineIndex = 0; lineIndex < m_data.m_geometry.m_lines.size(); ++lineIndex)
  {
    auto const & line = m_data.m_geometry.m_lines[lineIndex];
    for (size_t ptIndex = 0; ptIndex + 1 < line.size(); ++ptIndex)
    {
      m2::ParametrizedSegment<m2::PointD> seg(line[ptIndex].GetPoint(), line[ptIndex + 1].GetPoint());
      auto const closestPoint = seg.ClosestPointTo(tapPoint);
      auto const squaredDist = closestPoint.SquaredLength(tapPoint);
      if (squaredDist >= info.m_squareDist)
        continue;

      info.m_title = GetName();
      info.m_color = GetColor(0);
      info.m_squareDist = squaredDist;
      info.m_trackId = m_data.m_id;
      info.m_trackPoint = closestPoint;

      auto const segDistInMeters = mercator::DistanceOnEarth(line[ptIndex].GetPoint(), closestPoint);
      info.m_distFromBegM = segDistInMeters + GetLengthMetersImpl(lineIndex, ptIndex);
    }
  }
}

df::DepthLayer Track::GetDepthLayer() const
{
  return m_isSelected ? df::DepthLayer::SelectedUserLineLayer : df::DepthLayer::UserLineLayer;
}

size_t Track::GetLayerCount() const
{
  return m_data.m_layers.size();
}

dp::Color Track::GetColor(size_t layerIndex) const
{
  CHECK_LESS(layerIndex, m_data.m_layers.size(), ());
  return dp::Color(m_data.m_layers[layerIndex].m_color.m_rgba);
}

void Track::SetColor(dp::Color color)
{
  m_isDirty = true;
  m_data.m_layers[0].m_color.m_rgba = color.GetRGBA();
}

float Track::GetWidth(size_t layerIndex) const
{
  CHECK_LESS(layerIndex, m_data.m_layers.size(), ());
  return static_cast<float>(m_data.m_layers[layerIndex].m_lineWidth);
}

bool Track::IsVisible() const
{
  return m_data.m_visible;
}

void Track::SetVisibility(bool visible)
{
  m_isDirty = true;
  m_data.m_visible = visible;
}

float Track::GetDepth(size_t layerIndex) const
{
  return layerIndex * 10;
}

void Track::ForEachGeometry(GeometryFnT && fn) const
{
  for (auto const & line : m_data.m_geometry.m_lines)
  {
    std::vector<m2::PointD> points;
    points.reserve(line.size());
    for (auto const & pt : line)
      points.push_back(pt.GetPoint());

    fn(std::move(points));
  }
}

void Track::Attach(kml::MarkGroupId groupId)
{
  ASSERT_EQUAL(m_groupID, kml::kInvalidMarkGroupId, ());
  m_isDirty = true;
  m_groupID = groupId;
}

void Track::Detach()
{
  m_isDirty = true;
  m_groupID = kml::kInvalidMarkGroupId;
}

m2::PointD Track::GetPoint(double distanceInMeters) const
{
  if (!m_interactionData)
    CacheDataForInteraction();

  auto const & lengths = m_interactionData->m_lengths;
  ASSERT(!lengths.empty(), ());

  for (size_t lineIndex = 0; lineIndex < lengths.size(); ++lineIndex)
  {
    auto const & lineLengths = lengths[lineIndex];
    ASSERT(!lineLengths.empty(), ());

    if (distanceInMeters > lineLengths.back())
      continue;

    if (distanceInMeters <= lineLengths.front())
      return m_data.m_geometry.m_lines[lineIndex].front().GetPoint();

    auto const it = std::upper_bound(lineLengths.begin(), lineLengths.end(), distanceInMeters);
    if (it == lineLengths.end())
      return m_data.m_geometry.m_lines[lineIndex].back().GetPoint();

    ASSERT(it != lineLengths.begin(), ());
    size_t const ptIdx = std::distance(lineLengths.begin(), it) - 1;

    auto const & line = m_data.m_geometry.m_lines[lineIndex];
    auto const segLen = lineLengths[ptIdx + 1] - lineLengths[ptIdx];
    if (segLen < 1e-9)
      return line[ptIdx].GetPoint();

    double const f = (distanceInMeters - lineLengths[ptIdx]) / segLen;
    auto const & p1 = line[ptIdx].GetPoint();
    auto const & p2 = line[ptIdx + 1].GetPoint();
    return p1 + (p2 - p1) * f;
  }

  // Distance beyond last line — return last point.
  return m_data.m_geometry.m_lines.back().back().GetPoint();
}

kml::MultiGeometry::LineT Track::GetGeometry() const
{
  kml::MultiGeometry::LineT geometry;
  for (auto const & line : m_data.m_geometry.m_lines)
    for (size_t i = 0; i < line.size(); ++i)
      geometry.emplace_back(line[i]);
  return geometry;
}

std::pair<TrackStatistics, ElevationInfo const *> Track::GetStatisticsAndElevationInfo() const
{
  TrackStatistics ts;
  ts.m_length = GetLengthMeters();
  ts.CalculateDuration(m_data.m_geometry);

  auto const * ei = GetElevationInfo();
  if (ei)
  {
    auto const altInfo = ei->CalculateAltitudesInfo(GetElevationThreshold());
    ts.m_ascent = altInfo.GetTotalAscent();
    ts.m_descent = altInfo.GetTotalDescent();
    ts.m_minElevation = altInfo.m_minAltitude;
    ts.m_maxElevation = altInfo.m_maxAltitude;
  }
  return {ts, ei};
}

ElevationInfo const * Track::GetElevationInfo() const
{
  bool const preferTerrain = IsTerrainElevationPreferred();
  if (m_elevationInfo && m_elevationPrefersTerrain == preferTerrain)
    return &(*m_elevationInfo);

  m_elevationInfo.reset();
  bool const hasAltitudes = HasAltitudes();
  std::optional<std::vector<ElevationInfo::GeometryLine>> terrainLines;
  bool terrainOnly = false;
  if (preferTerrain || !hasAltitudes)
    terrainLines = GetTerrainLines(preferTerrain, terrainOnly);
  // Not cached: the terrain may still be downloaded.
  if (!terrainLines && !hasAltitudes)
    return nullptr;

  m_elevationInfo = ElevationInfo(terrainLines ? *terrainLines : GetData().m_geometry.m_lines);
  m_elevationPrefersTerrain = preferTerrain;
  m_terrainLines = std::move(terrainLines);
  m_terrainOnly = m_terrainLines && terrainOnly;

  // Relation tracks from MWM and the terrain have cleaner altitude data than raw GPS tracks.
  if (m_data.m_id != kml::kTempRelationTrackId && !m_terrainOnly)
    m_elevationInfo->SmoothSlopeOutliers();
  m_elevationInfo->Simplify();
  return &(*m_elevationInfo);
}

ElevationInfo::Altitude Track::GetElevationThreshold() const
{
  ASSERT(m_elevationInfo, ("Call GetElevationInfo() first"));
  // Relation tracks from MWM and the terrain have cleaner altitude data than raw GPS tracks.
  return m_data.m_id == kml::kTempRelationTrackId || m_terrainOnly ? ElevationInfo::kDefThresholdMWM
                                                                   : ElevationInfo::kDefThresholdGPS;
}

kml::MultiGeometry Track::GetGeometryWithElevation() const
{
  auto geometry = m_data.m_geometry;
  if (GetElevationInfo() && m_terrainLines)
    geometry.m_lines = *m_terrainLines;
  return geometry;
}

std::optional<std::vector<ElevationInfo::GeometryLine>> Track::GetTerrainLines(bool preferTerrain,
                                                                               bool & terrainOnly) const
{
  if (!g_terrainElevationsFn)
    return std::nullopt;

  auto lines = m_data.m_geometry.m_lines;
  // A track with only zero heights has none, e.g. a KML without altitudes.
  bool const hasOwn = std::ranges::any_of(lines, [](auto const & line)
  {
    return std::ranges::any_of(line, [](geometry::PointWithAltitude const & pt)
    {
      auto const altitude = pt.GetAltitude();
      return altitude != geometry::kInvalidAltitude && altitude != geometry::kDefaultAltitudeMeters;
    });
  });
  bool const replaceAll = preferTerrain || !hasOwn;
  terrainOnly = replaceAll;

  // All the points, also with own heights, to recognize a flight.
  std::vector<m2::PointD> points;
  for (auto const & line : lines)
    for (auto const & pt : line)
      points.push_back(pt.GetPoint());
  auto const elevations = g_terrainElevationsFn(points);
  CHECK_EQUAL(elevations.size(), points.size(), ());

  // A flight, e.g. by paraglider, keeps its own heights (Marisa): more than 5% of them far above the terrain.
  // GPS noise and the ellipsoid/geoid difference stay well below the margin.
  if (hasOwn)
  {
    double constexpr kFlightMarginM = 150;
    size_t above = 0, own = 0;
    size_t i = 0;
    for (auto const & line : lines)
    {
      for (auto const & pt : line)
      {
        auto const & elevation = elevations[i++];
        if (!elevation || pt.GetAltitude() == geometry::kInvalidAltitude)
          continue;
        ++own;
        if (pt.GetAltitude() > *elevation + kFlightMarginM)
          ++above;
      }
    }
    if (above * 20 > own)
      return std::nullopt;
  }

  size_t i = 0;
  for (auto & line : lines)
  {
    for (auto & pt : line)
    {
      auto const & elevation = elevations[i++];
      if (!replaceAll && pt.GetAltitude() != geometry::kInvalidAltitude)
        continue;
      if (!elevation)
        return std::nullopt;
      pt.SetAltitude(static_cast<geometry::Altitude>(std::lround(*elevation)));
    }
  }
  return lines;
}

double Track::GetDurationInSeconds() const
{
  TrackStatistics ts;
  ts.CalculateDuration(m_data.m_geometry);
  return ts.m_duration;
}
