#include "routing/car_restrictions.hpp"

#include "platform/platform.hpp"
#include "platform/settings.hpp"

#include "coding/reader.hpp"

#include "base/exception.hpp"
#include "base/logging.hpp"
#include "base/math.hpp"
#include "base/string_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace routing
{
namespace
{
std::string_view constexpr kHeightKey = "CarHeightCm";
std::string_view constexpr kVignettesKey = "CarVignettes";

// Both points of a clearance must lie this close to the road; the maps keep the OpenStreetMap nodes.
double constexpr kMatchDistanceM = 3.0;
double constexpr kMetersPerDegree = 111320.0;
double constexpr kCellsPerDegree = 100.0;

DECLARE_EXCEPTION(MaxHeightsFormatException, RootException);

// Lat/lon boxes as m2::RectD(minLon, minLat, maxLon, maxLat).
// Austria: ASFINAG "Streckenmaut", the only motorway sections the vignette does not cover.
m2::RectD constexpr kAustriaSpecialToll[] = {
    {11.36, 47.00, 11.53, 47.21},  // A13 Brenner Autobahn, Schönberg toll station to the Brenner pass
    {13.43, 47.00, 13.67, 47.28},  // A10 Tauern Autobahn, Tauern and Katschberg tunnels
    {13.97, 46.43, 14.09, 46.53},  // A11 Karawanken Autobahn, Karawanken tunnel
    {14.31, 47.57, 14.40, 47.67},  // A9 Pyhrn Autobahn, Bosruck tunnel
    {15.02, 47.18, 15.24, 47.32},  // A9 Pyhrn Autobahn, Gleinalm tunnel
    {10.10, 47.11, 10.28, 47.16},  // S16 Arlberg Schnellstraße, Arlberg tunnel
};

VignetteCountry const kVignetteCountries[] = {
    {"AT", "Austria", kAustriaSpecialToll},
    {"CZ", "Czech", {}},
};

bool IsMotorway(HighwayType type)
{
  return type == HighwayType::HighwayMotorway || type == HighwayType::HighwayMotorwayLink;
}

bool IsTrunk(HighwayType type)
{
  return type == HighwayType::HighwayTrunk || type == HighwayType::HighwayTrunkLink;
}

uint64_t CellKey(int64_t latCell, int64_t lonCell)
{
  return (static_cast<uint64_t>(latCell + (1 << 20)) << 32) | static_cast<uint64_t>(lonCell + (1 << 20));
}

int64_t ToCell(double degrees)
{
  return static_cast<int64_t>(std::floor(degrees * kCellsPerDegree));
}

// Distance in meters from |p| to the polyline, on a plane around |p|: clearances are matched within meters.
double DistanceToPolylineM(ms::LatLon const & p, std::vector<ms::LatLon> const & polyline)
{
  double const lonScale = std::cos(math::DegToRad(p.m_lat));
  auto const toPlane = [&](ms::LatLon const & ll)
  { return std::pair{(ll.m_lon - p.m_lon) * lonScale * kMetersPerDegree, (ll.m_lat - p.m_lat) * kMetersPerDegree}; };

  double best = std::numeric_limits<double>::max();
  for (size_t i = 0; i + 1 < polyline.size(); ++i)
  {
    auto const [ax, ay] = toPlane(polyline[i]);
    auto const [bx, by] = toPlane(polyline[i + 1]);
    double const dx = bx - ax;
    double const dy = by - ay;
    double const len2 = dx * dx + dy * dy;
    double t = len2 > 0 ? -(ax * dx + ay * dy) / len2 : 0;
    t = std::clamp(t, 0.0, 1.0);
    best = std::min(best, std::hypot(ax + t * dx, ay + t * dy));
  }
  return best;
}

bool InAnyRect(std::span<m2::RectD const> rects, std::vector<ms::LatLon> const & polyline)
{
  for (auto const & rect : rects)
    for (auto const & ll : polyline)
      if (rect.IsPointInside({ll.m_lon, ll.m_lat}))
        return true;
  return false;
}
}  // namespace

std::span<VignetteCountry const> GetVignetteCountries()
{
  return kVignetteCountries;
}

VignetteCountry const * FindVignetteCountry(std::string_view mwmName)
{
  for (auto const & country : kVignetteCountries)
  {
    // "Austria" and "Austria_Tyrol", but not a country whose name only starts the same.
    if (mwmName.starts_with(country.m_mwmPrefix) &&
        (mwmName.size() == country.m_mwmPrefix.size() || mwmName[country.m_mwmPrefix.size()] == '_' ||
         mwmName[country.m_mwmPrefix.size()] == ' '))
    {
      return &country;
    }
  }
  return nullptr;
}

bool IsSpecialTollRoad(VignetteCountry const & country, HighwayType type, std::vector<ms::LatLon> const & polyline)
{
  return (IsMotorway(type) || IsTrunk(type)) && InAnyRect(country.m_specialToll, polyline);
}

bool IsVignetteRoad(VignetteCountry const & country, HighwayType type, bool isToll,
                    std::vector<ms::LatLon> const & polyline)
{
  // Motorways need the vignette whether the toll is mapped or not; expressways are often mapped as trunk,
  // where only the toll tag tells them from ordinary trunk roads.
  if (!IsMotorway(type) && !(IsTrunk(type) && isToll))
    return false;
  return !IsSpecialTollRoad(country, type, polyline);
}

// MaxHeights --------------------------------------------------------------------------------------
MaxHeights::MaxHeights(std::vector<Entry> && entries) : m_entries(std::move(entries))
{
  for (uint32_t i = 0; i < m_entries.size(); ++i)
  {
    auto const & e = m_entries[i];
    uint64_t const a = CellKey(ToCell(e.m_a.m_lat), ToCell(e.m_a.m_lon));
    uint64_t const b = CellKey(ToCell(e.m_b.m_lat), ToCell(e.m_b.m_lon));
    m_cells[a].push_back(i);
    if (b != a)
      m_cells[b].push_back(i);
  }
}

// static
MaxHeights MaxHeights::Read(Reader const & reader)
{
  ReaderSource<Reader> src(reader);
  char magic[4];
  src.Read(magic, sizeof(magic));
  if (std::memcmp(magic, "MXH1", sizeof(magic)) != 0)
    MYTHROW(MaxHeightsFormatException, ("Unknown clearances file format"));

  auto const count = ReadPrimitiveFromSource<uint32_t>(src);
  size_t constexpr kEntrySize = 4 * sizeof(int32_t) + sizeof(uint16_t);
  if (src.Size() != static_cast<uint64_t>(count) * kEntrySize)
    MYTHROW(MaxHeightsFormatException, ("Clearances file size does not match", count, "entries"));

  auto const readLatLon = [&src]
  {
    auto const lat = ReadPrimitiveFromSource<int32_t>(src);
    auto const lon = ReadPrimitiveFromSource<int32_t>(src);
    return ms::LatLon(lat * 1e-7, lon * 1e-7);
  };

  std::vector<Entry> entries(count);
  for (auto & e : entries)
  {
    e.m_a = readLatLon();
    e.m_b = readLatLon();
    e.m_heightCm = ReadPrimitiveFromSource<uint16_t>(src);
  }
  return MaxHeights(std::move(entries));
}

// static
MaxHeights const & MaxHeights::Instance()
{
  static MaxHeights const instance = []
  {
    try
    {
      auto heights = Read(*GetPlatform().GetReader(std::string(kFileName)));
      LOG(LINFO, ("Clearances loaded:", heights.GetSize()));
      return heights;
    }
    catch (RootException const & e)
    {
      LOG(LWARNING, ("No clearances for car routing:", e.Msg()));
      return MaxHeights();
    }
  }();
  return instance;
}

uint16_t MaxHeights::GetMaxHeightCm(std::vector<ms::LatLon> const & polyline) const
{
  if (m_entries.empty() || polyline.size() < 2)
    return 0;

  double minLat = polyline.front().m_lat, maxLat = minLat;
  double minLon = polyline.front().m_lon, maxLon = minLon;
  for (auto const & ll : polyline)
  {
    minLat = std::min(minLat, ll.m_lat);
    maxLat = std::max(maxLat, ll.m_lat);
    minLon = std::min(minLon, ll.m_lon);
    maxLon = std::max(maxLon, ll.m_lon);
  }

  double const margin = kMatchDistanceM / kMetersPerDegree * 2;
  uint16_t result = 0;
  for (int64_t lat = ToCell(minLat - margin); lat <= ToCell(maxLat + margin); ++lat)
  {
    for (int64_t lon = ToCell(minLon - margin); lon <= ToCell(maxLon + margin); ++lon)
    {
      auto const it = m_cells.find(CellKey(lat, lon));
      if (it == m_cells.end())
        continue;
      for (uint32_t const i : it->second)
      {
        auto const & e = m_entries[i];
        if ((result == 0 || e.m_heightCm < result) && DistanceToPolylineM(e.m_a, polyline) <= kMatchDistanceM &&
            DistanceToPolylineM(e.m_b, polyline) <= kMatchDistanceM)
        {
          result = e.m_heightCm;
        }
      }
    }
  }
  return result;
}

// CarRestrictions ---------------------------------------------------------------------------------
bool CarRestrictions::HasVignette(std::string_view code) const
{
  return std::find(m_vignettes.begin(), m_vignettes.end(), code) != m_vignettes.end();
}

// static
CarRestrictions CarRestrictions::LoadFromSettings()
{
  CarRestrictions result;
  result.m_heightCm = LoadHeightCm();
  for (auto const code : strings::Tokenize(LoadVignettes(), ","))
    result.m_vignettes.emplace_back(code);
  return result;
}

// static
uint16_t CarRestrictions::LoadHeightCm()
{
  uint32_t heightCm = kDefaultHeightCm;
  settings::TryGet(kHeightKey, heightCm);
  return static_cast<uint16_t>(std::min<uint32_t>(heightCm, std::numeric_limits<uint16_t>::max()));
}

// static
void CarRestrictions::SaveHeightCm(uint16_t heightCm)
{
  settings::Set(kHeightKey, static_cast<uint32_t>(heightCm));
}

// static
std::string CarRestrictions::LoadVignettes()
{
  std::string codes;
  settings::TryGet(kVignettesKey, codes);
  return codes;
}

// static
void CarRestrictions::SaveVignettes(std::string const & codes)
{
  settings::Set(kVignettesKey, codes);
}
}  // namespace routing
