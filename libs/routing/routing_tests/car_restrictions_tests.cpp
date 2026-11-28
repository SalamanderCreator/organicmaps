#include "testing/testing.hpp"

#include "routing/car_restrictions.hpp"
#include "routing/routing_options.hpp"

#include "platform/settings.hpp"

#include "coding/reader.hpp"
#include "coding/writer.hpp"

#include <string>
#include <vector>

namespace car_restrictions_tests
{
using namespace routing;
using Polyline = std::vector<ms::LatLon>;

// About 74 m east-west at 48° N.
Polyline const kRoad = {{48.0, 11.0}, {48.0, 11.0005}, {48.0, 11.001}};

MaxHeights::Entry WayEntry(Polyline const & way, uint16_t heightCm)
{
  auto const at = [&](double t)
  {
    return ms::LatLon(way.front().m_lat + (way.back().m_lat - way.front().m_lat) * t,
                      way.front().m_lon + (way.back().m_lon - way.front().m_lon) * t);
  };
  return {at(0.25), at(0.75), heightCm};
}

UNIT_TEST(VignetteCountry_ByMwmName)
{
  TEST(FindVignetteCountry("Austria_Tyrol"), ());
  TEST_EQUAL(FindVignetteCountry("Austria_Tyrol")->m_code, "AT", ());
  TEST_EQUAL(FindVignetteCountry("Austria")->m_code, "AT", ());
  TEST_EQUAL(FindVignetteCountry("Czech_Praha")->m_code, "CZ", ());
  TEST_EQUAL(FindVignetteCountry("Czech Republic")->m_code, "CZ", ());
  TEST(!FindVignetteCountry("Germany_Free State of Bavaria_Upper Bavaria_Munich"), ());
  TEST(!FindVignetteCountry("Australia_Victoria"), ());
  TEST(!FindVignetteCountry("Czechia_Fake"), ());
}

UNIT_TEST(VignetteRoad_MotorwaysAndSpecialToll)
{
  auto const & austria = *FindVignetteCountry("Austria_Tyrol");
  // A12 near Innsbruck, outside the Brenner section.
  Polyline const a12 = {{47.27, 11.45}, {47.27, 11.46}};
  // A13 Europabrücke.
  Polyline const a13 = {{47.13, 11.42}, {47.14, 11.42}};

  TEST(IsVignetteRoad(austria, HighwayType::HighwayMotorway, false /* isToll */, a12), ());
  TEST(IsVignetteRoad(austria, HighwayType::HighwayMotorwayLink, true /* isToll */, a12), ());
  TEST(!IsSpecialTollRoad(austria, HighwayType::HighwayMotorway, a12), ());

  TEST(!IsVignetteRoad(austria, HighwayType::HighwayMotorway, true /* isToll */, a13), ());
  TEST(IsSpecialTollRoad(austria, HighwayType::HighwayMotorway, a13), ());

  // Expressways mapped as trunk need the toll tag; other roads never take a vignette.
  TEST(IsVignetteRoad(austria, HighwayType::HighwayTrunk, true /* isToll */, a12), ());
  TEST(!IsVignetteRoad(austria, HighwayType::HighwayTrunk, false /* isToll */, a12), ());
  TEST(!IsVignetteRoad(austria, HighwayType::HighwayPrimary, true /* isToll */, a12), ());
  // A toll pass road in the box of a special toll section stays an ordinary toll road.
  TEST(!IsSpecialTollRoad(austria, HighwayType::HighwayPrimary, a13), ());

  auto const & czech = *FindVignetteCountry("Czech_Praha");
  TEST(IsVignetteRoad(czech, HighwayType::HighwayMotorway, false /* isToll */, a13), ());
}

UNIT_TEST(MaxHeights_MatchesTheLowWayOnly)
{
  // The low way under a bridge and the bridge crossing it in the middle.
  Polyline const bridge = {{47.9995, 11.0005}, {48.0005, 11.0005}};
  MaxHeights const heights({WayEntry(kRoad, 280)});

  TEST_EQUAL(heights.GetMaxHeightCm(kRoad), 280, ());
  TEST_EQUAL(heights.GetMaxHeightCm(bridge), 0, ());

  // A parallel road 10 m away.
  double const tenMeters = 10.0 / 111320.0;
  Polyline const parallel = {{48.0 + tenMeters, 11.0}, {48.0 + tenMeters, 11.001}};
  TEST_EQUAL(heights.GetMaxHeightCm(parallel), 0, ());

  // A longer road that contains the low way.
  Polyline const longer = {{48.0, 10.999}, {48.0, 11.003}};
  TEST_EQUAL(heights.GetMaxHeightCm(longer), 280, ());
}

UNIT_TEST(MaxHeights_NodesAndLowest)
{
  MaxHeights const heights({{{48.0, 11.0002}, {48.0, 11.0002}, 350}, WayEntry(kRoad, 310), WayEntry(kRoad, 420)});
  TEST_EQUAL(heights.GetMaxHeightCm(kRoad), 310, ());
  TEST_EQUAL(MaxHeights().GetMaxHeightCm(kRoad), 0, ());
}

UNIT_TEST(MaxHeights_CellBorders)
{
  // The road crosses the 0.01° cells, its clearance points lie in two of them.
  Polyline const road = {{48.0, 10.9995}, {48.0, 11.0005}};
  MaxHeights const heights({WayEntry(road, 250)});
  TEST_EQUAL(heights.GetMaxHeightCm(road), 250, ());
}

UNIT_TEST(MaxHeights_Read)
{
  std::vector<uint8_t> data;
  {
    MemWriter<std::vector<uint8_t>> writer(data);
    writer.Write("MXH1", 4);
    WriteToSink(writer, uint32_t{1});
    for (int32_t v : {480000000, 110002500, 480000000, 110007500})
      WriteToSink(writer, v);
    WriteToSink(writer, uint16_t{290});
  }

  auto const heights = MaxHeights::Read(MemReader(data.data(), data.size()));
  TEST_EQUAL(heights.GetSize(), 1, ());
  TEST_EQUAL(heights.GetMaxHeightCm(kRoad), 290, ());

  data[0] = 'X';
  bool thrown = false;
  try
  {
    MaxHeights::Read(MemReader(data.data(), data.size()));
  }
  catch (RootException const &)
  {
    thrown = true;
  }
  TEST(thrown, ());
}

UNIT_TEST(CarRestrictions_Settings)
{
  std::string savedVignettes;
  bool const hadVignettes = settings::Get("CarVignettes", savedVignettes);
  uint32_t savedHeight = 0;
  bool const hadHeight = settings::Get("CarHeightCm", savedHeight);

  settings::Delete("CarHeightCm");
  TEST_EQUAL(CarRestrictions::LoadHeightCm(), CarRestrictions::kDefaultHeightCm, ());

  CarRestrictions::SaveHeightCm(0);
  CarRestrictions::SaveVignettes("AT,CZ");
  auto const restrictions = CarRestrictions::LoadFromSettings();
  TEST_EQUAL(restrictions.m_heightCm, 0, ());
  TEST(restrictions.HasVignette("AT"), ());
  TEST(restrictions.HasVignette("CZ"), ());
  TEST(!restrictions.HasVignette("SK"), ());

  if (hadVignettes)
    settings::Set("CarVignettes", savedVignettes);
  else
    settings::Delete("CarVignettes");
  if (hadHeight)
    settings::Set("CarHeightCm", savedHeight);
  else
    settings::Delete("CarHeightCm");
}

UNIT_TEST(RoutingOptions_LowClearanceIsSoftAndExpensive)
{
  using Road = RoutingOptions::Road;
  TEST((RoutingOptions::kSoftAvoidOptions & Road::LowClearance) != 0, ());
  TEST_GREATER(RoutingOptions::GetSoftAvoidWeightFactor(Road::LowClearance | Road::Paved),
               RoutingOptions::GetSoftAvoidWeightFactor(Road::PathExpert), ());
  // The vignette only marks the road; it is never an avoid option.
  TEST_EQUAL(RoutingOptions::GetSupportedOptions(VehicleType::Car) & (Road::Vignette | Road::LowClearance), 0, ());
}
}  // namespace car_restrictions_tests
