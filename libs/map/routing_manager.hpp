#pragma once

#include "map/bookmark_manager.hpp"
#include "map/extrapolation/extrapolator.hpp"
#include "map/route_preview.hpp"
#include "map/routing_mark.hpp"
#include "map/transit/transit_display.hpp"
#include "map/transit/transit_reader.hpp"

#include "routing/following_info.hpp"
#include "routing/route.hpp"
#include "routing/router.hpp"
#include "routing/routing_callbacks.hpp"
#include "routing/routing_session.hpp"
#include "routing/speed_camera_manager.hpp"
#include "routing/track_router.hpp"

#include "storage/storage_defines.hpp"

#include "drape_frontend/drape_engine_safe_ptr.hpp"

#include "drape/pointers.hpp"

#include "base/thread_checker.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace storage
{
class CountryInfoGetter;
}

namespace routing
{
class NumMwmIds;
}

namespace track_following
{
struct JoinedTrack;
}

class DataSource;

namespace power_management
{
class PowerManager;
}

struct RoutePointInfo
{
  std::string m_name;
  RouteMarkType m_markType = RouteMarkType::Start;
  size_t m_intermediateIndex = 0;
  bool m_isPassed = false;
  bool m_isMyPosition = false;
  m2::PointD m_position;
};

class RoutingManager final
{
public:
  class Delegate
  {
  public:
    virtual void OnRouteFollow(routing::RouterType type) = 0;
    virtual ~Delegate() = default;
  };

  /// @todo Refactor and replace with the single interface of multiple function-members.
  struct Callbacks
  {
    using DataSourceGetterFn = std::function<DataSource &()>;
    using CountryInfoGetterFn = std::function<storage::CountryInfoGetter const &()>;
    using CountryParentNameGetterFn = std::function<std::string(std::string const &)>;
    using GetStringsBundleFn = std::function<StringsBundle const &()>;
    using PowerManagerGetter = std::function<power_management::PowerManager const &()>;

    template <typename DataSourceGetter, typename CountryInfoGetter, typename CountryParentNameGetter,
              typename StringsBundleGetter, typename PowerManagerGetter>
    Callbacks(DataSourceGetter && dataSourceGetter, CountryInfoGetter && countryInfoGetter,
              CountryParentNameGetter && countryParentNameGetter, StringsBundleGetter && stringsBundleGetter,
              PowerManagerGetter && powerManagerGetter)
      : m_dataSourceGetter(std::forward<DataSourceGetter>(dataSourceGetter))
      , m_countryInfoGetter(std::forward<CountryInfoGetter>(countryInfoGetter))
      , m_countryParentNameGetterFn(std::forward<CountryParentNameGetter>(countryParentNameGetter))
      , m_stringsBundleGetter(std::forward<StringsBundleGetter>(stringsBundleGetter))
      , m_powerManagerGetter(std::forward<PowerManagerGetter>(powerManagerGetter))
    {}

    DataSourceGetterFn m_dataSourceGetter;
    CountryInfoGetterFn m_countryInfoGetter;
    CountryParentNameGetterFn m_countryParentNameGetterFn;
    GetStringsBundleFn m_stringsBundleGetter;
    PowerManagerGetter m_powerManagerGetter;
  };

  using RouteBuildingCallback = std::function<void(routing::RouterResultCode, storage::CountriesSet const &)>;
  using RouteSpeedCamShowCallback = std::function<void(m2::PointD const &, double)>;
  using RouteSpeedCamsClearCallback = std::function<void()>;

  using RouteStartBuildCallback = std::function<void(std::vector<RouteMarkData> const & points)>;

  enum class Recommendation
  {
    // It can be recommended if location is found almost immediately
    // after restoring route points from file. In this case we can
    // rebuild route using "my position".
    RebuildAfterPointsLoading = 0,
  };
  using RouteRecommendCallback = std::function<void(Recommendation)>;

  RoutingManager(Callbacks && callbacks, Delegate & delegate);
  void Init(std::shared_ptr<routing::NumMwmIds> ptr);

  void SetBookmarkManager(BookmarkManager * bmManager);
  void SetTransitManager(TransitReadManager * transitManager);

  routing::RoutingSession const & RoutingSession() const { return m_routingSession; }
  routing::RoutingSession & RoutingSession() { return m_routingSession; }
  void SetRouter(routing::RouterType type);
  routing::RouterType GetRouter() const { return m_currentRouterType; }
  /// Maps&Fly: routes follow |track| (pedestrian or bicycle profile) instead of the roads until
  /// CloseRouting(true) removes the route points. The route points must be the start and the track end.
  /// nullptr returns to normal routing.
  void SetFollowedTrack(std::shared_ptr<routing::TrackFollowingData const> track);
  bool IsFollowingTrack() const { return m_followedTrack != nullptr; }
  /// Maps&Fly: until navigation reaches the followed track, moves its entry to the point of |track| closest to
  /// the position, so the straight approach always leads there. |track| is the whole track, followed backwards
  /// with |reverse|. Call after SetFollowedTrack(), which stops it.
  /// Maps&Fly: shows a dotted straight line from |from| to |to|, e.g. from the position to the closest point of a
  /// watched track. Replaces the previous line.
  void ShowTrackApproachLine(m2::PointD const & from, m2::PointD const & to);
  void HideTrackApproachLine();
  void SetFollowedTrackLiveEntry(std::shared_ptr<track_following::JoinedTrack const> track, bool reverse);
  bool IsRoutingActive() const { return m_routingSession.IsActive(); }
  bool IsRouteBuilt() const { return m_routingSession.IsBuilt(); }
  bool IsRouteBuilding() const { return m_routingSession.IsBuilding(); }
  bool IsRouteRebuildingOnly() const { return m_routingSession.IsRebuildingOnly(); }
  bool IsRouteFinished() const { return m_routingSession.IsFinished(); }
  bool IsOnRoute() const { return m_routingSession.IsOnRoute(); }
  bool IsRoutingFollowing() const { return m_routingSession.IsFollowing(); }
  bool IsRouteValid() const { return m_routingSession.IsRouteValid(); }
  void BuildRoute(uint32_t timeoutSec = routing::RouterDelegate::kNoTimeout);
  bool HaveRoutePlanningOptionsChanged() const;
  void SetUserCurrentPosition(m2::PointD const & position);
  void ResetRoutingSession() { m_routingSession.Reset(); }
  // FollowRoute has a bug where the router follows the route even if the method hads't been called.
  // This method was added because we do not want to break the behaviour that is familiar to our
  // users.
  bool DisableFollowMode();
  kml::TrackId SaveRoute();

  void SetRouteBuildingListener(RouteBuildingCallback const & buildingCallback)
  {
    m_routingBuildingCallback = buildingCallback;
  }

  void SetRouteSpeedCamShowListener(RouteSpeedCamShowCallback const & speedCamShowCallback)
  {
    m_routeSpeedCamShowCallback = speedCamShowCallback;
  }

  void SetRouteSpeedCamsClearListener(RouteSpeedCamsClearCallback const & speedCamsClearCallback)
  {
    m_routeSpeedCamsClearCallback = speedCamsClearCallback;
  }

  /// See warning above.
  void SetRouteProgressListener(routing::ProgressCallback const & progressCallback)
  {
    m_routingSession.SetProgressCallback(progressCallback);
  }
  void SetRouteRecommendationListener(RouteRecommendCallback const & recommendCallback)
  {
    m_routeRecommendCallback = recommendCallback;
  }
  void FollowRoute();
  void CloseRouting(bool removeRoutePoints);

  /// \brief Activate alternative |idx| (typically triggered by tapping its ETA balloon).
  /// Returns false if the index is out of range, already active, or its adjustment caches are
  /// unavailable or belong to another result. Re-renders the drape to highlight the selected variant.
  bool SwapActiveAlternative(size_t idx);

  /// \brief Hit-tests |mercator| against the alternative-route polylines. If one is closer than
  /// a tap-area threshold (kTapPixels * |mercatorPerPixel|), swaps it to active and returns true.
  /// No-op when navigation is active (alts aren't drawn then) or there are no alternatives.
  bool TryTapOnAlternativeRoute(m2::PointD const & mercator, double mercatorPerPixel);
  void GetRouteFollowingInfo(routing::FollowingInfo & info) const { m_routingSession.GetRouteFollowingInfo(info); }

  TransitRouteInfo GetTransitRouteInfo() const;

  m2::PointD GetRouteEndPoint() const { return m_routingSession.GetEndPoint(); }
  /// Returns the most situable router engine type.
  routing::RouterType GetBestRouter(m2::PointD const & startPoint, m2::PointD const & finalPoint) const;
  routing::RouterType GetLastUsedRouter() const;
  void SetLastUsedRouter(routing::RouterType type);
  // Sound notifications for turn instructions.
  void EnableTurnNotifications(bool enable) { m_routingSession.EnableTurnNotifications(enable); }
  bool AreTurnNotificationsEnabled() const { return m_routingSession.AreTurnNotificationsEnabled(); }
  /// \brief Sets a locale for TTS.
  /// \param locale is a string with locale code. For example "en", "ru", "zh-Hant" and so on.
  /// \note See data/sound-strings/ for the available locales.
  void SetTurnNotificationsLocale(std::string const & locale) { m_routingSession.SetTurnNotificationsLocale(locale); }
  /// @return current TTS locale. For example "en", "ru", "zh-Hant" and so on.
  /// In case of error returns an empty string.
  /// \note The method returns correct locale after SetTurnNotificationsLocale has been called.
  /// If not, it returns an empty string.
  std::string GetTurnNotificationsLocale() const { return m_routingSession.GetTurnNotificationsLocale(); }
  // @return polyline of the route.
  m2::PolylineD const & GetRoutePolyline() const { return m_routingSession.GetRoute()->GetPoly(); }
  // @return generated turns on the route.
  std::vector<routing::turns::TurnItem> GetTurnsOnRouteForTests() const
  {
    std::vector<routing::turns::TurnItem> turns;
    m_routingSession.GetRoute()->GetTurnsForTesting(turns);
    return turns;
  }

  Callbacks & GetCallbacksForTests() { return m_callbacks; }
  /// \brief Adds to @param notifications strings - notifications, which are ready to be
  /// pronounced to end user right now.
  /// Adds notifications about turns and speed camera on the road.
  /// \param announceStreets is true when TTS street names should be pronounced
  ///
  /// \note Current notifications will be deleted after call and second call
  /// will not return previous data, only newer.
  void GenerateNotifications(std::vector<std::string> & notifications, bool announceStreets);

  /// Appends an intermediate stop before the finish, ignoring markData.m_intermediateIndex; Start/Finish replace the
  /// existing endpoint instead. A new stop is dropped at capacity, except when it takes over an existing My Position
  /// mark. optimize places only the new stop at the predicted position (needs both endpoints, ignored in Ruler mode,
  /// still applied while following). Returns false when a new point cannot be added at capacity.
  bool AddRoutePoint(RouteMarkData && markData, bool optimize);
  /// Replaces an existing slot without optimization. If the target is gone, adds the point normally; otherwise the
  /// target slot's current type overrides markData.
  void ReplaceRoutePoint(RouteMarkType type, size_t intermediateIndex, RouteMarkData && markData);
  /// Reorders stops after the last passed mark in place, keeping endpoints, passed marks, and earlier stops fixed.
  /// Returns whether the order changed; the caller rebuilds. No-op while following, in Ruler mode, or without
  /// both endpoints.
  bool OptimizeRoutePoints();
  bool ContinueRouteToPoint(RouteMarkData && markData);
  std::vector<RouteMarkData> GetRoutePoints() const;
  size_t GetRoutePointsCount() const;
  void RemoveRoutePoint(RouteMarkType type, size_t intermediateIndex = 0);
  void RemoveRoutePoints();
  void RemoveIntermediateRoutePoints();
  void RemovePassedRoutePoints();
  void MoveRoutePoint(size_t currentIndex, size_t targetIndex);
  void MoveRoutePoint(RouteMarkType currentType, size_t currentIntermediateIndex, RouteMarkType targetType,
                      size_t targetIntermediateIndex);
  /// Reverses the complete route, including intermediate points. Returns false without changing points if either
  /// endpoint is missing.
  bool ReverseRoutePoints();
  void HideRoutePoint(RouteMarkType type, size_t intermediateIndex = 0);
  bool CouldAddIntermediatePoint() const;
  bool IsMyPosition(RouteMarkType type, size_t intermediateIndex = 0);

  void RemoveRoute(bool deactivateFollowing);

  void CheckLocationForRouting(location::GpsInfo const & info);
  /// Maps&Fly: @return whether the entry of the followed track moved, so the route needs a rebuild.
  bool UpdateLiveTrackEntry(m2::PointD const & position, bool force);
  std::unique_ptr<routing::IRouter> MakeTrackRouter(routing::VehicleType vehicleType) const;
  void CallRouteBuilded(routing::RouterResultCode code, storage::CountriesSet const & absentCountries);
  void OnBuildRouteReady(routing::RoutesResult const & result, routing::RouterResultCode code);
  void OnRebuildRouteReady(routing::RoutesResult const & result, routing::RouterResultCode code);
  void OnNeedMoreMaps(uint64_t routeId, storage::CountriesSet const & absentCountries);
  void OnRemoveRoute(routing::RouterResultCode code);
  void OnRoutePointPassed(RouteMarkType type, size_t intermediateIndex);
  void OnLocationUpdate(location::GpsInfo const & info);

  routing::SpeedCameraManager & GetSpeedCamManager() { return m_routingSession.GetSpeedCamManager(); }
  bool IsSpeedCamLimitExceeded() const;

  void SetTurnNotificationsUnits(measurement_utils::Units const units)
  {
    m_routingSession.SetTurnNotificationsUnits(units);
  }
  void SetDrapeEngine(ref_ptr<df::DrapeEngine> engine, bool is3dAllowed);
  /// \returns true if altitude information along |m_route| is available and
  /// false otherwise.
  bool HasRouteAltitude() const;

  /// Returns true for a valid route with complete heights and a nonzero height after simplification.
  bool GetRouteElevationInfo(ElevationInfo & ei) const;

  /// Maps&Fly: route length in meters by path grade (SAC T1-T2 or ungraded, T3, T4-T6).
  struct PathGradeDistances
  {
    double m_normalM = 0;
    double m_difficultM = 0;
    double m_expertM = 0;
  };
  /// Returns nullopt without a valid route.
  std::optional<PathGradeDistances> GetRoutePathGradeDistances() const;

  /// Maps&Fly: way types and surfaces of the route, like Komoot shows them.
  enum class RouteWayType : uint8_t
  {
    Road,       // motorway to tertiary
    MinorRoad,  // unclassified, residential, service, living street
    Track,
    Cycleway,
    Footway,  // footway, pedestrian street, platform, pier
    Path,     // path, bridleway
    Steps,
    Ferry,
    Other,
    Count
  };
  enum class RouteSurface : uint8_t
  {
    PavedGood,
    PavedBad,
    UnpavedGood,
    UnpavedBad,
    Unknown,
    Count
  };
  struct RouteWayStats
  {
    std::array<double, static_cast<size_t>(RouteWayType::Count)> m_wayTypeM{};
    std::array<double, static_cast<size_t>(RouteSurface::Count)> m_surfaceM{};
    // Unpaved, or untagged on a track or path: the "nature" share of the route.
    double m_naturalM = 0;
  };
  /// Reads the features of the route; returns nullopt without a valid route.
  std::optional<RouteWayStats> GetRouteWayStats() const;

  /// \brief Interpolates a point along the current route polyline at the given distance from start.
  /// \return Nullopt if the route is invalid.
  std::optional<m2::PointD> GetRoutePointAtDistance(double distanceMeters) const;

  uint32_t OpenRoutePointsTransaction();
  void ApplyRoutePointsTransaction(uint32_t transactionId);
  void CancelRoutePointsTransaction(uint32_t transactionId);
  static uint32_t InvalidRoutePointsTransactionId();

  /// \returns true if there are route points saved in file and false otherwise.
  bool HasSavedRoutePoints() const;
  /// Synchronously restores into an empty, inactive itinerary on the GUI thread. Returns whether points were restored.
  /// Leaves recovery untouched while an itinerary is present or active; consumes the file after a read attempt.
  bool LoadRoutePoints();
  /// Saves remaining route points synchronously on the GUI thread; incomplete or finished routes discard recovery.
  void SaveRoutePoints();
  /// Deletes saved recovery synchronously on the GUI thread.
  void DeleteSavedRoutePoints();

  void UpdatePreviewMode();
  void CancelPreviewMode();

  routing::RouterType GetCurrentRouterType() const { return m_currentRouterType; }

  /// Maps&Fly: calculates a pedestrian or bicycle route from |from| to |to| next to the routing session,
  /// e.g. for a tapped place. A new request or CancelRoutePreview() drops the previous one.
  /// The callback gets nullopt when no route was found.
  using RoutePreviewCallback = std::function<void(std::optional<route_preview::Stats> const & stats)>;
  void CalculateRoutePreview(m2::PointD const & from, m2::PointD const & to, routing::RouterType type,
                             RoutePreviewCallback && callback);
  /// Also hides the routes drawn by the preview.
  void CancelRoutePreview();

  struct RouteInsertVariants
  {
    route_preview::Stats m_current;
    /// The point is inserted between the stops where it adds the least time, and that leg is planned anew.
    std::optional<route_preview::Stats> m_optimal;
    /// The planned route stays, with a way from its closest point to the point and back.
    std::optional<route_preview::Stats> m_detour;
    /// The planned route stays, but leaves for the point and joins again further on, chosen by the router within
    /// a few hundred meters around the point. Unset when it equals another variant.
    std::optional<route_preview::Stats> m_via;
  };
  using RouteInsertVariantsCallback = std::function<void(RouteInsertVariants const & variants)>;
  /// Maps&Fly: while a walking or cycling route is planned or followed, calculates the ways to add |pt| and draws
  /// them next to the route until CancelRoutePreview(). While following, only the rest of the route counts.
  /// @return false without such a route.
  bool CalculateRouteInsertVariants(m2::PointD const & pt, RouteInsertVariantsCallback && callback);
  enum class RouteInsertKind
  {
    Optimal,
    Via,
    SideTrip
  };
  /// Maps&Fly: adds a variant of the last CalculateRouteInsertVariants() to |point| as stops: where it leaves the
  /// route, with |junctionTitle|, |point| and where it rejoins the route. Rebuild the route then.
  /// @return false when the variant is not calculated (yet) or the route changed.
  bool AddRouteDetour(RouteMarkData && point, std::string const & junctionTitle, RouteInsertKind kind);

private:
  void SetRouterImpl(routing::RouterType type);
  std::unique_ptr<routing::IRouter> MakeIndexRouter(routing::VehicleType vehicleType, bool loadAltitudes) const;
  /// Draws |route|, or only its parts off |reference| when that is not empty.
  void ShowRouteVariant(routing::RouteBase const & route, std::vector<std::vector<m2::PointD>> const & reference,
                        std::string const & color, double depth);
  void ShowPreviewLine(std::vector<m2::PointD> points, std::vector<std::vector<m2::PointD>> const & reference,
                       std::string const & color, double depth);
  void HideRouteVariants();

  struct RoadInfo
  {
    RoadInfo() = default;

    explicit RoadInfo(m2::PointD const & pt, FeatureID const & featureId) : m_startPoint(pt), m_featureId(featureId) {}

    m2::PointD m_startPoint;
    FeatureID m_featureId;
    double m_distance = 0.0;
  };
  using RoadWarningsCollection = std::map<RoadWarningMarkType, std::vector<RoadInfo>>;

  MwmSet::MwmId GetMwmId(routing::NumMwmId numMwmId) const;

  /// \brief Renders every route in |result| via drape subroutes. The active alternative
  /// (result.m_activeIdx) is drawn with normal styling; the rest are dimmed. Also creates
  /// road-warning marks for the active route.
  /// \returns true if the active route has an avoidable warning (toll/ferry/dirty) on a car route,
  /// i.e. one that should surface the "driving options" affordance (RouterResultCode::HasWarnings).
  bool InsertRoute(routing::RoutesResult const & result);

  // Helper: build drape subroutes for a single route. |isActive| controls styling
  // (alternatives are dimmed). |roadWarnings| is appended for every route (active and alternatives)
  // so warning marks of all routes are shown together.
  void InsertSingleRoute(routing::RouteBase const & route, bool isActive, double depthOffset,
                         std::shared_ptr<TransitRouteDisplay> const & transitRouteDisplay,
                         RoadWarningsCollection & roadWarnings);

  // Linear warnings (toll/ferry/dirty/steps): a span of the route sharing the same road type.
  void CollectRoadWarnings(std::vector<routing::RouteSegment> const & segments, m2::PointD const & startPt,
                           double baseDistance, RoadWarningsCollection & roadWarnings);
  // Point warnings (gate/lift_gate): barrier nodes precomputed on the routing thread and stored in
  // the route (see routing::RouteBase::GetWarnings); here we just map them to mark types.
  void CollectRoadPointWarnings(routing::RouteBase const & route, RoadWarningsCollection & roadWarnings);
  void CreateRoadWarningMarks(RoadWarningsCollection && roadWarnings);

  // Creates an ETA balloon (RouteAltMark) at the midpoint of each route variant in |result|.
  // Active variant uses the route palette; alternatives use a dimmer palette.
  void CreateRouteAltMarks(routing::RoutesResult const & result);

  // Synchronously remove the alternative-route subroutes from drape and clear the alt ETA
  // balloons when rebuilding or entering navigation. The active route is left untouched.
  void ClearAlternativeRoutes();

  /// \returns false if the location could not be matched to the route and should be matched to the
  /// road graph. Otherwise returns true.
  void MatchLocationToRoute(location::GpsInfo & info, location::RouteMatchingInfo & routeMatchingInfo);
  location::RouteMatchingInfo GetRouteMatchingInfo(location::GpsInfo & info);
  uint32_t GenerateRoutePointsTransactionId() const;

  void SetPointsFollowingMode(bool enabled);

  /// Places |added| among the unpassed stops, keeping their relative order, or re-inserts all of them if it is null.
  /// Returns whether any stop index changed.
  bool ReorderIntermediatePoints(RoutePointsLayout & layout, RouteMarkPoint const * added);

  m2::RectD ShowPreviewSegments(std::vector<RouteMarkData> const & routePoints);
  void HidePreviewSegments();

  void SetSubroutesVisibility(bool visible);

  void CancelRecommendation(Recommendation recommendation);

  std::vector<RouteMarkData> ReadAndDeleteSavedRoutePoints() const;
  std::vector<RouteMarkData> GetRoutePointsToSave() const;

  void OnExtrapolatedLocationUpdate(location::GpsInfo const & info);

  RouteBuildingCallback m_routingBuildingCallback;
  RouteSpeedCamShowCallback m_routeSpeedCamShowCallback;
  RouteSpeedCamsClearCallback m_routeSpeedCamsClearCallback;
  RouteRecommendCallback m_routeRecommendCallback;
  Callbacks m_callbacks;
  df::DrapeEngineSafePtr m_drapeEngine;
  routing::RouterType m_currentRouterType = routing::RouterType::Count;
  bool m_loadAltitudes = false;
  std::shared_ptr<routing::TrackFollowingData const> m_followedTrack;
  std::shared_ptr<track_following::JoinedTrack const> m_liveEntryTrack;
  bool m_liveEntryReverse = false;
  std::optional<dp::DrapeID> m_trackApproachLineId;
  routing::RoutingSession m_routingSession;
  Delegate & m_delegate;

  BookmarkManager * m_bmManager = nullptr;
  extrapolation::Extrapolator m_extrapolator;

  std::vector<dp::DrapeID> m_drapeSubroutes;
  mutable std::mutex m_drapeSubroutesMutex;

  std::unique_ptr<location::GpsInfo> m_gpsInfoCache;

  TransitRouteInfo m_transitRouteInfo;

  struct RoutePointsTransaction
  {
    std::vector<RouteMarkData> m_routeMarks;
  };
  std::map<uint32_t, RoutePointsTransaction> m_routePointsTransactions;
  std::chrono::steady_clock::time_point m_loadRoutePointsTimestamp;
  std::map<std::string, m2::PointF> m_transitSymbolSizes;

  std::shared_ptr<routing::NumMwmIds> m_numMwmIDs;
  std::shared_ptr<m4::Tree<routing::NumMwmId>> m_numMwmTree;

  std::unique_ptr<route_preview::Calculator> m_routePreview;
  std::vector<dp::DrapeID> m_routeVariantSubroutes;
  struct RouteDetours
  {
    m2::PointD m_target;
    /// Set when its calculation is done.
    std::optional<route_preview::Detour> m_optimal;
    route_preview::Detour m_via;
    route_preview::Detour m_sideTrip;
  };
  std::optional<RouteDetours> m_routeDetours;

  TransitReadManager * m_transitReadManager = nullptr;

  DECLARE_THREAD_CHECKER(m_threadChecker);
};
