#pragma once

#include "routing_common/vehicle_model.hpp"

#include "geometry/latlon.hpp"
#include "geometry/rect2d.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

class Reader;

namespace routing
{
/// Maps&Fly: a country whose motorways are paid with a vignette instead of by distance.
struct VignetteCountry
{
  /// ISO 3166-1 code, as saved in the settings.
  std::string_view m_code;
  /// Map files of the country start with it, e.g. "Austria_Tyrol".
  std::string_view m_mwmPrefix;
  /// Lat/lon boxes of the motorway sections with a separate toll (Streckenmaut) that no vignette covers.
  std::span<m2::RectD const> m_specialToll;
};

std::span<VignetteCountry const> GetVignetteCountries();
/// The vignette country of a map file, nullptr for other countries.
VignetteCountry const * FindVignetteCountry(std::string_view mwmName);

/// Maps&Fly: OpenStreetMap clearances (maxheight) from a file next to the maps, which do not contain them.
/// A way is stored as two points at a quarter and three quarters of its length: a road matches it when both
/// lie on the road, so a bridge crossing above the low way does not.
class MaxHeights
{
public:
  struct Entry
  {
    ms::LatLon m_a;
    ms::LatLon m_b;
    uint16_t m_heightCm = 0;
  };

  static std::string_view constexpr kFileName = "maxheights.bin";

  MaxHeights() = default;
  explicit MaxHeights(std::vector<Entry> && entries);

  /// Format: "MXH1", uint32 count, then per entry int32 lat/lon of both points in 1e-7 degrees and uint16 cm,
  /// little endian.
  static MaxHeights Read(Reader const & reader);
  /// The file of the app resources, read once; empty without the file.
  static MaxHeights const & Instance();

  /// The lowest clearance on the polyline in cm, 0 when it has none.
  uint16_t GetMaxHeightCm(std::vector<ms::LatLon> const & polyline) const;
  size_t GetSize() const { return m_entries.size(); }

private:
  std::vector<Entry> m_entries;
  std::unordered_map<uint64_t, std::vector<uint32_t>> m_cells;
};

/// Maps&Fly: the car settings that the routing reads together with the avoided roads.
struct CarRestrictions
{
  static uint16_t constexpr kDefaultHeightCm = 300;

  /// Vehicle height in cm, 0 when clearances are ignored.
  uint16_t m_heightCm = 0;
  /// ISO codes of the countries with a bought vignette.
  std::vector<std::string> m_vignettes;

  bool HasVignette(std::string_view code) const;

  static CarRestrictions LoadFromSettings();
  static uint16_t LoadHeightCm();
  static void SaveHeightCm(uint16_t heightCm);
  /// Comma separated ISO codes, e.g. "AT,CZ".
  static std::string LoadVignettes();
  static void SaveVignettes(std::string const & codes);
};

/// Maps&Fly: what a car road gets from the restrictions of its country.
struct RoadRestrictions
{
  VignetteCountry const * m_country = nullptr;
  bool m_hasVignette = false;
  uint16_t m_heightCm = 0;
  MaxHeights const * m_maxHeights = nullptr;
};

/// True for a road of a vignette country that only the vignette pays for. |isToll| is the toll tag of the map.
bool IsVignetteRoad(VignetteCountry const & country, HighwayType type, bool isToll,
                    std::vector<ms::LatLon> const & polyline);
/// True when the road lies in a section with a separate toll.
bool IsSpecialTollRoad(VignetteCountry const & country, HighwayType type, std::vector<ms::LatLon> const & polyline);
}  // namespace routing
