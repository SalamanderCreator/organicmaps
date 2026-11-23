#pragma once

#include "routing/vehicle_mask.hpp"

#include "base/small_map.hpp"

#include <optional>
#include <string>
#include <type_traits>

namespace routing
{
enum class RouterType;

class RoutingOptions
{
public:
  enum Road : uint16_t
  {
    Usual = 1u << 0,
    Toll = 1u << 1,
    Motorway = 1u << 2,
    Ferry = 1u << 3,
    Dirty = 1u << 4,
    Steps = 1u << 5,
    // Maps&Fly: paths graded from sac_scale/trail_visibility by the generator (_path_grade).
    // Difficult is about SAC T3, Expert is T4-T6. They are avoided softly, see kSoftAvoidOptions.
    PathDifficult = 1u << 6,
    PathExpert = 1u << 7,
    // Maps&Fly: mapped paved surfaces and, without a mapped surface, roads other than tracks and
    // paths. Avoiding it softly prefers natural ways ("schön" instead of "schnell").
    Paved = 1u << 8,

    Max = (1u << 8) + 1
  };

  using RoadType = std::underlying_type_t<Road>;

  /// Avoided roads of these types stay routable with a weight penalty, so a stop on such a path
  /// can still be reached and the route reports it instead of failing.
  static RoadType constexpr kSoftAvoidOptions = PathDifficult | PathExpert | Paved;

  /// Weight factor of a road with the given avoided soft options, 1 without any.
  static double GetSoftAvoidWeightFactor(RoadType avoidedOnRoad);

  RoutingOptions() = default;
  explicit RoutingOptions(RoadType mask) : m_options(mask) {}

  static RoutingOptions LoadFromSettings(VehicleType vehicleType);
  static void SaveToSettings(VehicleType vehicleType, RoutingOptions options);
  static RoadType GetSupportedOptions(VehicleType vehicleType);
  static std::optional<VehicleType> GetVehicleType(RouterType routerType);
  static std::optional<Road> RoadFromId(uint32_t id);

  static bool LoadRouteOptimizationFromSettings();
  static void SaveRouteOptimizationToSettings(bool enabled);

  void Add(Road type);
  void Remove(Road type);
  bool Has(Road type) const;

  RoadType GetOptions() const { return m_options; }

private:
  RoadType m_options = 0;
};

class RoutingOptionsClassifier
{
public:
  std::optional<RoutingOptions::Road> Get(uint32_t type) const;
  /// True for the surface types (psurface-*) the generator adds for a mapped surface.
  static bool IsSurface(uint32_t type);
  static RoutingOptionsClassifier const & Instance(VehicleType vehicleType);

private:
  explicit RoutingOptionsClassifier(bool includeDirtyRoadClasses);

  base::SmallMap<uint32_t, RoutingOptions::Road> m_data;
};

std::string DebugPrint(RoutingOptions const & routingOptions);
std::string DebugPrint(RoutingOptions::Road type);

/// Options guard for debugging/tests.
class RoutingOptionSetter
{
public:
  explicit RoutingOptionSetter(RoutingOptions::RoadType roadsMask, VehicleType vehicleType = VehicleType::Car);
  ~RoutingOptionSetter();

private:
  VehicleType m_vehicleType;
  std::optional<std::string> m_saved;
};
}  // namespace routing
