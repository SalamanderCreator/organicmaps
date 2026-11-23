#include "routing_manager.hpp"

#include "map/routing_mark.hpp"

#include "routing/absent_regions_finder.hpp"
#include "routing/checkpoint_predictor.hpp"
#include "routing/index_router.hpp"
#include "routing/route.hpp"
#include "routing/routing_callbacks.hpp"
#include "routing/routing_options.hpp"
#include "routing/ruler_router.hpp"
#include "routing/speed_camera.hpp"

#include "storage/country_info_getter.hpp"
#include "storage/routing_helpers.hpp"

#include "indexer/classificator.hpp"
#include "indexer/data_source.hpp"
#include "indexer/ftypes_matcher.hpp"

#include "drape_frontend/drape_engine.hpp"
#include "drape_frontend/visual_params.hpp"

#include "routing_common/num_mwm_id.hpp"

#include "platform/country_file.hpp"
#include "platform/distance.hpp"
#include "platform/duration.hpp"
#include "platform/platform.hpp"

#include "geometry/algorithm.hpp"
#include "geometry/mercator.hpp"  // kPointEqualityEps
#include "geometry/parametrized_segment.hpp"

#include "coding/file_writer.hpp"

#include "base/logging.hpp"
#include "base/scope_guard.hpp"
#include "base/small_map.hpp"
#include "base/stl_helpers.hpp"
#include "base/string_utils.hpp"

#include <glaze/json.hpp>

#include <algorithm>
#include <map>

using namespace routing;

namespace route_points_json
{
struct RoutePointJson
{
  int type = 0;
  std::string title;
  std::string subtitle;
  double x = 0.0;
  double y = 0.0;
  bool replaceWithMyPosition = false;
};

RoutePointJson ToRoutePointJson(RouteMarkData const & data)
{
  return {.type = static_cast<int>(data.m_pointType),
          .title = data.m_title,
          .subtitle = data.m_subTitle,
          .x = data.m_position.x,
          .y = data.m_position.y,
          .replaceWithMyPosition = data.m_replaceWithMyPositionAfterRestart};
}

RouteMarkData ToRouteMarkData(RoutePointJson const & point)
{
  RouteMarkData data;
  data.m_pointType = static_cast<RouteMarkType>(point.type);
  data.m_title = point.title;
  data.m_subTitle = point.subtitle;
  data.m_position = {point.x, point.y};
  data.m_replaceWithMyPositionAfterRestart = point.replaceWithMyPosition;
  return data;
}
}  // namespace route_points_json

namespace
{
std::string_view constexpr kRouterTypeKey = "router";

double constexpr kRouteScaleMultiplier = 1.5;

std::string const kRoutePointsFile = "route_points.dat";

uint32_t constexpr kInvalidTransactionId = 0;

void FillTurnsDistancesForRendering(std::vector<RouteSegment> const & segments, double baseDistance,
                                    std::vector<double> & turns)
{
  using namespace routing::turns;
  turns.clear();
  turns.reserve(segments.size());
  for (auto const & s : segments)
  {
    auto const & t = s.GetTurn();
    CHECK_NOT_EQUAL(t.m_turn, CarDirection::Count, ());
    // We do not render some of the turn directions.
    if (t.m_turn == CarDirection::None || t.m_turn == CarDirection::StartAtEndOfStreet ||
        t.m_turn == CarDirection::StayOnRoundAbout || t.m_turn == CarDirection::ReachedYourDestination)
    {
      continue;
    }
    turns.push_back(s.GetDistFromBeginningMerc() - baseDistance);
  }
}

void FillTrafficForRendering(std::vector<RouteSegment> const & segments, std::vector<traffic::SpeedGroup> & traffic)
{
  traffic.clear();
  traffic.reserve(segments.size());
  for (auto const & s : segments)
    traffic.push_back(s.GetTraffic());
}

RouteMarkData GetLastPassedPoint(BookmarkManager * bmManager, std::vector<RouteMarkData> const & points)
{
  ASSERT_GREATER_OR_EQUAL(points.size(), 2, ());
  ASSERT(points[0].m_pointType == RouteMarkType::Start, ());
  RouteMarkData data = points[0];

  for (int i = static_cast<int>(points.size()) - 1; i >= 0; i--)
  {
    if (points[i].m_isPassed)
    {
      data = points[i];
      break;
    }
  }

  // Last passed point will be considered as start point.
  data.m_pointType = RouteMarkType::Start;
  data.m_intermediateIndex = 0;
  if (data.m_isMyPosition)
  {
    data.m_position = bmManager->MyPositionMark().GetPivot();
    data.m_isMyPosition = false;
    data.m_replaceWithMyPositionAfterRestart = true;
  }

  return data;
}

std::string SerializeRoutePoints(std::vector<RouteMarkData> const & points)
{
  ASSERT_GREATER_OR_EQUAL(points.size(), 2, ());
  std::vector<route_points_json::RoutePointJson> pointsJson;
  pointsJson.reserve(points.size());
  for (auto const & p : points)
    pointsJson.push_back(route_points_json::ToRoutePointJson(p));

  std::string buffer;
  if (auto const error = glz::write_json(pointsJson, buffer); error)
    MYTHROW(RootException, (glz::format_error(error)));
  return buffer;
}

std::vector<RouteMarkData> DeserializeRoutePoints(std::string const & data)
{
  std::vector<route_points_json::RoutePointJson> pointsJson;
  glz::opts constexpr opts{.error_on_unknown_keys = false, .error_on_missing_keys = false};
  if (auto const error = glz::read<opts>(pointsJson, data); error || pointsJson.empty())
    return {};

  std::vector<RouteMarkData> result;
  result.reserve(pointsJson.size());
  for (auto const & pointJson : pointsJson)
  {
    auto point = route_points_json::ToRouteMarkData(pointJson);
    if (point.m_position.EqualDxDy(m2::PointD::Zero(), mercator::kPointEqualityEps))
      continue;

    result.push_back(std::move(point));
  }

  if (result.size() < 2)
    return {};

  return result;
}

// Maps a barrier point-feature classificator type (stored in routing::Route::GetWarnings) to a UI
// warning mark type. Mirrors ftypes::IsWayChecker; this is the single place to extend when adding
// a new barrier warning kind.
class BarrierWarningChecker : public ftypes::BaseChecker
{
public:
  BarrierWarningChecker()
  {
    Classificator const & c = classif();
    std::pair<char const *, RoadWarningMarkType> const types[] = {
        {"gate", RoadWarningMarkType::Gate},
        {"lift_gate", RoadWarningMarkType::LiftGate},
    };

    m_marks.Reserve(std::size(types));
    for (auto const & e : types)
    {
      uint32_t const type = c.GetTypeByPath({"barrier", e.first});
      m_types.push_back(type);
      m_marks.Insert(type, e.second);
    }
    m_marks.FinishBuilding();
  }

  DECLARE_CHECKER_INSTANCE(BarrierWarningChecker);

  /// @returns RoadWarningMarkType::Count if |type| is not a known barrier warning.
  RoadWarningMarkType GetWarningType(uint32_t type) const
  {
    if (auto const * res = m_marks.Find(ftype::Trunc(type, 2)))
      return *res;
    return RoadWarningMarkType::Count;
  }

private:
  base::SmallMap<uint32_t, RoadWarningMarkType> m_marks;
};

drape_ptr<df::Subroute> CreateDrapeSubroute(std::vector<RouteSegment> const & segments, m2::PointD const & startPt,
                                            double baseDistance, double baseDepth, routing::RouterType routerType)
{
  auto subroute = make_unique_dp<df::Subroute>();
  subroute->m_baseDistance = baseDistance;
  subroute->m_baseDepthIndex = baseDepth;

  auto constexpr kBias = 1.0;

  if (routerType == RouterType::Transit)
  {
    subroute->m_headFakeDistance = -kBias;
    subroute->m_tailFakeDistance = kBias;
    subroute->m_polyline.Add(startPt);
    return subroute;
  }

  std::vector<m2::PointD> points;
  points.reserve(segments.size() + 1);
  points.push_back(startPt);
  for (auto const & s : segments)
    points.push_back(s.GetJunction().GetPoint());

  if (points.size() < 2)
  {
    LOG(LWARNING, ("Invalid subroute. Points number =", points.size()));
    return nullptr;
  }

  if (routerType == RouterType::Ruler)
  {
    auto const subrouteLen = segments.back().GetDistFromBeginningMerc() - baseDistance;
    subroute->m_headFakeDistance = -kBias;
    subroute->m_tailFakeDistance = subrouteLen + kBias;
    subroute->m_polyline = m2::PolylineD(std::move(points));
    return subroute;
  }

  // We support visualization of fake edges only in the head and in the tail of subroute.
  auto constexpr kInvalidId = std::numeric_limits<size_t>::max();
  auto firstReal = kInvalidId;
  auto lastReal = kInvalidId;
  for (size_t i = 0; i < segments.size(); ++i)
  {
    if (!segments[i].GetSegment().IsRealSegment())
      continue;

    if (firstReal == kInvalidId)
      firstReal = i;
    lastReal = i;
  }

  if (firstReal == kInvalidId)
  {
    // All segments are fake.
    subroute->m_headFakeDistance = 0.0;
    subroute->m_tailFakeDistance = 0.0;
  }
  else
  {
    CHECK_NOT_EQUAL(firstReal, kInvalidId, ());
    CHECK_NOT_EQUAL(lastReal, kInvalidId, ());

    auto constexpr kEps = 1e-5;

    // To prevent visual artefacts, in the case when all head segments are real
    // m_headFakeDistance must be less than 0.0.
    auto const headLen = (firstReal > 0) ? segments[firstReal - 1].GetDistFromBeginningMerc() - baseDistance : 0.0;
    if (AlmostEqualAbs(headLen, 0.0, kEps))
      subroute->m_headFakeDistance = -kBias;
    else
      subroute->m_headFakeDistance = headLen;

    // To prevent visual artefacts, in the case when all tail segments are real
    // m_tailFakeDistance must be greater than the length of the subroute.
    auto const subrouteLen = segments.back().GetDistFromBeginningMerc() - baseDistance;
    auto const tailLen = segments[lastReal].GetDistFromBeginningMerc() - baseDistance;
    if (AlmostEqualAbs(tailLen, subrouteLen, kEps))
      subroute->m_tailFakeDistance = subrouteLen + kBias;
    else
      subroute->m_tailFakeDistance = tailLen;
  }

  subroute->m_polyline = m2::PolylineD(std::move(points));
  return subroute;
}
}  // namespace

RoutingManager::RoutingManager(Callbacks && callbacks, Delegate & delegate)
  : m_callbacks(std::move(callbacks))
  , m_delegate(delegate)
  , m_extrapolator([this](location::GpsInfo const & gpsInfo) { this->OnExtrapolatedLocationUpdate(gpsInfo); })
{
  m_routingSession.Init(
#ifdef SHOW_ROUTE_DEBUG_MARKS
      [this](m2::PointD const & pt)
  {
    if (m_bmManager == nullptr)
      return;
    auto editSession = m_bmManager->GetEditSession();
    editSession.SetIsVisible(UserMark::Type::DEBUG_MARK, true);
    editSession.CreateUserMark<DebugMarkPoint>(pt);
  }
#else
      nullptr
#endif
  );

  m_routingSession.SetRoutingCallbacks([this](RoutesResult const & result, RouterResultCode code)
  { OnBuildRouteReady(result, code); }, [this](RoutesResult const & result, RouterResultCode code)
  { OnRebuildRouteReady(result, code); }, [this](uint64_t routeId, storage::CountriesSet const & absentCountries)
  { OnNeedMoreMaps(routeId, absentCountries); }, [this](RouterResultCode code) { OnRemoveRoute(code); });

  m_routingSession.SetChangeSessionStateCallback([this](SessionState, SessionState state)
  {
    if (state == SessionState::RouteBuilding || state == SessionState::RouteRebuilding)
      ClearAlternativeRoutes();
  });

  m_routingSession.SetCheckpointCallback([this](size_t passedCheckpointIdx)
  {
    CHECK_GREATER(passedCheckpointIdx, 0, ());
    size_t const pointsCount = GetRoutePointsCount();
    // Raw point edits leave the followed session active until the caller rebuilds.
    // A removed checkpoint must not mark or save the edited itinerary.
    if (passedCheckpointIdx >= pointsCount)
      return;
    if (passedCheckpointIdx + 1 == pointsCount)
      OnRoutePointPassed(RouteMarkType::Finish, 0);
    else
      OnRoutePointPassed(RouteMarkType::Intermediate, passedCheckpointIdx - 1);
  });

  m_routingSession.SetSpeedCamShowCallback([this](m2::PointD const & point, double cameraSpeedKmPH)
  {
    GetPlatform().RunTask(Platform::Thread::Gui, [this, point, cameraSpeedKmPH]()
    {
      if (m_routeSpeedCamShowCallback)
        m_routeSpeedCamShowCallback(point, cameraSpeedKmPH);

      auto editSession = m_bmManager->GetEditSession();
      auto mark = editSession.CreateUserMark<SpeedCameraMark>(point);

      mark->SetIndex(0);
      if (cameraSpeedKmPH == SpeedCameraOnRoute::kNoSpeedInfo)
        return;

      double speed = cameraSpeedKmPH;
      if (measurement_utils::GetMeasurementUnits() == measurement_utils::Units::Imperial)
        speed = measurement_utils::KmphToMiph(cameraSpeedKmPH);

      mark->SetTitle(strings::to_string(static_cast<int>(speed + 0.5)));
    });
  });

  m_routingSession.SetSpeedCamClearCallback([this]()
  {
    GetPlatform().RunTask(Platform::Thread::Gui, [this]()
    {
      m_bmManager->GetEditSession().ClearGroup(UserMark::Type::SPEED_CAM);
      if (m_routeSpeedCamsClearCallback)
        m_routeSpeedCamsClearCallback();
    });
  });
}

void RoutingManager::SetBookmarkManager(BookmarkManager * bmManager)
{
  m_bmManager = bmManager;
}

void RoutingManager::SetTransitManager(TransitReadManager * transitManager)
{
  m_transitReadManager = transitManager;
}

void RoutingManager::OnBuildRouteReady(RoutesResult const & result, RouterResultCode code)
{
  // @TODO(bykoianko) Remove |code| from callback signature.
  CHECK_EQUAL(code, RouterResultCode::NoError, ());
  HidePreviewSegments();

  auto const hasWarnings = InsertRoute(result);
  m_drapeEngine.SafeCall(&df::DrapeEngine::StopLocationFollow);

  // Validate route (in case of bicycle routing it can be invalid).
  ASSERT(result.IsValid(), ());
  auto const & active = result.GetActive();
  // Do not show the full route if one or more stops were added, for easier multi-stop trip planning.
  if (active.IsValid() && active.GetSubrouteCount() < 2 && m_currentRouterType != routing::RouterType::Ruler)
  {
    m2::RectD routeRect = active.GetLimitRect();
    routeRect.Scale(kRouteScaleMultiplier);
    m_drapeEngine.SafeCall(&df::DrapeEngine::SetModelViewRect, routeRect, true /* applyRotation */, -1 /* zoom */,
                           true /* isAnim */, true /* useVisibleViewport */);
  }

  CallRouteBuilded(hasWarnings ? RouterResultCode::HasWarnings : code, storage::CountriesSet());
}

void RoutingManager::OnRebuildRouteReady(RoutesResult const & result, RouterResultCode code)
{
  HidePreviewSegments();

  if (code != RouterResultCode::NoError)
    return;

  auto const hasWarnings = InsertRoute(result);
  CallRouteBuilded(hasWarnings ? RouterResultCode::HasWarnings : code, storage::CountriesSet());
}

void RoutingManager::OnNeedMoreMaps(uint64_t routeId, storage::CountriesSet const & absentCountries)
{
  // No need to inform user about maps needed for the route if the method is called
  // when RoutingSession contains a new route.
  if (m_routingSession.IsRouteValid() && !m_routingSession.IsRouteId(routeId))
    return;

  HidePreviewSegments();
  CallRouteBuilded(RouterResultCode::NeedMoreMaps, absentCountries);
}

void RoutingManager::OnRemoveRoute(routing::RouterResultCode code)
{
  HidePreviewSegments();
  RemoveRoute(true /* deactivateFollowing */);
  CallRouteBuilded(code, storage::CountriesSet());
}

void RoutingManager::OnRoutePointPassed(RouteMarkType type, size_t intermediateIndex)
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  routePoints.PassRoutePoint(type, intermediateIndex);

  if (type == RouteMarkType::Finish)
    RemoveRoute(false /* deactivateFollowing */);

  SaveRoutePoints();
}

void RoutingManager::OnLocationUpdate(location::GpsInfo const & info)
{
  m_extrapolator.OnLocationUpdate(info);
}

RouterType RoutingManager::GetBestRouter(m2::PointD const & startPoint, m2::PointD const & finalPoint) const
{
  // todo Implement something more sophisticated here (or delete the method).
  return GetLastUsedRouter();
}

RouterType RoutingManager::GetLastUsedRouter() const
{
  std::string routerTypeStr;
  settings::TryGet(kRouterTypeKey, routerTypeStr);
  // Maps&Fly offers no car or public transport routing, so those profiles and the default fall back to walking.
  RouterType const type = FromString(routerTypeStr);
  return type == RouterType::Vehicle || type == RouterType::Transit ? RouterType::Pedestrian : type;
}

void RoutingManager::Init(std::shared_ptr<routing::NumMwmIds> ptr)
{
  m_numMwmIDs = std::move(ptr);
  m_numMwmTree = MakeNumMwmTree(*m_numMwmIDs, m_callbacks.m_countryInfoGetter());

  SetRouterImpl(GetLastUsedRouter());
  m_routePreview = std::make_unique<route_preview::Calculator>([this](VehicleType vehicleType)
  { return MakeIndexRouter(vehicleType, true /* loadAltitudes */); });
}

void RoutingManager::SetRouterImpl(RouterType type)
{
  // Ruler has no road profile and uses Transit session settings.
  VehicleType const vehicleType = RoutingOptions::GetVehicleType(type).value_or(VehicleType::Transit);

  m_loadAltitudes = vehicleType != VehicleType::Car;

  std::unique_ptr<IRouter> router;
  std::unique_ptr<AbsentRegionsFinder> absentFinder;

  if (type == RouterType::Ruler)
    router = std::make_unique<RulerRouter>();
  else
  {
    auto & dataSource = m_callbacks.m_dataSourceGetter();

    auto const countryFileGetter = [this](m2::PointD const & p)
    { return m_callbacks.m_countryInfoGetter().GetRegionCountryId(p); };

    auto const localFileChecker = [this, &dataSource](std::string const & countryFile)
    {
      MwmSet::MwmId mwmId = dataSource.GetMwmIdByCountryFile(platform::CountryFile(countryFile));
      if (!mwmId.IsAlive())
      {
        /// @todo Temporary hack, works only for splitted regions.
        /// Should delegate "old" (outdated) countries check to the Framework.
        platform::CountryFile parent(m_callbacks.m_countryParentNameGetterFn(countryFile));
        if (!parent.IsEmpty())
          mwmId = dataSource.GetMwmIdByCountryFile(parent);
      }

      return mwmId.IsAlive();
    };

    auto const getMwmRectByName = [this](std::string const & countryId)
    { return m_callbacks.m_countryInfoGetter().GetLimitRectForLeaf(countryId); };

    router = std::make_unique<IndexRouter>(vehicleType, m_loadAltitudes, m_callbacks.m_countryParentNameGetterFn,
                                           countryFileGetter, getMwmRectByName, m_numMwmIDs, m_numMwmTree,
                                           m_routingSession, dataSource);
    absentFinder = std::make_unique<AbsentRegionsFinder>(countryFileGetter, localFileChecker, m_numMwmIDs, dataSource);
  }

  m_routingSession.SetRoutingSettings(GetRoutingSettings(vehicleType));
  m_routingSession.SetRouter(std::move(router), std::move(absentFinder));
  m_currentRouterType = type;
}

void RoutingManager::RemoveRoute(bool deactivateFollowing)
{
  GetPlatform().RunTask(Platform::Thread::Gui, [this, deactivateFollowing]()
  {
    {
      auto es = m_bmManager->GetEditSession();
      es.ClearGroup(UserMark::Type::TRANSIT);
      es.ClearGroup(UserMark::Type::SPEED_CAM);
      es.ClearGroup(UserMark::Type::ROAD_WARNING);
      es.ClearGroup(UserMark::Type::ROUTE_ALT);
    }
    if (deactivateFollowing)
      SetPointsFollowingMode(false /* enabled */);
  });

  if (deactivateFollowing)
  {
    m_transitReadManager->BlockTransitSchemeMode(false /* isBlocked */);
    // Remove all subroutes.
    m_drapeEngine.SafeCall(&df::DrapeEngine::RemoveSubroute, dp::DrapeID(), true /* deactivateFollowing */);
  }
  else
  {
    df::DrapeEngineLockGuard lock(m_drapeEngine);
    if (lock)
    {
      std::lock_guard<std::mutex> lockSubroutes(m_drapeSubroutesMutex);
      for (auto const & subrouteId : m_drapeSubroutes)
        lock.Get()->RemoveSubroute(subrouteId, false /* deactivateFollowing */);
    }
  }

  {
    std::lock_guard<std::mutex> lock(m_drapeSubroutesMutex);
    m_drapeSubroutes.clear();
    m_transitRouteInfo = TransitRouteInfo();
  }
}

void RoutingManager::ClearAlternativeRoutes()
{
  // Synchronously clear ETA balloons. RemoveRoute uses RunTask(Gui) which only fires after
  // the current GUI flow returns, leaving stale marks briefly visible; we call the same path
  // directly since RoutingManager is GUI-thread-only.
  m_bmManager->GetEditSession().ClearGroup(UserMark::Type::ROUTE_ALT);

  m_drapeEngine.SafeCall(&df::DrapeEngine::RemoveAlternativeSubroutes);
}

void RoutingManager::CollectRoadWarnings(std::vector<routing::RouteSegment> const & segments,
                                         m2::PointD const & startPt, double baseDistance,
                                         RoadWarningsCollection & roadWarnings)
{
  double currentDistance = baseDistance;
  double startDistance = baseDistance;
  RoadWarningMarkType lastWarn = RoadWarningMarkType::Count;
  for (size_t i = 0; i < segments.size(); ++i)
  {
    auto const currentWarn = ChooseRoadWarning(segments[i].GetRoadTypes(), m_currentRouterType);
    if (currentWarn != lastWarn)
    {
      if (lastWarn != RoadWarningMarkType::Count)
      {
        ASSERT(!roadWarnings[lastWarn].empty(), ());
        roadWarnings[lastWarn].back().m_distance = segments[i].GetDistFromBeginningMeters() - startDistance;
      }

      if (currentWarn != RoadWarningMarkType::Count)
      {
        startDistance = currentDistance;
        auto const featureId =
            FeatureID(GetMwmId(segments[i].GetSegment().GetMwmId()), segments[i].GetSegment().GetFeatureId());
        auto const markPoint = i == 0 ? startPt : segments[i - 1].GetJunction().GetPoint();
        roadWarnings[currentWarn].push_back(RoadInfo(markPoint, featureId));
      }
      lastWarn = currentWarn;
    }
    currentDistance = segments[i].GetDistFromBeginningMeters();
  }
  if (lastWarn != RoadWarningMarkType::Count)
    roadWarnings[lastWarn].back().m_distance = segments.back().GetDistFromBeginningMeters() - startDistance;
}

void RoutingManager::CollectRoadPointWarnings(RouteBase const & route, RoadWarningsCollection & roadWarnings)
{
  // The heavy barrier lookup already ran on the routing thread (IndexRouter::RedressRoute);
  // here we just translate the stored barrier types into UI mark types and filter by router type.
  auto const & checker = BarrierWarningChecker::Instance();
  for (auto const & warning : route.GetWarnings())
  {
    auto const markType = checker.GetWarningType(warning.m_type);
    if (markType == RoadWarningMarkType::Count || !IsWarningShownFor(markType, m_currentRouterType))
      continue;

    // Check for duplicates (from alt routes).
    RoadInfo const toInsert(warning.m_point, warning.m_featureId);
    auto & resVec = roadWarnings[markType];
    if (!base::IsExistIf(resVec, [&toInsert](RoadInfo const & ri)
    {
      return ri.m_featureId == toInsert.m_featureId &&
             ri.m_startPoint.EqualDxDy(toInsert.m_startPoint, kMwmPointAccuracy);
    }))
      resVec.push_back(toInsert);
  }
}

void RoutingManager::CreateRoadWarningMarks(RoadWarningsCollection && roadWarnings)
{
  if (roadWarnings.empty())
    return;

  GetPlatform().RunTask(Platform::Thread::Gui, [this, roadWarnings = std::move(roadWarnings)]()
  {
    auto es = m_bmManager->GetEditSession();
    for (auto const & typeInfo : roadWarnings)
    {
      auto const type = typeInfo.first;
      for (size_t i = 0; i < typeInfo.second.size(); ++i)
      {
        auto const & routeInfo = typeInfo.second[i];
        auto mark = es.CreateUserMark<RoadWarningMark>(routeInfo.m_startPoint);
        mark->SetIndex(static_cast<uint32_t>(i));
        mark->SetRoadWarningType(type);
        mark->SetFeatureId(routeInfo.m_featureId);
        // Point warnings (gate/lift_gate) sit on a single vertex and carry no span length.
        if (routeInfo.m_distance > 0.0)
          mark->SetDistance(platform::Distance::CreateFormatted(routeInfo.m_distance).ToString());
      }
    }
  });
}

namespace
{
// Multiplier applied to the alpha channel of subroute colors for alternative (non-active) routes.
float constexpr kAlternativeRouteAlphaMul = 0.5f;

}  // namespace

void RoutingManager::CreateRouteAltMarks(routing::RoutesResult const & result)
{
  if (result.m_routes.empty())
    return;

  // Snapshot the data we need so the Gui-thread task doesn't depend on |result|'s lifetime.
  struct AltMarkInfo
  {
    m2::PointD m_pt;
    std::string m_eta;
    size_t m_idx;
    bool m_isActive;
  };
  std::vector<AltMarkInfo> infos;
  infos.reserve(result.m_routes.size());

  for (size_t i = 0; i < result.m_routes.size(); ++i)
  {
    auto const & r = result.m_routes[i];
    if (!r.IsValid())
      continue;

    // Alts carry a divergence midpoint (set by IndexRouter::CalculateRoute) so the balloon lands
    // where the alt actually differs from the active route. Active route has no diff — fall back
    // to the geometric midpoint of the whole route.
    auto const & diffMid = r.GetDiffMidpoint();
    m2::PointD const pivot = diffMid ? *diffMid : r.GetMidpoint();
    infos.push_back({pivot, platform::Duration(r.GetTotalTimeSec()).GetRoutingString(), i, i == result.m_activeIdx});
  }

  GetPlatform().RunTask(Platform::Thread::Gui, [this, routesId = result.m_routesId, infos = std::move(infos)]()
  {
    // A rebuild or navigation can hide choices before this queued task runs.
    bool hasAlternatives = false;
    m_routingSession.RouteCall([&](RoutesResult const & current)
    { hasAlternatives = current.m_routesId == routesId && current.m_routes.size() > 1; });
    if (!hasAlternatives || m_routingSession.IsFollowing())
      return;

    // Place each balloon up or down based on the midpoint's latitude relative to the others:
    // the northern midpoint (larger mercator y) gets the up balloon, the southern one goes down.
    // +y in drape vertex-normal space is downward, so (0, -N) lifts the body above the pivot.
    float constexpr kAltMarkOffsetPx = 50.0f;
    double avgY = 0.0;
    for (auto const & info : infos)
      avgY += info.m_pt.y;
    avgY /= static_cast<double>(infos.size());

    auto es = m_bmManager->GetEditSession();
    for (auto const & info : infos)
    {
      auto mark = es.CreateUserMark<RouteAltMark>(info.m_pt);
      mark->SetEta(info.m_eta);
      mark->SetRouteIdx(info.m_idx);
      mark->SetIsActive(info.m_isActive);
      float const sign = (info.m_pt.y >= avgY) ? -1.0f : 1.0f;
      mark->SetPixelOffset({0.0f, sign * kAltMarkOffsetPx});
    }
  });
}

MwmSet::MwmId RoutingManager::GetMwmId(routing::NumMwmId numMwmId) const
{
  return m_callbacks.m_dataSourceGetter().GetMwmIdByCountryFile(m_numMwmIDs->GetFile(numMwmId));
}

bool RoutingManager::InsertRoute(RoutesResult const & result)
{
  if (!m_drapeEngine || result.m_routes.empty())
    return false;

  // TODO: Now we always update whole route, so we need to remove previous one.
  RemoveRoute(false /* deactivateFollowing */);

  RoadWarningsCollection roadWarnings;

  bool const isTransitRoute = (m_currentRouterType == RouterType::Transit);
  auto const makeTransitRouteDisplay = [this]()
  {
    // clang-format off
    return std::make_shared<TransitRouteDisplay>(*m_transitReadManager,
          [this](routing::NumMwmId numMwmId) { return GetMwmId(numMwmId); },
          m_callbacks.m_stringsBundleGetter, m_bmManager, m_transitSymbolSizes);
    // clang-format on
  };

  std::shared_ptr<TransitRouteDisplay> transitRouteDisplay;
  if (isTransitRoute)
    transitRouteDisplay = makeTransitRouteDisplay();

  // In follow (navigation) mode only the active route is drawn — alternatives and ETA balloons
  // would clutter the moving map and the ETA is shown in the navigation UI instead.
  bool const isFollowing = m_routingSession.IsFollowing();
  if (!isFollowing)
  {
    for (size_t i = 0; i < result.m_routes.size(); ++i)
    {
      if (i == result.m_activeIdx)
        continue;
      // A TransitRouteDisplay accumulates steps/distance across all subroutes fed to it, so an
      // alternative route must use its own throwaway display: it draws just its (muted) polyline,
      // without corrupting the active route's distance/steps or duplicating its stop marks (the
      // alt's display is never asked for route info or marks).
      auto const altDisplay = isTransitRoute ? makeTransitRouteDisplay() : transitRouteDisplay;
      InsertSingleRoute(result.m_routes[i], false /* isActive */, 0.0 /* depthOffset */, altDisplay, roadWarnings);
    }
  }
  // Lift the active route by 10 so it stays above alternative subroutes even when polylines overlap.
  // The offset must exceed the per-route subroute count (count is typically 1, so 10 is plenty).
  InsertSingleRoute(result.GetActive(), true /* isActive */, 10.0 /* depthOffset */, transitRouteDisplay, roadWarnings);

  if (!isFollowing && m_currentRouterType != RouterType::Ruler && result.m_routes.size() >= 2)
    CreateRouteAltMarks(result);

  {
    std::lock_guard<std::mutex> lock(m_drapeSubroutesMutex);
    m_transitRouteInfo = isTransitRoute ? transitRouteDisplay->GetRouteInfo() : TransitRouteInfo();
  }

  if (isTransitRoute)
  {
    GetPlatform().RunTask(Platform::Thread::Gui, [transitRouteDisplay = std::move(transitRouteDisplay)]()
    { transitRouteDisplay->CreateTransitMarks(); });
  }

  // HasWarnings signals avoidable car-road warnings. Walking and cycling still render their warning
  // marks and expose ferry/steps avoidance directly.
  // Gate/lift_gate have no avoidance control.
  bool const hasDrivingOptionsWarning =
      m_currentRouterType == RouterType::Vehicle &&
      base::AnyOf(roadWarnings, [](auto const & w) { return IsAvoidableRoadWarning(w.first); });

  if (!roadWarnings.empty())
    CreateRoadWarningMarks(std::move(roadWarnings));

  return hasDrivingOptionsWarning;
}

void RoutingManager::InsertSingleRoute(RouteBase const & route, bool isActive, double depthOffset,
                                       std::shared_ptr<TransitRouteDisplay> const & transitRouteDisplay,
                                       RoadWarningsCollection & roadWarnings)
{
  if (!route.IsValid())
    return;

  float const alphaMul = isActive ? 1.0f : kAlternativeRouteAlphaMul;

  std::vector<RouteSegment> segments;
  double distance = 0.0;
  auto const subroutesCount = route.GetSubrouteCount();
  for (size_t subrouteIndex = route.GetCurrentSubrouteIdx(); subrouteIndex < subroutesCount; ++subrouteIndex)
  {
    route.GetSubrouteInfo(subrouteIndex, segments);

    auto const startPt = route.GetSubrouteAttrs(subrouteIndex).GetStart().GetPoint();
    auto subroute =
        CreateDrapeSubroute(segments, startPt, distance,
                            static_cast<double>(subroutesCount - subrouteIndex - 1) + depthOffset, m_currentRouterType);
    if (!subroute)
      continue;
    subroute->m_alphaMul = alphaMul;
    distance = segments.back().GetDistFromBeginningMerc();
    switch (m_currentRouterType)
    {
    case RouterType::Vehicle:
    {
      subroute->m_routeType = df::RouteType::Car;
      subroute->AddStyle(df::SubrouteStyle(df::kRouteColor, df::kRouteOutlineColor));
      // Skip traffic colors on alternatives — keep them visually muted and easy to distinguish.
      if (isActive)
      {
        FillTrafficForRendering(segments, subroute->m_traffic);
        FillTurnsDistancesForRendering(segments, subroute->m_baseDistance, subroute->m_turns);
      }
      break;
    }
    case RouterType::Transit:
    {
      subroute->m_routeType = df::RouteType::Transit;
      if (!transitRouteDisplay->ProcessSubroute(segments, *subroute.get()))
        continue;
      break;
    }
    case RouterType::Pedestrian:
    {
      subroute->m_routeType = df::RouteType::Pedestrian;
      subroute->AddStyle(df::SubrouteStyle(df::kRoutePedestrian, df::RoutePattern(4.0, 2.0)));
      break;
    }
    case RouterType::Bicycle:
    {
      subroute->m_routeType = df::RouteType::Bicycle;
      subroute->AddStyle(df::SubrouteStyle(df::kRouteBicycle, df::RoutePattern(8.0, 2.0)));
      if (isActive)
        FillTurnsDistancesForRendering(segments, subroute->m_baseDistance, subroute->m_turns);
      break;
    }
    case RouterType::Ruler:
    {
      subroute->m_routeType = df::RouteType::Ruler;
      subroute->AddStyle(df::SubrouteStyle(df::kRouteRuler, df::RoutePattern(16.0, 2.0)));
      break;
    }
    default: CHECK(false, ("Unknown router type"));
    }

    CollectRoadWarnings(segments, startPt, subroute->m_baseDistance, roadWarnings);

    auto const subrouteId =
        m_drapeEngine.SafeCallWithResult(&df::DrapeEngine::AddSubroute, df::SubrouteConstPtr(subroute.release()));

    std::lock_guard<std::mutex> lock(m_drapeSubroutesMutex);
    m_drapeSubroutes.push_back(subrouteId);
  }

  // Point warnings (barrier nodes) are precomputed on the routing thread (IndexRouter::RedressRoute)
  // and stored in the route; read them once (route-global, not per-subroute).
  CollectRoadPointWarnings(route, roadWarnings);
}

void RoutingManager::FollowRoute()
{
  if (!m_routingSession.EnableFollowMode())
    return;

  m_transitReadManager->BlockTransitSchemeMode(true /* isBlocked */);

  // Switching on the extrapolator only for following mode in car and bicycle navigation.
  m_extrapolator.Enable(m_currentRouterType == RouterType::Vehicle || m_currentRouterType == RouterType::Bicycle);
  m_delegate.OnRouteFollow(m_currentRouterType);

  m_bmManager->GetEditSession().ClearGroup(UserMark::Type::ROAD_WARNING);
  HideRoutePoint(RouteMarkType::Start);
  SetPointsFollowingMode(true /* enabled */);

  ClearAlternativeRoutes();

  CancelRecommendation(Recommendation::RebuildAfterPointsLoading);
}

bool RoutingManager::SwapActiveAlternative(size_t idx)
{
  if (!m_routingSession.SwapActiveAlternative(idx))
    return false;

  // Re-render drape with the new active variant and notify platform UI (elevation profile,
  // route info, etc.) via the same RouteBuilded callback path used by the initial build, so
  // any cached route data on the Android/iOS side is refreshed for the new active route.
  bool hasWarnings = false;
  m_routingSession.RouteCall([this, &hasWarnings](routing::RoutesResult const & result)
  { hasWarnings = InsertRoute(result); });
  CallRouteBuilded(hasWarnings ? RouterResultCode::HasWarnings : RouterResultCode::NoError, storage::CountriesSet());
  return true;
}

bool RoutingManager::TryTapOnAlternativeRoute(m2::PointD const & mercator, double mercatorPerPixel)
{
  // Alts aren't drawn during navigation and the active route shouldn't be tap-swappable.
  if (!IsRoutingActive() || m_routingSession.IsFollowing() || !m_routingSession.IsRouteValid())
    return false;

  // Pixel-radius for the tap area. Matches the visual half-width the routes are drawn with;
  // closer than this and we treat the tap as hitting that polyline.
  double constexpr kTapPixels = 16.0;
  double const tapMerc = kTapPixels * df::VisualParams::Instance().GetVisualScale() * mercatorPerPixel;
  double const tapMercSq = tapMerc * tapMerc;
  m2::RectD const tapRect(mercator, tapMerc, tapMerc);

  int targetIdx = -1;
  double bestSq = tapMercSq;
  m_routingSession.RouteCall([&](routing::RoutesResult const & result)
  {
    for (size_t i = 0; i < result.m_routes.size(); ++i)
    {
      if (i == result.m_activeIdx)
        continue;

      std::optional<m2::PointD> prev;
      result.m_routes[i].ForEachPoint([&](geometry::PointWithAltitude const & p)
      {
        if (prev)
        {
          auto const & p2 = p.GetPoint();
          if (m2::RectD(*prev, p2).IsIntersect(tapRect))
          {
            m2::ParametrizedSegment<m2::PointD> seg(*prev, p2);
            double const distSq = seg.SquaredDistanceToPoint(mercator);
            if (distSq < bestSq)
            {
              bestSq = distSq;
              targetIdx = i;
            }
          }
        }
        prev = p.GetPoint();
      });
    }
  });

  return targetIdx >= 0 ? SwapActiveAlternative(targetIdx) : false;
}

void RoutingManager::CloseRouting(bool removeRoutePoints)
{
  m_extrapolator.Enable(false);
  // Hide preview.
  HidePreviewSegments();

  if (m_routingSession.IsBuilt())
    m_routingSession.EmitCloseRoutingEvent();
  m_routingSession.Reset();
  RemoveRoute(true /* deactivateFollowing */);

  if (removeRoutePoints)
    RemoveRoutePoints();
}

void RoutingManager::SetLastUsedRouter(RouterType type)
{
  settings::Set(kRouterTypeKey, ToString(type));
}

void RoutingManager::HideRoutePoint(RouteMarkType type, size_t intermediateIndex)
{
  RoutePointsLayout routePoints(*m_bmManager);
  RouteMarkPoint * mark = routePoints.GetRoutePointForEdit(type, intermediateIndex);
  if (mark != nullptr)
    mark->SetIsVisible(false);
}

bool RoutingManager::IsMyPosition(RouteMarkType type, size_t intermediateIndex)
{
  RoutePointsLayout routePoints(*m_bmManager);
  RouteMarkPoint const * mark = routePoints.GetRoutePoint(type, intermediateIndex);
  return mark != nullptr && mark->IsMyPosition();
}

std::vector<RouteMarkData> RoutingManager::GetRoutePoints() const
{
  std::vector<RouteMarkData> result;
  RoutePointsLayout routePoints(*m_bmManager);
  for (auto const & p : routePoints.GetRoutePoints())
    result.push_back(p->GetMarkData());
  return result;
}

size_t RoutingManager::GetRoutePointsCount() const
{
  RoutePointsLayout routePoints(*m_bmManager);
  return routePoints.GetRoutePointsCount();
}

bool RoutingManager::CouldAddIntermediatePoint() const
{
  if (!IsRoutingActive())
    return false;

  return m_bmManager->GetUserMarkIds(UserMark::Type::ROUTING).size() < RoutePointsLayout::kMaxRoutePointsCount;
}

bool RoutingManager::AddRoutePoint(RouteMarkData && markData, bool optimize)
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  bool const isIntermediate = markData.m_pointType == RouteMarkType::Intermediate;

  if (!isIntermediate)
    routePoints.RemoveRoutePoint(markData.m_pointType);

  if (markData.m_isMyPosition)
  {
    if (auto const * staleMyPosition = routePoints.GetMyPositionPoint())
      routePoints.RemoveRoutePoint(staleMyPosition->GetRoutePointType(), staleMyPosition->GetIntermediateIndex());
  }

  bool const hasStart = routePoints.GetRoutePoint(RouteMarkType::Start) != nullptr;
  bool const hasFinish = routePoints.GetRoutePoint(RouteMarkType::Finish) != nullptr;
  if (isIntermediate)
    markData.m_intermediateIndex = routePoints.GetRoutePointsCount() - hasStart - hasFinish;

  markData.m_isVisible = !markData.m_isMyPosition;
  auto const * addedPoint = routePoints.AddRoutePoint(std::move(markData));
  if (addedPoint == nullptr)
    return false;

  // Ruler points describe the user's measurement, not a route to optimize.
  if (isIntermediate && optimize && hasStart && hasFinish && m_currentRouterType != RouterType::Ruler)
    ReorderIntermediatePoints(routePoints, addedPoint);
  return true;
}

void RoutingManager::ReplaceRoutePoint(RouteMarkType type, size_t intermediateIndex, RouteMarkData && markData)
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  auto * target = routePoints.GetRoutePointForEdit(type, intermediateIndex);
  if (target == nullptr)
  {
    markData.m_pointType = type;
    AddRoutePoint(std::move(markData), false /* optimize */);
    return;
  }

  if (markData.m_isMyPosition)
  {
    if (auto const * staleMyPosition = routePoints.GetMyPositionPoint();
        staleMyPosition != nullptr && staleMyPosition != target)
    {
      routePoints.RemoveRoutePoint(staleMyPosition->GetRoutePointType(), staleMyPosition->GetIntermediateIndex());
    }
  }

  // Removing another My Position mark may have changed the target's type or index.
  markData.m_pointType = target->GetRoutePointType();
  markData.m_intermediateIndex = target->GetIntermediateIndex();
  markData.m_isVisible = !markData.m_isMyPosition;
  target->SetMarkData(std::move(markData));
}

bool RoutingManager::OptimizeRoutePoints()
{
  ASSERT(m_bmManager != nullptr, ());
  if (IsRoutingFollowing() || m_currentRouterType == RouterType::Ruler ||
      m_bmManager->GetUserMarkIds(UserMark::Type::ROUTING).size() < 3)
    return false;
  RoutePointsLayout layout(*m_bmManager);
  if (layout.GetRoutePoint(RouteMarkType::Start) == nullptr || layout.GetRoutePoint(RouteMarkType::Finish) == nullptr)
    return false;
  return ReorderIntermediatePoints(layout, nullptr /* added */);
}

bool RoutingManager::ContinueRouteToPoint(RouteMarkData && markData)
{
  ASSERT(m_bmManager != nullptr, ());
  ASSERT(markData.m_pointType == RouteMarkType::Finish, ("New route point should have type RouteMarkType::Finish"));
  RoutePointsLayout routePoints(*m_bmManager);

  if (routePoints.GetRoutePointsCount() >= RoutePointsLayout::kMaxRoutePointsCount)
  {
    LOG(LWARNING, ("Cannot continue route: route points limit reached."));
    return false;
  }

  // Finish point is now Intermediate point
  RouteMarkPoint * finishMarkData = routePoints.GetRoutePointForEdit(RouteMarkType::Finish);
  if (finishMarkData == nullptr)
  {
    LOG(LWARNING, ("Cannot continue route: finish point is missing."));
    return false;
  }
  finishMarkData->SetRoutePointType(RouteMarkType::Intermediate);
  finishMarkData->SetIntermediateIndex(routePoints.GetRoutePointsCount() - 2);

  if (markData.m_isMyPosition)
  {
    RouteMarkPoint const * mark = routePoints.GetMyPositionPoint();
    if (mark)
      routePoints.RemoveRoutePoint(mark->GetRoutePointType(), mark->GetIntermediateIndex());
  }

  markData.m_intermediateIndex = routePoints.GetRoutePointsCount() - 1;
  markData.m_isVisible = !markData.m_isMyPosition;
  routePoints.AddRoutePoint(std::move(markData));
  return true;
}

void RoutingManager::RemoveRoutePoint(RouteMarkType type, size_t intermediateIndex)
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  routePoints.RemoveRoutePoint(type, intermediateIndex);
}

void RoutingManager::RemoveRoutePoints()
{
  CancelRecommendation(Recommendation::RebuildAfterPointsLoading);
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  routePoints.RemoveRoutePoints();
}

void RoutingManager::RemoveIntermediateRoutePoints()
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  routePoints.RemoveIntermediateRoutePoints();
}

void RoutingManager::RemovePassedRoutePoints()
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  if (!routePoints.RemovePassedRoutePoints())
    return;

  // If the passed Start was removed, add a new one at the current position.
  if (routePoints.GetRoutePoint(RouteMarkType::Start) == nullptr)
  {
    RouteMarkData startPt;
    startPt.m_pointType = RouteMarkType::Start;
    startPt.m_isMyPosition = true;
    startPt.m_isVisible = false;
    startPt.m_position = m_bmManager->MyPositionMark().GetPivot();
    routePoints.AddRoutePoint(std::move(startPt));
  }
}

void RoutingManager::MoveRoutePoint(RouteMarkType currentType, size_t currentIntermediateIndex,
                                    RouteMarkType targetType, size_t targetIntermediateIndex)
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  routePoints.MoveRoutePoint(currentType, currentIntermediateIndex, targetType, targetIntermediateIndex);
}

void RoutingManager::MoveRoutePoint(size_t currentIndex, size_t targetIndex)
{
  ASSERT(m_bmManager != nullptr, ());

  RoutePointsLayout routePoints(*m_bmManager);
  size_t const sz = routePoints.GetRoutePointsCount();
  if (sz == 1)
  {
    auto * point = routePoints.GetRoutePoints().front();
    auto const currentType = point->GetRoutePointType();
    ASSERT(currentType != RouteMarkType::Intermediate, ());
    ASSERT_LESS(currentIndex, 2, ());
    ASSERT_LESS(targetIndex, 2, ());
    ASSERT_NOT_EQUAL(currentIndex, targetIndex, ());
    // Either endpoint slot may be dragged, so flip the real point regardless of drag direction.
    point->SetRoutePointFullType(currentType == RouteMarkType::Start ? RouteMarkType::Finish : RouteMarkType::Start, 0);
    return;
  }
  auto const convertIndex = [sz](RouteMarkType & type, size_t & index)
  {
    if (index == 0)
    {
      type = RouteMarkType::Start;
      index = 0;
    }
    else if (index + 1 == sz)
    {
      type = RouteMarkType::Finish;
      index = 0;
    }
    else
    {
      type = RouteMarkType::Intermediate;
      --index;
    }
  };
  RouteMarkType currentType;
  RouteMarkType targetType;

  convertIndex(currentType, currentIndex);
  convertIndex(targetType, targetIndex);

  routePoints.MoveRoutePoint(currentType, currentIndex, targetType, targetIndex);
}

bool RoutingManager::ReverseRoutePoints()
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  auto const points = routePoints.GetRoutePoints();
  if (points.size() < 2 || points.front()->GetRoutePointType() != RouteMarkType::Start ||
      points.back()->GetRoutePointType() != RouteMarkType::Finish)
    return false;

  for (size_t i = 0; i < points.size(); ++i)
  {
    size_t const reversedIndex = points.size() - 1 - i;
    auto * point = points[i];
    if (reversedIndex == 0)
      point->SetRoutePointFullType(RouteMarkType::Start, 0);
    else if (reversedIndex + 1 == points.size())
      point->SetRoutePointFullType(RouteMarkType::Finish, 0);
    else
      point->SetRoutePointFullType(RouteMarkType::Intermediate, reversedIndex - 1);

    point->SetPassed(false);
    point->SetIsVisible(!point->IsMyPosition());
  }
  return true;
}

void RoutingManager::SetPointsFollowingMode(bool enabled)
{
  ASSERT(m_bmManager != nullptr, ());
  RoutePointsLayout routePoints(*m_bmManager);
  routePoints.SetFollowingMode(enabled);
}

bool RoutingManager::ReorderIntermediatePoints(RoutePointsLayout & layout, RouteMarkPoint const * added)
{
  auto const points = layout.GetRoutePoints();
  ASSERT_GREATER_OR_EQUAL(points.size(), 3, ());
  ASSERT(points.front()->GetRoutePointType() == RouteMarkType::Start, ());
  ASSERT(points.back()->GetRoutePointType() == RouteMarkType::Finish, ());

  // Passed stops keep their slots; the rest of the route starts from the last of them.
  size_t lastPassedIndex = 0;
  for (size_t i = 1; i + 1 < points.size(); ++i)
    if (points[i]->IsPassed())
      lastPassedIndex = i;

  std::vector<RouteMarkPoint *> ordered;
  std::vector<RouteMarkPoint *> toPlace;
  for (size_t i = lastPassedIndex + 1; i + 1 < points.size(); ++i)
    if (added == nullptr || points[i] == added)
      toPlace.push_back(points[i]);
    else
      ordered.push_back(points[i]);

  std::vector<m2::PointD> positions;
  for (auto const * point : ordered)
    positions.push_back(point->GetPivot());

  CheckpointPredictor const predictor(points[lastPassedIndex]->GetPivot(), points.back()->GetPivot());
  for (auto * point : toPlace)
  {
    size_t const index = predictor.PredictPosition(positions, point->GetPivot());
    positions.insert(positions.begin() + index, point->GetPivot());
    ordered.insert(ordered.begin() + index, point);
  }

  // Greedy re-insertion depends on the current order and may produce a longer route. Commit only a strictly shorter
  // one to prevent repeated optimization from flipping between orders.
  if (added == nullptr)
  {
    double currentLength = 0.0;
    for (size_t i = lastPassedIndex; i + 1 < points.size(); ++i)
      currentLength += mercator::DistanceOnEarth(points[i]->GetPivot(), points[i + 1]->GetPivot());

    double optimizedLength = 0.0;
    m2::PointD const * previous = &points[lastPassedIndex]->GetPivot();
    for (auto const & position : positions)
    {
      optimizedLength += mercator::DistanceOnEarth(*previous, position);
      previous = &position;
    }
    optimizedLength += mercator::DistanceOnEarth(*previous, points.back()->GetPivot());
    if (optimizedLength >= currentLength)
      return false;
  }

  bool changed = false;
  for (size_t i = 0; i < ordered.size(); ++i)
  {
    size_t const index = lastPassedIndex + i;
    if (ordered[i]->GetIntermediateIndex() != index)
    {
      ordered[i]->SetIntermediateIndex(index);
      changed = true;
    }
  }
  return changed;
}

void RoutingManager::GenerateNotifications(std::vector<std::string> & turnNotifications, bool announceStreets)
{
  m_routingSession.GenerateNotifications(turnNotifications, announceStreets);
}

namespace
{
RoutingOptions LoadRoutePlanningOptions(RouterType router)
{
  auto const vehicle = RoutingOptions::GetVehicleType(router);
  return vehicle ? RoutingOptions::LoadFromSettings(*vehicle) : RoutingOptions{};
}
}  // namespace

bool RoutingManager::HaveRoutePlanningOptionsChanged() const
{
  auto const options = m_routingSession.GetRoutePlanningOptions();
  return options && options->GetOptions() != LoadRoutePlanningOptions(m_currentRouterType).GetOptions();
}

void RoutingManager::BuildRoute(uint32_t timeoutSec)
{
  CHECK_THREAD_CHECKER(m_threadChecker, ("BuildRoute"));
  CHECK(m_numMwmIDs, ("Initialize routing before building a route"));

  // Retire the previous build before notifying observers, including on validation failures.
  CloseRouting(false /* remove route points */);
  m_routingSession.SetRoutePlanningOptions(LoadRoutePlanningOptions(m_currentRouterType));
  m_bmManager->GetEditSession().ClearGroup(UserMark::Type::TRANSIT);

  // Remove already passed intermediate points so they don't affect the new route.
  // https://github.com/organicmaps/organicmaps/issues/7939
  // https://github.com/organicmaps/organicmaps/issues/9592
  // https://github.com/organicmaps/organicmaps/issues/11256
  RemovePassedRoutePoints();

  auto routePoints = GetRoutePoints();
  if (routePoints.size() < 2)
  {
    CallRouteBuilded(RouterResultCode::Cancelled, storage::CountriesSet());
    return;
  }

  // Update my position.
  for (auto & p : routePoints)
  {
    if (!p.m_isMyPosition)
      continue;

    auto const & myPosition = m_bmManager->MyPositionMark();
    if (!myPosition.HasPosition())
    {
      CallRouteBuilded(RouterResultCode::NoCurrentPosition, storage::CountriesSet());
      return;
    }
    p.m_position = myPosition.GetPivot();
    // Saved stops must use the fix that produced this route, even if GPS moves before Save.
    RoutePointsLayout layout(*m_bmManager);
    auto * mark = layout.GetRoutePointForEdit(p.m_pointType, p.m_intermediateIndex);
    CHECK(mark != nullptr, (static_cast<int>(p.m_pointType), p.m_intermediateIndex));
    mark->SetPosition(p.m_position);
  }

  // Check for equal points.
  for (size_t i = 0; i < routePoints.size(); i++)
  {
    for (size_t j = i + 1; j < routePoints.size(); j++)
    {
      if (routePoints[i].m_position.EqualDxDy(routePoints[j].m_position, mercator::kPointEqualityEps))
      {
        CallRouteBuilded(RouterResultCode::Cancelled, storage::CountriesSet());
        return;
      }
    }
  }

  ShowPreviewSegments(routePoints);

  // Route points preview.
  // Disabled preview zoom to fix https://github.com/organicmaps/organicmaps/issues/5409.
  // Uncomment next lines to enable back zoom on route point add/remove.

  // m2::RectD rect = ShowPreviewSegments(routePoints);
  // rect.Scale(kRouteScaleMultiplier);
  // m_drapeEngine.SafeCall(&df::DrapeEngine::SetModelViewRect, rect, true /* applyRotation */,
  //                        -1 /* zoom */, true /* isAnim */, true /* useVisibleViewport */);

  m_routingSession.ClearPositionAccumulator();
  m_routingSession.SetUserCurrentPosition(routePoints.front().m_position);

  std::vector<m2::PointD> points;
  points.reserve(routePoints.size());
  for (auto const & point : routePoints)
    points.push_back(point.m_position);

  m_routingSession.BuildRoute(Checkpoints(std::move(points)), timeoutSec);
}

void RoutingManager::SetUserCurrentPosition(m2::PointD const & position)
{
  m_routingSession.PushPositionAccumulator(position);

  if (IsRoutingActive())
    m_routingSession.SetUserCurrentPosition(position);

  if (m_routeRecommendCallback != nullptr)
  {
    // Check if we've found my position almost immediately after route points loading.
    auto constexpr kFoundLocationInterval = 2.0;
    auto const elapsed = std::chrono::steady_clock::now() - m_loadRoutePointsTimestamp;
    auto const sec = std::chrono::duration_cast<std::chrono::duration<double>>(elapsed).count();
    if (sec <= kFoundLocationInterval)
    {
      m_routeRecommendCallback(Recommendation::RebuildAfterPointsLoading);
      CancelRecommendation(Recommendation::RebuildAfterPointsLoading);
    }
  }
}

static std::string GetNameFromPoint(RouteMarkData const & rmd)
{
  if (rmd.m_subTitle.empty())
    return "";
  return rmd.m_title;
}

kml::TrackId RoutingManager::SaveRoute()
{
  auto const routePoints = GetRoutePoints();
  // RemoveRoutePoints() leaves the built route valid after clearing its editable marks.
  if (routePoints.size() < 2)
    return kml::kInvalidTrackId;

  RouteJunctions junctions;
  if (!RoutingSession().GetRouteJunctionPoints(junctions))
    return kml::kInvalidTrackId;

  kml::MultiGeometry::TimeT times;
  kml::MultiGeometry::RemoveDuplicatePoints(junctions, times);
  if (junctions.size() < 2)
    return kml::kInvalidTrackId;

  std::string const from = GetNameFromPoint(routePoints.front());
  std::string const to = GetNameFromPoint(routePoints.back());

  // GetRoutePoints() preserves stop order. A stop title needs no subtitle: imported stops have
  // plain names, whereas from/to use the subtitle guard to filter unnamed-pin placeholders.
  std::vector<kml::TrackStopData> stops;
  for (auto const & point : routePoints)
    if (point.m_pointType == RouteMarkType::Intermediate)
      stops.push_back({point.m_position, point.m_title});

  return m_bmManager->SaveRoute(std::move(junctions), from, to, std::move(stops), ToString(m_currentRouterType));
}

bool RoutingManager::DisableFollowMode()
{
  bool const disabled = m_routingSession.DisableFollowMode();
  if (disabled)
  {
    m_transitReadManager->BlockTransitSchemeMode(false /* isBlocked */);
    m_drapeEngine.SafeCall(&df::DrapeEngine::DeactivateRouteFollowing);
  }
  return disabled;
}

void RoutingManager::CheckLocationForRouting(location::GpsInfo const & info)
{
  if (!IsRoutingActive())
    return;

  SessionState const state = m_routingSession.OnLocationPositionChanged(info);
  if (state == SessionState::RouteNeedRebuild)
  {
    m_routingSession.RebuildRoute(mercator::FromLatLon(info.m_latitude, info.m_longitude),
                                  [this](RoutesResult const & result, RouterResultCode code)
    { OnRebuildRouteReady(result, code); }, nullptr /* needMoreMapsCallback */, nullptr /* removeRouteCallback */,
                                  RouterDelegate::kNoTimeout, SessionState::RouteRebuilding,
                                  true /* adjustToPrevRoute */);
  }
}

void RoutingManager::CallRouteBuilded(RouterResultCode code, storage::CountriesSet const & absentCountries)
{
  CHECK(m_routingBuildingCallback, ());
  m_routingBuildingCallback(code, absentCountries);
}

void RoutingManager::MatchLocationToRoute(location::GpsInfo & location, location::RouteMatchingInfo & routeMatchingInfo)
{
  if (!IsRoutingActive())
    return;

  bool const matchedToRoute = m_routingSession.MatchLocationToRoute(location, routeMatchingInfo);

  if (!matchedToRoute && m_currentRouterType == RouterType::Vehicle)
    m_routingSession.MatchLocationToRoadGraph(location);
}

location::RouteMatchingInfo RoutingManager::GetRouteMatchingInfo(location::GpsInfo & info)
{
  CheckLocationForRouting(info);

  location::RouteMatchingInfo routeMatchingInfo;
  MatchLocationToRoute(info, routeMatchingInfo);
  return routeMatchingInfo;
}

void RoutingManager::SetDrapeEngine(ref_ptr<df::DrapeEngine> engine, bool is3dAllowed)
{
  m_drapeEngine.Set(engine);
  if (engine == nullptr)
    return;

  // Apply gps info which was set before drape engine creation.
  if (m_gpsInfoCache != nullptr)
  {
    auto routeMatchingInfo = GetRouteMatchingInfo(*m_gpsInfoCache);
    m_drapeEngine.SafeCall(&df::DrapeEngine::SetGpsInfo, *m_gpsInfoCache, m_routingSession.IsNavigable(),
                           routeMatchingInfo);
    m_gpsInfoCache.reset();
  }

  std::vector<std::string> symbols;
  symbols.reserve(kTransitSymbols.size() * 2);
  for (auto const & typePair : kTransitSymbols)
  {
    symbols.push_back(typePair.second + "-s");
    symbols.push_back(typePair.second + "-m");
  }
  m_drapeEngine.SafeCall(&df::DrapeEngine::RequestSymbolsSize, symbols,
                         [this, is3dAllowed](std::map<std::string, m2::PointF> && sizes)
  {
    GetPlatform().RunTask(Platform::Thread::Gui, [this, is3dAllowed, sizes = std::move(sizes)]() mutable
    {
      m_transitSymbolSizes = std::move(sizes);

      // In case of the engine reinitialization recover route.
      if (IsRoutingActive())
      {
        m_routingSession.RouteCall([this](RoutesResult const & result) { InsertRoute(result); });

        if (is3dAllowed && m_routingSession.IsFollowing())
          m_drapeEngine.SafeCall(&df::DrapeEngine::EnablePerspective);
      }
    });
  });
}

bool RoutingManager::HasRouteAltitude() const
{
  return m_loadAltitudes && m_routingSession.HasRouteAltitude();
}

bool RoutingManager::GetRouteElevationInfo(ElevationInfo & ei) const
{
  auto const * route = m_routingSession.GetRoute();
  if (!route || !route->IsValid() || !route->HaveAltitudes())
    return false;

  geometry::Altitudes altitudes;
  route->GetAltitudes(altitudes);

  ei.Assign(route->GetSegDistanceMeters(), altitudes);
  ei.Simplify();
  return std::ranges::any_of(ei.GetLines().front(),
                             [](auto const & point) { return point.m_altitude != geometry::kDefaultAltitudeMeters; });
}

std::optional<m2::PointD> RoutingManager::GetRoutePointAtDistance(double distanceMeters) const
{
  auto const * route = m_routingSession.GetRoute();
  if (!route || !route->IsValid())
    return std::nullopt;

  auto const & distances = route->GetSegDistanceMeters();
  auto const & points = route->GetPoly().GetPoints();
  return m2::InterpolatePointAtDistance(distances, points, distanceMeters);
}

void RoutingManager::SetRouter(RouterType type)
{
  CHECK_THREAD_CHECKER(m_threadChecker, ("SetRouter"));

  // A profile preference can be saved after SDK setup, before asynchronous routing initialization.
  // Init() installs GetLastUsedRouter().
  if (!m_numMwmIDs)
  {
    SetLastUsedRouter(type);
    return;
  }

  if (m_currentRouterType == type)
    return;

  // Hide preview.
  HidePreviewSegments();

  SetLastUsedRouter(type);
  SetRouterImpl(type);
}

// static
uint32_t RoutingManager::InvalidRoutePointsTransactionId()
{
  return kInvalidTransactionId;
}

uint32_t RoutingManager::GenerateRoutePointsTransactionId() const
{
  static uint32_t id = kInvalidTransactionId + 1;
  return id++;
}

uint32_t RoutingManager::OpenRoutePointsTransaction()
{
  auto const id = GenerateRoutePointsTransactionId();
  m_routePointsTransactions[id].m_routeMarks = GetRoutePoints();
  return id;
}

void RoutingManager::ApplyRoutePointsTransaction(uint32_t transactionId)
{
  if (m_routePointsTransactions.find(transactionId) == m_routePointsTransactions.end())
    return;

  // If we apply a transaction we can remove all earlier transactions.
  // Newer transactions remain available to be applied or cancelled.
  for (auto it = m_routePointsTransactions.begin(); it != m_routePointsTransactions.end();)
    if (it->first <= transactionId)
      it = m_routePointsTransactions.erase(it);
    else
      ++it;
}

void RoutingManager::CancelRoutePointsTransaction(uint32_t transactionId)
{
  auto const it = m_routePointsTransactions.find(transactionId);
  if (it == m_routePointsTransactions.end())
    return;
  auto routeMarks = it->second.m_routeMarks;

  // If we cancel a transaction we must remove all later transactions.
  for (auto it = m_routePointsTransactions.begin(); it != m_routePointsTransactions.end();)
    if (it->first >= transactionId)
      it = m_routePointsTransactions.erase(it);
    else
      ++it;

  // Revert route points.
  ASSERT(m_bmManager != nullptr, ());
  auto editSession = m_bmManager->GetEditSession();
  editSession.ClearGroup(UserMark::Type::ROUTING);
  RoutePointsLayout routePoints(*m_bmManager);
  for (auto & markData : routeMarks)
    routePoints.AddRoutePoint(std::move(markData));
}

bool RoutingManager::HasSavedRoutePoints() const
{
  auto const fileName = GetPlatform().SettingsPathForFile(kRoutePointsFile);
  return GetPlatform().IsFileExistsByFullPath(fileName);
}

std::vector<RouteMarkData> RoutingManager::ReadAndDeleteSavedRoutePoints() const
{
  if (!HasSavedRoutePoints())
    return {};

  auto const fileName = GetPlatform().SettingsPathForFile(kRoutePointsFile);
  // Consume the file before applying points and notifying mark observers.
  SCOPE_GUARD(routePointsFileGuard, [&fileName] { FileWriter::DeleteFileX(fileName); });
  try
  {
    std::string data;
    ReaderPtr<Reader>(GetPlatform().GetReader(fileName)).ReadAsString(data);
    return DeserializeRoutePoints(data);
  }
  catch (RootException const & ex)
  {
    // Recovery is optional; file read errors must not prevent opening the map.
    LOG(LWARNING, ("Loading route points failed:", ex.Msg()));
    return {};
  }
}

bool RoutingManager::LoadRoutePoints()
{
  CHECK_THREAD_CHECKER(m_threadChecker, ());
  ASSERT(m_bmManager != nullptr, ());
  if (IsRoutingActive() || !m_bmManager->GetUserMarkIds(UserMark::Type::ROUTING).empty())
    return false;

  auto points = ReadAndDeleteSavedRoutePoints();
  if (points.empty())
    return false;

  bool routeUsedPosition = false;
  auto const & myPosMark = m_bmManager->MyPositionMark();
  auto editSession = m_bmManager->GetEditSession();
  // Saved points have no intermediate indices; replay their order without optimization.
  for (auto & p : points)
  {
    bool const positionStart = p.m_replaceWithMyPositionAfterRestart && p.m_pointType == RouteMarkType::Start;
    routeUsedPosition |= positionStart;
    if (positionStart && myPosMark.HasPosition())
    {
      RouteMarkData startPt;
      startPt.m_pointType = RouteMarkType::Start;
      startPt.m_isMyPosition = true;
      startPt.m_title = m_callbacks.m_stringsBundleGetter().GetString("core_my_position");
      startPt.m_position = myPosMark.GetPivot();
      AddRoutePoint(std::move(startPt), false /* optimize */);
    }
    else
    {
      AddRoutePoint(std::move(p), false /* optimize */);
    }
  }

  // An imminent first position fix can replace the saved My Position origin.
  if (routeUsedPosition && !myPosMark.HasPosition())
    m_loadRoutePointsTimestamp = std::chrono::steady_clock::now();
  return true;
}

void RoutingManager::SaveRoutePoints()
{
  CHECK_THREAD_CHECKER(m_threadChecker, ());
  auto points = GetRoutePointsToSave();
  if (points.empty())
  {
    DeleteSavedRoutePoints();
    return;
  }

  try
  {
    auto const fileName = GetPlatform().SettingsPathForFile(kRoutePointsFile);
    FileWriter writer(fileName);
    std::string const pointsData = SerializeRoutePoints(points);
    writer.Write(pointsData.c_str(), pointsData.length());
  }
  catch (RootException const & ex)
  {
    LOG(LWARNING, ("Saving route points failed:", ex.Msg()));
  }
}

std::vector<RouteMarkData> RoutingManager::GetRoutePointsToSave() const
{
  auto points = GetRoutePoints();
  if (points.size() < 2 || points.back().m_isPassed)
    return {};

  std::vector<RouteMarkData> result;
  result.reserve(points.size());

  // Resume from the last passed point, or the original start when none was passed.
  result.emplace_back(GetLastPassedPoint(m_bmManager, points));

  for (auto & p : points)
  {
    // Here we skip passed points and the start point.
    if (p.m_isPassed || p.m_pointType == RouteMarkType::Start)
      continue;

    result.push_back(std::move(p));
  }

  if (result.size() < 2)
    return {};

  return result;
}

void RoutingManager::OnExtrapolatedLocationUpdate(location::GpsInfo const & info)
{
  location::GpsInfo gpsInfo(info);
  if (!m_drapeEngine)
    m_gpsInfoCache = std::make_unique<location::GpsInfo>(gpsInfo);

  auto routeMatchingInfo = GetRouteMatchingInfo(gpsInfo);
  m_drapeEngine.SafeCall(&df::DrapeEngine::SetGpsInfo, gpsInfo, m_routingSession.IsNavigable(), routeMatchingInfo);
}

void RoutingManager::DeleteSavedRoutePoints()
{
  CHECK_THREAD_CHECKER(m_threadChecker, ());
  Platform::RemoveFileIfExists(GetPlatform().SettingsPathForFile(kRoutePointsFile));
}

void RoutingManager::UpdatePreviewMode()
{
  SetSubroutesVisibility(false /* visible */);
  HidePreviewSegments();
  ShowPreviewSegments(GetRoutePoints());
}

void RoutingManager::CancelPreviewMode()
{
  SetSubroutesVisibility(true /* visible */);
  HidePreviewSegments();
}

m2::RectD RoutingManager::ShowPreviewSegments(std::vector<RouteMarkData> const & routePoints)
{
  df::DrapeEngineLockGuard lock(m_drapeEngine);
  if (!lock)
    return mercator::Bounds::FullRect();

  m2::RectD rect;
  for (size_t pointIndex = 0; pointIndex + 1 < routePoints.size(); pointIndex++)
  {
    rect.Add(routePoints[pointIndex].m_position);
    rect.Add(routePoints[pointIndex + 1].m_position);
    lock.Get()->AddRoutePreviewSegment(routePoints[pointIndex].m_position, routePoints[pointIndex + 1].m_position);
  }
  return rect;
}

void RoutingManager::HidePreviewSegments()
{
  m_drapeEngine.SafeCall(&df::DrapeEngine::RemoveAllRoutePreviewSegments);
}

void RoutingManager::CancelRecommendation(Recommendation recommendation)
{
  if (recommendation == Recommendation::RebuildAfterPointsLoading)
    m_loadRoutePointsTimestamp = std::chrono::steady_clock::time_point();
}

TransitRouteInfo RoutingManager::GetTransitRouteInfo() const
{
  std::lock_guard<std::mutex> lock(m_drapeSubroutesMutex);
  return m_transitRouteInfo;
}

void RoutingManager::SetSubroutesVisibility(bool visible)
{
  df::DrapeEngineLockGuard lock(m_drapeEngine);
  if (!lock)
    return;

  std::lock_guard<std::mutex> lockSubroutes(m_drapeSubroutesMutex);
  for (auto const & subrouteId : m_drapeSubroutes)
    lock.Get()->SetSubrouteVisibility(subrouteId, visible);
}

bool RoutingManager::IsSpeedCamLimitExceeded() const
{
  return m_routingSession.IsSpeedCamLimitExceeded();
}

std::unique_ptr<IRouter> RoutingManager::MakeIndexRouter(VehicleType vehicleType, bool loadAltitudes) const
{
  auto const countryFileGetter = [this](m2::PointD const & p)
  { return m_callbacks.m_countryInfoGetter().GetRegionCountryId(p); };
  auto const getMwmRectByName = [this](std::string const & countryId)
  { return m_callbacks.m_countryInfoGetter().GetLimitRectForLeaf(countryId); };

  return std::make_unique<IndexRouter>(vehicleType, loadAltitudes, m_callbacks.m_countryParentNameGetterFn,
                                       countryFileGetter, getMwmRectByName, m_numMwmIDs, m_numMwmTree, m_routingSession,
                                       m_callbacks.m_dataSourceGetter());
}

namespace
{
// Maps&Fly: colors of the previewed routes, from the route and traffic colors of the map styles.
std::string const kRoutePreviewColor = "TrafficG4";
std::string const kRouteDetourColor = "RoadShieldOrangeBackground";
std::string const kRouteViaColor = "RoadShieldRedBackground";
// Below the active route, which has depth 10 and more: the optimal variant shows only where it differs.
double constexpr kRouteOptimalDepth = 5.0;
double constexpr kRouteDetourDepth = 6.0;
double constexpr kRouteViaDepth = 7.0;

std::vector<m2::PointD> GetPolylinePoints(RouteBase const & route)
{
  std::vector<m2::PointD> points;
  route.ForEachPoint([&points](geometry::PointWithAltitude const & p) { points.push_back(p.GetPoint()); });
  return points;
}
}  // namespace

void RoutingManager::CalculateRoutePreview(m2::PointD const & from, m2::PointD const & to, RouterType type,
                                           RoutePreviewCallback && callback)
{
  CHECK_THREAD_CHECKER(m_threadChecker, ());
  CHECK(m_routePreview, ("Routing is not initialized"));
  CHECK(type == RouterType::Pedestrian || type == RouterType::Bicycle, (type));

  HideRouteVariants();
  VehicleType const vehicleType = type == RouterType::Bicycle ? VehicleType::Bicycle : VehicleType::Pedestrian;
  m_routePreview->Calculate({from, to}, vehicleType,
                            [this, vehicleType, callback = std::move(callback)](RouteBase const * route)
  {
    if (!route)
    {
      callback(std::nullopt);
      return;
    }
    ShowRouteVariant(*route, {} /* reference */, kRoutePreviewColor, kRouteOptimalDepth);
    callback(route_preview::CalculateStats(*route, vehicleType));
  });
}

void RoutingManager::CancelRoutePreview()
{
  CHECK_THREAD_CHECKER(m_threadChecker, ());
  if (m_routePreview)
    m_routePreview->Cancel();
  HideRouteVariants();
}

bool RoutingManager::CalculateRouteInsertVariants(m2::PointD const & pt, RouteInsertVariantsCallback && callback)
{
  CHECK_THREAD_CHECKER(m_threadChecker, ());
  CHECK(m_routePreview, ("Routing is not initialized"));

  CancelRoutePreview();
  m_routeDetours.reset();

  auto const type = m_currentRouterType;
  auto const * route = m_routingSession.GetRoute();
  if ((type != RouterType::Pedestrian && type != RouterType::Bicycle) || route == nullptr || !route->IsValid())
    return false;
  VehicleType const vehicleType = type == RouterType::Bicycle ? VehicleType::Bicycle : VehicleType::Pedestrian;

  // While navigating, only the rest of the route from the current position counts.
  bool const isFollowing = m_routingSession.IsFollowing();
  auto const iter = route->GetCurrentIter();
  size_t const firstSegmentIdx = isFollowing && iter.IsValid() ? iter.m_ind : 0;
  size_t const firstLegIdx = isFollowing ? route->GetCurrentSubrouteIdx() : 0;

  // Legs are indexed like the subroutes; passed ones stay empty.
  std::vector<m2::PointD> checkpoints;
  std::vector<std::vector<m2::PointD>> legs(route->GetSubrouteCount());
  auto const & segments = route->GetRouteSegments();
  for (size_t legIdx = firstLegIdx; legIdx < legs.size(); ++legIdx)
  {
    auto const & attrs = route->GetSubrouteAttrs(legIdx);
    size_t const beginIdx = std::max(attrs.GetBeginSegmentIdx(), firstSegmentIdx);
    auto & leg = legs[legIdx];
    leg.push_back(beginIdx == attrs.GetBeginSegmentIdx() ? attrs.GetStart().GetPoint() : iter.m_pt);
    for (size_t i = beginIdx; i < attrs.GetEndSegmentIdx(); ++i)
      leg.push_back(segments[i].GetJunction().GetPoint());
    if (leg.size() < 2)
      leg.push_back(attrs.GetFinish().GetPoint());

    if (checkpoints.empty())
      checkpoints.push_back(leg.front());
    checkpoints.push_back(attrs.GetFinish().GetPoint());
  }

  // Only the remaining legs are candidates for detours.
  std::vector<std::vector<m2::PointD>> remainingLegs(legs.begin() + firstLegIdx, legs.end());
  auto projection = route_preview::ProjectToRoute(remainingLegs, pt);
  projection.m_legIdx += firstLegIdx;
  auto const makeDetour = [&](double gapM)
  {
    auto detour = route_preview::MakeDetour(legs, projection, gapM);
    // The current position is no route point, so a detour right from it leaves there.
    if (!detour.m_leave && isFollowing && projection.m_legIdx == firstLegIdx)
      detour.m_leave = legs[firstLegIdx].front();
    return detour;
  };
  auto const & leg = legs[projection.m_legIdx];
  auto const leaveAndRejoin = [&leg](route_preview::Detour const & detour)
  { return std::make_pair(detour.m_leave.value_or(leg.front()), detour.m_rejoin.value_or(leg.back())); };

  // The side trip leaves and rejoins a few meters around the closest point: the route refuses equal route points.
  double constexpr kSideTripGapM = 3.0;
  auto const sideTrip = makeDetour(kSideTripGapM);
  // The via route may leave and rejoin anywhere within this distance around the closest point; the router chooses.
  // Farther than the place to let the router find a way through it, but keeping most of the route.
  double const viaGapM = std::clamp(2.0 * mercator::DistanceOnEarth(pt, projection.m_point), 300.0, 3000.0);
  auto const via = makeDetour(viaGapM);
  m_routeDetours = RouteDetours{pt, via, sideTrip};

  auto const [sideLeave, sideRejoin] = leaveAndRejoin(sideTrip);
  auto const [viaLeave, viaRejoin] = leaveAndRejoin(via);

  auto variants = std::make_shared<RouteInsertVariants>();
  variants->m_current = route_preview::CalculateStats(*route, vehicleType, firstSegmentIdx);
  auto reference = std::make_shared<std::vector<std::vector<m2::PointD>>>(std::move(remainingLegs));
  struct ViaRoute
  {
    route_preview::Stats m_stats;
    std::vector<m2::PointD> m_points;
  };
  auto viaRoute = std::make_shared<std::optional<ViaRoute>>();

  // One router runs them one by one. Only the parts off the route are drawn.
  struct Request
  {
    std::vector<m2::PointD> m_checkpoints;
    route_preview::Calculator::Callback m_onResult;
  };
  auto requests = std::make_shared<std::vector<Request>>();
  requests->push_back({route_preview::InsertOptimally(std::move(checkpoints), pt), [=, this](RouteBase const * r)
  {
    if (!r)
      return;
    variants->m_optimal = route_preview::CalculateStats(*r, vehicleType);
    ShowRouteVariant(*r, *reference, kRoutePreviewColor, kRouteOptimalDepth);
  }});
  requests->push_back({{sideLeave, pt, sideRejoin},
                       [=, this](RouteBase const * r)
  {
    if (!r)
      return;
    // The few meters between leaving and rejoining are also on the planned route.
    variants->m_detour =
        route_preview::Add(variants->m_current, route_preview::CalculateStats(*r, vehicleType), vehicleType);
    ShowRouteVariant(*r, *reference, kRouteDetourColor, kRouteDetourDepth);
  }});
  // The via route replaces the planned route between its leave and rejoin points, so that part is needed too.
  requests->push_back({{viaLeave, pt, viaRejoin},
                       [=, this](RouteBase const * r)
  {
    if (r)
      *viaRoute = ViaRoute{route_preview::CalculateStats(*r, vehicleType), GetPolylinePoints(*r)};
  }});
  requests->push_back({{viaLeave, viaRejoin},
                       [=, this](RouteBase const * r)
  {
    if (!r || !*viaRoute)
      return;
    auto const stats = route_preview::Replace(variants->m_current, route_preview::CalculateStats(*r, vehicleType),
                                              (*viaRoute)->m_stats, vehicleType);
    // Shown only when it differs from the other variants.
    auto const differs = [&stats](std::optional<route_preview::Stats> const & other)
    {
      double constexpr kMinDiffM = 30.0;
      double constexpr kMinDiffRatio = 0.03;
      return !other ||
             std::abs(other->m_distanceM - stats.m_distanceM) > std::max(kMinDiffM, kMinDiffRatio * stats.m_distanceM);
    };
    if (differs(variants->m_optimal) && differs(variants->m_detour))
    {
      variants->m_via = stats;
      ShowPreviewLine((*viaRoute)->m_points, *reference, kRouteViaColor, kRouteViaDepth);
    }
  }});

  // Runs request |idx| and then the next ones; reports when all are done.
  // Runs request |idx| and then the next ones; reports when all are done. The pending router callback owns |run|,
  // so a cancelled chain is freed with it.
  auto run = std::make_shared<std::function<void(size_t)>>();
  *run =
      [this, requests, variants, vehicleType, weakRun = std::weak_ptr(run), callback = std::move(callback)](size_t idx)
  {
    if (idx == requests->size())
    {
      callback(*variants);
      return;
    }
    auto checkpointsCopy = (*requests)[idx].m_checkpoints;
    m_routePreview->Calculate(std::move(checkpointsCopy), vehicleType,
                              [requests, run = weakRun.lock(), idx](RouteBase const * r)
    {
      (*requests)[idx].m_onResult(r);
      (*run)(idx + 1);
    });
  };
  (*run)(0);
  return true;
}

bool RoutingManager::AddRouteDetour(RouteMarkData && point, std::string const & junctionTitle, bool sideTrip)
{
  CHECK_THREAD_CHECKER(m_threadChecker, ());
  ASSERT(m_bmManager != nullptr, ());

  if (!m_routeDetours || !m_routeDetours->m_target.EqualDxDy(point.m_position, mercator::kPointEqualityEps))
    return false;
  auto const detour = sideTrip ? m_routeDetours->m_sideTrip : m_routeDetours->m_via;

  auto const * route = m_routingSession.GetRoute();
  size_t const pointsCount = GetRoutePointsCount();
  // The detour belongs to the route the variants were calculated for.
  if (route == nullptr || route->GetSubrouteCount() + 1 != pointsCount ||
      pointsCount + 3 > RoutePointsLayout::kMaxRoutePointsCount)
  {
    return false;
  }

  point.m_isMyPosition = false;
  std::vector<RouteMarkData> stops;
  auto const addJunction = [&](std::optional<m2::PointD> const & position)
  {
    if (!position)
      return;
    RouteMarkData junction;
    junction.m_title = junctionTitle;
    junction.m_position = *position;
    stops.push_back(std::move(junction));
  };
  addJunction(detour.m_leave);
  stops.push_back(std::move(point));
  addJunction(detour.m_rejoin);

  // A new stop comes last among the stops, i.e. just before the finish; then it moves after the leg start.
  size_t targetIdx = detour.m_legIdx + 1;
  for (auto & stop : stops)
  {
    stop.m_pointType = RouteMarkType::Intermediate;
    if (!AddRoutePoint(std::move(stop), false /* optimize */))
      return false;
    size_t const addedIdx = GetRoutePointsCount() - 2;
    if (addedIdx != targetIdx)
      MoveRoutePoint(addedIdx, targetIdx);
    ++targetIdx;
  }
  m_routeDetours.reset();
  return true;
}

void RoutingManager::ShowRouteVariant(RouteBase const & route, std::vector<std::vector<m2::PointD>> const & reference,
                                      std::string const & color, double depth)
{
  ShowPreviewLine(GetPolylinePoints(route), reference, color, depth);
}

void RoutingManager::ShowPreviewLine(std::vector<m2::PointD> points,
                                     std::vector<std::vector<m2::PointD>> const & reference, std::string const & color,
                                     double depth)
{
  std::vector<std::vector<m2::PointD>> lines;
  if (reference.empty())
  {
    lines.push_back(std::move(points));
  }
  else
  {
    // Farther than the GPS and map inaccuracies, closer than a parallel path.
    double constexpr kMaxDistToRouteM = 15.0;
    lines = route_preview::FindDivergingParts(reference, points, kMaxDistToRouteM);
  }

  // The renderer hides the passed part of every subroute while navigating, measured by the active route. A distance
  // far beyond it keeps the previews whole. Taxi draws like the walking and cycling routes, without their masks for
  // later legs, but a bit wider, which suits a proposal.
  double constexpr kPreviewBaseDistance = 1e6;
  double constexpr kBias = 1.0;
  for (auto & line : lines)
  {
    if (line.size() < 2)
      continue;
    auto subroute = make_unique_dp<df::Subroute>();
    subroute->m_polyline = m2::PolylineD(std::move(line));
    subroute->m_baseDistance = kPreviewBaseDistance;
    subroute->m_baseDepthIndex = depth;
    subroute->m_headFakeDistance = -kBias;
    subroute->m_tailFakeDistance = subroute->m_polyline.GetLength() + kBias;
    subroute->m_routeType = df::RouteType::Taxi;
    subroute->AddStyle(df::SubrouteStyle(color, df::RoutePattern(4.0, 2.0)));
    auto const id =
        m_drapeEngine.SafeCallWithResult(&df::DrapeEngine::AddSubroute, df::SubrouteConstPtr(subroute.release()));
    m_routeVariantSubroutes.push_back(id);
  }
}

void RoutingManager::HideRouteVariants()
{
  for (auto const id : m_routeVariantSubroutes)
    m_drapeEngine.SafeCall(&df::DrapeEngine::RemoveSubroute, id, false /* deactivateFollowing */);
  m_routeVariantSubroutes.clear();
}
