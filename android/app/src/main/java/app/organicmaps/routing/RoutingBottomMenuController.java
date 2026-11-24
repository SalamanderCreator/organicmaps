package app.organicmaps.routing;

import android.annotation.SuppressLint;
import android.app.Activity;
import android.content.Context;
import android.content.res.Resources;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.os.Bundle;
import android.text.Spannable;
import android.text.SpannableStringBuilder;
import android.text.Spanned;
import android.text.TextUtils;
import android.text.style.AbsoluteSizeSpan;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.text.style.TypefaceSpan;
import android.view.View;
import android.widget.TextView;
import android.widget.Toast;
import androidx.annotation.IdRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.core.content.ContextCompat;
import androidx.recyclerview.widget.RecyclerView;
import app.organicmaps.MwmActivity;
import app.organicmaps.R;
import app.organicmaps.bookmarks.BookmarksSharingHelper;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.Router;
import app.organicmaps.sdk.bookmarks.data.DistanceAndAzimut;
import app.organicmaps.sdk.bookmarks.data.FileType;
import app.organicmaps.sdk.routing.RouteAltitudeData;
import app.organicmaps.sdk.routing.RouteMarkData;
import app.organicmaps.sdk.routing.RoutingController;
import app.organicmaps.sdk.routing.RoutingInfo;
import app.organicmaps.sdk.routing.RoutingOptions;
import app.organicmaps.sdk.routing.TransitRouteInfo;
import app.organicmaps.sdk.routing.TransitStepInfo;
import app.organicmaps.sdk.settings.RoadType;
import app.organicmaps.sdk.util.Distance;
import app.organicmaps.sdk.util.StringUtils;
import app.organicmaps.util.ThemeUtils;
import app.organicmaps.util.UiUtils;
import app.organicmaps.util.Utils;
import app.organicmaps.widget.RoutingProgressButton;
import app.organicmaps.widget.recycler.DotDividerItemDecoration;
import app.organicmaps.widget.recycler.MultilineLayoutManager;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;
import java.text.NumberFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedList;
import java.util.List;

final class RoutingBottomMenuController
{
  enum StartState
  {
    ENABLED,
    DISABLED,
    BUILDING
  }

  private static final String STATE_ALTITUDE_CHART_SHOWN = "altitude_chart_shown";
  private static final String STATE_ERROR = "error";
  private static final float SAVE_BUTTON_DISABLED_ALPHA = 0.5f;

  @NonNull
  private final Activity mContext;
  @NonNull
  private final View mTimeElevationLine;
  @NonNull
  private final View mAltitudeChartFrame;
  @NonNull
  private final View mTransitTime;
  @NonNull
  private final TextView mError;
  @NonNull
  private final RoutingProgressButton mStart;
  @NonNull
  private final View mAltitudeChart;
  @NonNull
  private final RouteElevationChartController mRouteElevationChartController;
  @NonNull
  private final TextView mTime;
  @NonNull
  private final TextView mAltitudeDifference;
  @NonNull
  private final TextView mTimeVehicle;
  @NonNull
  private final TextView mTimeRuler;

  @Nullable
  private final TextView mArrival;
  @NonNull
  private final DotDividerItemDecoration mTransitViewDecorator;
  @NonNull
  private final TransitStepAdapter mTransitAdapter;
  // Compact leg-badge strip; tapping it (or the summary row above) opens the full-screen per-leg details.
  @NonNull
  private final RecyclerView mTransitRecyclerView;
  @NonNull
  private final View mSaveButton;
  @NonNull
  private final TextView mPathGrades;
  @NonNull
  private final View mSurfaceSummary;
  @NonNull
  private final View mSurfaceDetails;
  private boolean mSurfaceDetailsShown;
  @Nullable
  private final View mManageRoutePanel;
  @Nullable
  private ManageRouteController mManageRouteController;

  @NonNull
  private final RoutingBottomMenuListener mListener;
  @Nullable
  private Runnable mVisibilityChangedCallback;
  @NonNull
  private StartState mStartState = StartState.DISABLED;

  // Order of RoutingManager::RouteWayType and RouteSurface.
  private static final int[] WAY_TYPE_NAMES = {
      R.string.route_way_road,     R.string.route_way_minor_road, R.string.route_way_track,
      R.string.route_way_cycleway, R.string.route_way_footway,    R.string.route_way_path,
      R.string.route_way_steps,    R.string.route_way_ferry,      R.string.route_way_other};
  private static final int[] WAY_TYPE_COLORS = {
      R.color.route_way_road,     R.color.route_way_minor_road, R.color.route_way_track,
      R.color.route_way_cycleway, R.color.route_way_footway,    R.color.route_way_path,
      R.color.route_way_steps,    R.color.route_way_ferry,      R.color.route_stats_unknown};
  private static final int[] SURFACE_NAMES = {R.string.route_surface_paved_good, R.string.route_surface_paved_bad,
                                              R.string.route_surface_unpaved_good, R.string.route_surface_unpaved_bad,
                                              R.string.route_surface_unknown};
  private static final int[] SURFACE_COLORS = {R.color.route_paved, R.color.route_surface_paved_bad,
                                               R.color.route_natural, R.color.route_surface_unpaved_bad,
                                               R.color.route_stats_unknown};

  @NonNull
  static RoutingBottomMenuController newInstance(@NonNull Activity activity, @NonNull View frame,
                                                 @NonNull View chartPanel,
                                                 @Nullable RecyclerView.Adapter<?> headerAdapter,
                                                 @NonNull RoutingBottomMenuListener listener)
  {
    // Chart-related ids live inside the chartPanel view tree (it is inflated standalone and hosted by the
    // route-list ConcatAdapter header), so they must be resolved from chartPanel rather than from frame.
    View timeElevationLine = chartPanel.findViewById(R.id.time_elevation_line);
    View transitTime = chartPanel.findViewById(R.id.transit_time);
    TextView rulerTime = chartPanel.findViewById(R.id.time_ruler);
    TextView error = (TextView) getViewById(activity, frame, R.id.error);
    RoutingProgressButton start = (RoutingProgressButton) getViewById(activity, frame, R.id.start);
    View altitudeChart = chartPanel.findViewById(R.id.altitude_chart);
    TextView time = chartPanel.findViewById(R.id.time);
    TextView timeVehicle = chartPanel.findViewById(R.id.time_vehicle);
    TextView altitudeDifference = chartPanel.findViewById(R.id.altitude_difference);
    TextView arrival = (TextView) getViewById(activity, frame, R.id.arrival);
    View saveButton = getViewById(activity, frame, R.id.btn__save);
    return new RoutingBottomMenuController(activity, chartPanel, timeElevationLine, transitTime, rulerTime, error,
                                           start, altitudeChart, time, altitudeDifference, timeVehicle, arrival,
                                           saveButton, headerAdapter, listener);
  }

  @NonNull
  private static View getViewById(@NonNull Activity activity, @NonNull View frame, @IdRes int resourceId)
  {
    View view = frame.findViewById(resourceId);
    return view == null ? activity.findViewById(resourceId) : view;
  }

  private RoutingBottomMenuController(@NonNull Activity context, @NonNull View altitudeChartFrame,
                                      @NonNull View timeElevationLine, @NonNull View transitTime,
                                      @NonNull TextView rulerTime, @NonNull TextView error,
                                      @NonNull RoutingProgressButton start, @NonNull View altitudeChart,
                                      @NonNull TextView time, @NonNull TextView altitudeDifference,
                                      @NonNull TextView timeVehicle, @Nullable TextView arrival,
                                      @NonNull View saveButton, @Nullable RecyclerView.Adapter<?> headerAdapter,
                                      @NonNull RoutingBottomMenuListener listener)
  {
    mContext = context;
    mAltitudeChartFrame = altitudeChartFrame;
    mTimeElevationLine = timeElevationLine;
    mTransitTime = transitTime;
    mTimeRuler = rulerTime;
    mError = error;
    mStart = start;
    mAltitudeChart = altitudeChart;
    mRouteElevationChartController = new RouteElevationChartController(mAltitudeChart);
    mRouteElevationChartController.setListener(new RouteElevationChartController.ElevationSelectionListener() {
      @Override
      public void onElevationPointSelected(double distanceMeters)
      {
        Framework.nativeRouteSetElevationActivePoint(distanceMeters);
      }

      @Override
      public void onElevationPointDeselected()
      {
        Framework.nativeRouteRemoveElevationActivePoint();
      }
    });
    mTime = time;
    mAltitudeDifference = altitudeDifference;
    mTimeVehicle = timeVehicle;
    mArrival = arrival;
    mSaveButton = saveButton;
    mPathGrades = altitudeChartFrame.findViewById(R.id.route_path_grades);
    mSurfaceSummary = altitudeChartFrame.findViewById(R.id.route_surface_summary);
    mSurfaceDetails = altitudeChartFrame.findViewById(R.id.route_surface_details);
    mSurfaceSummary.setOnClickListener(v -> {
      mSurfaceDetailsShown = !mSurfaceDetailsShown;
      UiUtils.showIf(mSurfaceDetailsShown, mSurfaceDetails);
      notifyVisibilityChanged();
    });
    mListener = listener;
    int dividerRes = ThemeUtils.getResource(mContext, R.attr.transitStepDivider);
    Drawable dividerDrawable = ContextCompat.getDrawable(mContext, dividerRes);
    Resources res = mContext.getResources();
    mTransitViewDecorator =
        new DotDividerItemDecoration(dividerDrawable, res.getDimensionPixelSize(R.dimen.margin_base),
                                     res.getDimensionPixelSize(R.dimen.margin_half));
    mManageRoutePanel = mContext.findViewById(R.id.manage_route_panel);
    if (mManageRoutePanel != null)
    {
      mManageRouteController =
          new ManageRouteController(mManageRoutePanel, headerAdapter, new ManageRouteController.ManageRouteCallback() {
            @Override
            public void onAddStop()
            {
              final RoutingController controller = RoutingController.get();
              if (controller.getStartPoint() == null || controller.getEndPoint() == null)
                return;
              if (Framework.nativeIsRoutePointsLimitReached())
              {
                ((MwmActivity) mContext).onStopPointLimitReached();
                return;
              }
              controller.waitForPoiPickToAppend();
              openSearchForRoutePick();
            }
            @Override
            public void onOpenRouteSearch()
            {
              openSearchForRoutePick();
            }
          });
    }
    mSaveButton.setOnClickListener(v -> showSaveMenu());
    mStart.setEnabled(false);
    mStart.setOnClickListener(v -> {
      // Ignore the event if the back and start buttons are pressed at the same time.
      // https://github.com/organicmaps/organicmaps/issues/6628
      if (RoutingController.get().isPlanning())
        mListener.onRoutingStart();
    });
    mTransitRecyclerView = altitudeChartFrame.findViewById(R.id.transit_recycler_view);
    mTransitAdapter = new TransitStepAdapter();
    mTransitRecyclerView.setLayoutManager(new MultilineLayoutManager(mAltitudeChartFrame.getLayoutDirection()));
    mTransitRecyclerView.setNestedScrollingEnabled(false);
    mTransitRecyclerView.addItemDecoration(mTransitViewDecorator);
    mTransitRecyclerView.setAdapter(mTransitAdapter);
    // Clickable only for public transport; ruler routes reuse the tile without per-leg details.
    mTransitTime.setOnClickListener(v -> TransitDetailsActivity.start(mContext));
    mTransitTime.setClickable(false);
  }

  private void openSearchForRoutePick()
  {
    ((MwmActivity) mContext).showSearch("");
  }

  void setVisibilityChangedCallback(@Nullable Runnable callback)
  {
    mVisibilityChangedCallback = callback;
  }

  private void notifyVisibilityChanged()
  {
    if (mVisibilityChangedCallback != null)
      mVisibilityChangedCallback.run();
  }

  void showAltitudeChartAndRoutingDetails()
  {
    UiUtils.hide(mError, mAltitudeChart, mTimeElevationLine);

    if (!RoutingController.get().isVehicleRouterType() && !RoutingController.get().isRulerRouterType())
      showRouteAltitudeChart();
    showRoutingDetails();
    showPathGrades();
    UiUtils.show(mAltitudeChartFrame);
    notifyVisibilityChanged();
    refreshManageRoute();
  }

  void updateSaveButton()
  {
    // A saved route can still be shared as GPX.
    setSaveButtonEnabled(RoutingController.get().isBuilt());
  }

  private void setSaveButtonEnabled(boolean enabled)
  {
    mSaveButton.setEnabled(enabled);
    mSaveButton.setAlpha(enabled ? 1.0f : SAVE_BUTTON_DISABLED_ALPHA);
  }

  void refreshManageRoute()
  {
    if (mManageRouteController != null)
      mManageRouteController.refresh();
  }

  void hideAltitudeChartAndRoutingDetails()
  {
    UiUtils.hide(mAltitudeChart, mTimeVehicle, mTimeElevationLine, mTransitTime, mTimeRuler, mTransitRecyclerView,
                 mPathGrades, mSurfaceSummary, mSurfaceDetails);
    notifyVisibilityChanged();
  }

  @SuppressLint("SetTextI18n")
  void showTransitInfo(@NonNull TransitRouteInfo info)
  {
    refreshManageRoute();
    hideAltitudeChartAndRoutingDetails();
    UiUtils.hide(mError, mTimeElevationLine, mTimeVehicle);
    setStartState(StartState.DISABLED);

    // The whole tile is one tap target that opens the details: total duration, walking distance, leg strip.
    mTransitAdapter.setItems(info.getTransitSteps());
    ((TextView) mTransitTime.findViewById(R.id.total_time))
        .setText(StringUtils.nativeFormatRoutingDuration(info.getTotalTime()));
    TextView walkView = mTransitTime.findViewById(R.id.total_walk_distance);
    UiUtils.showIf(info.getTotalPedestrianTimeInSec() > 0, walkView, mTransitTime.findViewById(R.id.pedestrian_icon));
    walkView.setText(info.getTotalPedestrianDistance() + " " + info.getTotalPedestrianDistanceUnits());
    mTransitTime.setClickable(true);
    UiUtils.show(mTransitTime, mTransitRecyclerView, mTransitTime.findViewById(R.id.transit_topline));

    notifyVisibilityChanged();
  }

  @SuppressLint("SetTextI18n")
  void showRulerInfo(@NonNull RouteMarkData[] points, Distance totalLength)
  {
    refreshManageRoute();
    UiUtils.hide(mError, mTimeVehicle, mTimeElevationLine, mAltitudeChart);
    setStartState(StartState.DISABLED);
    hideAltitudeChartAndRoutingDetails();
    UiUtils.show(mAltitudeChartFrame, mTimeRuler);

    // The tile is shared with public transport routing, but a straight-line ruler route has no per-leg
    // details, so make it inert and hide the summary top line. Only the (waypoint) strip is meaningful.
    mTransitTime.setClickable(false);
    UiUtils.hide(mTransitTime.findViewById(R.id.transit_topline));

    if (points.length > 2)
    {
      UiUtils.show(mTransitTime, mTransitRecyclerView);
      mTransitAdapter.setItems(pointsToRulerSteps(points));
    }
    else
      UiUtils.hide(mTransitTime, mTransitRecyclerView); // Show only distance between start and finish
    mTimeRuler.setText(mContext.getString(R.string.placepage_distance) + ": " + totalLength.mDistanceStr + " "
                       + totalLength.getUnitsStr(mContext));
    notifyVisibilityChanged();
  }

  // Create steps info to use in TransitStepAdapter.
  private List<TransitStepInfo> pointsToRulerSteps(RouteMarkData[] points)
  {
    List<TransitStepInfo> transitSteps = new LinkedList<>();
    for (int i = 1; i < points.length; i++)
    {
      RouteMarkData segmentStart = points[i - 1];
      RouteMarkData segmentEnd = points[i];
      DistanceAndAzimut dist = Framework.nativeGetDistanceAndAzimuthFromLatLon(segmentStart.mLat, segmentStart.mLon,
                                                                               segmentEnd.mLat, segmentEnd.mLon, 0);
      if (i > 1)
        transitSteps.add(TransitStepInfo.intermediatePoint(i - 2));
      transitSteps.add(
          TransitStepInfo.ruler(dist.getDistance().mDistanceStr, dist.getDistance().getUnitsStr(mContext)));
    }

    return transitSteps;
  }

  // Single entry point for the Start button visual state. The button itself is always visible.
  void setStartState(@NonNull StartState state)
  {
    if (state == mStartState)
      return;
    mStart.setBuildProgress(0);
    mStartState = state;
    mStart.setEnabled(state == StartState.ENABLED);
    notifyVisibilityChanged();
  }

  void setBuildProgress(int progress)
  {
    mStart.setBuildProgress(progress);
  }

  // Explicit reset covers the rebuild-during-build case where setStartState(BUILDING) early-returns
  // (state unchanged) and would not zero the fill on its own.
  void resetBuildProgress()
  {
    mStart.setBuildProgress(0);
  }

  private void showError(@NonNull String message)
  {
    UiUtils.hide(mAltitudeChartFrame);
    mError.setText(message);
    mError.setVisibility(View.VISIBLE);
    setStartState(StartState.DISABLED);
    notifyVisibilityChanged();
  }

  void saveRoutingPanelState(@NonNull Bundle outState)
  {
    outState.putBoolean(STATE_ALTITUDE_CHART_SHOWN, UiUtils.isVisible(mAltitudeChartFrame));
    if (UiUtils.isVisible(mError))
      outState.putString(STATE_ERROR, mError.getText().toString());
  }

  void restoreRoutingPanelState(@NonNull Bundle state)
  {
    if (state.getBoolean(STATE_ALTITUDE_CHART_SHOWN))
    {
      if (RoutingController.get().isTransitType())
      {
        TransitRouteInfo info = RoutingController.get().getCachedTransitInfo();
        if (info != null)
          showTransitInfo(info);
      }
      else if (RoutingController.get().isRulerRouterType())
      {
        RoutingInfo routingInfo = RoutingController.get().getCachedRoutingInfo();
        if (routingInfo != null)
          showRulerInfo(Framework.nativeGetRoutePoints(), routingInfo.distToTarget);
      }
      else if (RoutingController.get().isBuilt())
      {
        setStartState(StartState.ENABLED);
        showAltitudeChartAndRoutingDetails();
      }
    }

    String error = state.getString(STATE_ERROR);
    if (!TextUtils.isEmpty(error))
      showError(error);
  }

  private void showRouteAltitudeChart()
  {
    hideAltitudeChartAndRoutingDetails();
    final RouteAltitudeData data = Framework.nativeGetRouteAltitudeData();
    if (data == null || data.getSize() == 0)
    {
      mRouteElevationChartController.clearSelection();
      Framework.nativeRouteRemoveElevationActivePoint();
      mRouteElevationChartController.setData(null);
      UiUtils.hide(mAltitudeChart, mTimeElevationLine);
      return;
    }
    mRouteElevationChartController.setData(data);
    final int formatResId =
        StringUtils.isRtl() ? R.string.route_ascent_descent_format_rtl : R.string.route_ascent_descent_format_ltr;
    final String ascent = Framework.nativeFormatAltitude(data.getTotalAscent());
    final String descent = Framework.nativeFormatAltitude(data.getTotalDescent());
    mAltitudeDifference.setText(mContext.getString(formatResId, ascent, descent));
    UiUtils.show(mAltitudeDifference, mAltitudeChart);
  }

  // Maps&Fly: shows how much of a walking or cycling route runs on SAC T3 and T4-T6 paths, and warns
  // when the route uses a grade the routing options avoid (e.g. because a stop lies on such a path).
  private void showPathGrades()
  {
    final Router router = RoutingController.get().getLastRouterType();
    final double[] grades = Framework.nativeGetRoutePathGradeDistances();
    if (grades == null || (router != Router.Pedestrian && router != Router.Bicycle))
    {
      UiUtils.hide(mPathGrades, mSurfaceSummary, mSurfaceDetails);
      return;
    }

    final SpannableStringBuilder text = new SpannableStringBuilder(mContext.getString(R.string.route_path_grades));
    final String[] labels = {mContext.getString(R.string.route_path_grade_normal),
                             mContext.getString(R.string.route_path_grade_difficult),
                             mContext.getString(R.string.route_path_grade_expert)};
    boolean first = true;
    for (int i = 0; i < grades.length; ++i)
    {
      // Shorter parts are just junction snapping noise.
      if (grades[i] < 1)
        continue;
      text.append(first ? " " : " · ").append(labels[i]);
      if (grades[1] + grades[2] >= 1)
        text.append(" ").append(StringUtils.nativeFormatDistance(grades[i]).toString(mContext));
      first = false;
    }

    final boolean expertAvoided = grades[2] >= 1 && RoutingOptions.hasOption(router, RoadType.PathExpert);
    final boolean difficultAvoided = grades[1] >= 1 && RoutingOptions.hasOption(router, RoadType.PathDifficult);
    if (expertAvoided || difficultAvoided)
    {
      final int start = text.length();
      text.append("\n").append(mContext.getString(expertAvoided ? R.string.route_path_grade_expert_warning
                                                                : R.string.route_path_grade_difficult_warning));
      text.setSpan(new ForegroundColorSpan(ContextCompat.getColor(mContext, R.color.base_red)), start, text.length(),
                   Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    }
    mPathGrades.setText(text);
    UiUtils.show(mPathGrades);
    showWayStats();
  }

  // Maps&Fly: natural vs. paved share as a bar; the info button expands way types and surfaces like Komoot.
  private void showWayStats()
  {
    final double[] stats = Framework.nativeGetRouteWayStats();
    final int surfaceOffset = WAY_TYPE_NAMES.length;
    final int naturalIndex = surfaceOffset + SURFACE_NAMES.length;
    double total = 0;
    if (stats != null && stats.length == naturalIndex + 1)
      for (int i = 0; i < surfaceOffset; ++i)
        total += stats[i];
    if (total < 1)
    {
      UiUtils.hide(mSurfaceSummary, mSurfaceDetails);
      return;
    }

    final double natural = Math.min(stats[naturalIndex], total);
    final StackedBarView bar = mSurfaceSummary.findViewById(R.id.route_surface_bar);
    bar.setParts(new double[] {natural, total - natural},
                 new int[] {ContextCompat.getColor(mContext, R.color.route_natural),
                            ContextCompat.getColor(mContext, R.color.route_paved)});
    final TextView share = mSurfaceSummary.findViewById(R.id.route_surface_share);
    share.setText(
        mContext.getString(R.string.route_natural_share, NumberFormat.getPercentInstance().format(natural / total)));

    showShares(R.id.route_way_types_bar, R.id.route_way_types_list, stats, 0, WAY_TYPE_NAMES, WAY_TYPE_COLORS);
    showShares(R.id.route_surfaces_bar, R.id.route_surfaces_list, stats, surfaceOffset, SURFACE_NAMES, SURFACE_COLORS);
    UiUtils.show(mSurfaceSummary);
    UiUtils.showIf(mSurfaceDetailsShown, mSurfaceDetails);
  }

  // Longest first, as a bar and a compact legend "■ Name 1,2 km".
  private void showShares(@IdRes int barId, @IdRes int listId, @NonNull double[] values, int offset,
                          @NonNull int[] names, @NonNull int[] colors)
  {
    final List<Integer> order = new ArrayList<>();
    for (int i = 0; i < names.length; ++i)
      if (values[offset + i] >= 1)
        order.add(i);
    Collections.sort(order, (a, b) -> Double.compare(values[offset + b], values[offset + a]));

    final double[] parts = new double[order.size()];
    final int[] partColors = new int[order.size()];
    final SpannableStringBuilder legend = new SpannableStringBuilder();
    for (int k = 0; k < order.size(); ++k)
    {
      final int i = order.get(k);
      parts[k] = values[offset + i];
      partColors[k] = ContextCompat.getColor(mContext, colors[i]);
      if (k > 0)
        legend.append("   ");
      final int start = legend.length();
      legend.append("■");
      legend.setSpan(new ForegroundColorSpan(partColors[k]), start, legend.length(),
                     Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
      legend.append("\u00A0")
          .append(mContext.getString(names[i]))
          .append("\u00A0")
          .append(StringUtils.nativeFormatDistance(parts[k]).toString(mContext));
    }
    final StackedBarView bar = mSurfaceDetails.findViewById(barId);
    bar.setParts(parts, partColors);
    final TextView list = mSurfaceDetails.findViewById(listId);
    list.setText(legend);
  }

  private void showRoutingDetails()
  {
    final RoutingInfo rinfo = RoutingController.get().getCachedRoutingInfo();
    if (rinfo == null)
    {
      UiUtils.hide(mTimeElevationLine, mTimeVehicle);
      return;
    }

    Spanned spanned = makeSpannedRoutingDetails(mContext, rinfo);
    if (RoutingController.get().isVehicleRouterType())
    {
      hideAltitudeChartAndRoutingDetails();
      UiUtils.show(mTimeVehicle);
      mTimeVehicle.setText(spanned);
    }
    else
    {
      UiUtils.show(mTimeElevationLine);
      mTime.setText(spanned);
    }

    if (mArrival != null)
    {
      String arrivalTime = Utils.formatArrivalTime(mContext, rinfo.totalTimeInSeconds);
      mArrival.setText(arrivalTime);
    }
  }

  @NonNull
  private static Spanned makeSpannedRoutingDetails(@NonNull Context context, @NonNull RoutingInfo routingInfo)

  {
    String time = StringUtils.nativeFormatRoutingDuration(routingInfo.totalTimeInSeconds);

    SpannableStringBuilder builder = new SpannableStringBuilder();
    initTimeBuilderSequence(context, time, builder);

    String dot = "\u00A0• ";
    initDotBuilderSequence(context, dot, builder);

    initDistanceBuilderSequence(context, routingInfo.distToTarget.toString(context), builder);

    return builder;
  }

  private static void initTimeBuilderSequence(@NonNull Context context, @NonNull CharSequence time,
                                              @NonNull SpannableStringBuilder builder)
  {
    builder.append(time);

    builder.setSpan(new TypefaceSpan(context.getResources().getString(R.string.robotoMedium)), 0, builder.length(),
                    Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(
        new AbsoluteSizeSpan(context.getResources().getDimensionPixelSize(R.dimen.text_size_routing_number)), 0,
        builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(new StyleSpan(Typeface.BOLD), 0, builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(new ForegroundColorSpan(ThemeUtils.getColor(context, android.R.attr.textColorPrimary)), 0,
                    builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
  }

  private static void initDotBuilderSequence(@NonNull Context context, @NonNull String dot,
                                             @NonNull SpannableStringBuilder builder)
  {
    builder.append(dot);
    builder.setSpan(new TypefaceSpan(context.getResources().getString(R.string.robotoMedium)),
                    builder.length() - dot.length(), builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(
        new AbsoluteSizeSpan(context.getResources().getDimensionPixelSize(R.dimen.text_size_routing_number)),
        builder.length() - dot.length(), builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(new ForegroundColorSpan(ThemeUtils.getColor(context, R.attr.secondary)),
                    builder.length() - dot.length(), builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
  }

  private static void initDistanceBuilderSequence(@NonNull Context context, @NonNull String arrivalTime,
                                                  @NonNull SpannableStringBuilder builder)
  {
    builder.append(arrivalTime);
    builder.setSpan(new TypefaceSpan(context.getResources().getString(R.string.robotoMedium)),
                    builder.length() - arrivalTime.length(), builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(
        new AbsoluteSizeSpan(context.getResources().getDimensionPixelSize(R.dimen.text_size_routing_number)),
        builder.length() - arrivalTime.length(), builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(new StyleSpan(Typeface.NORMAL), builder.length() - arrivalTime.length(), builder.length(),
                    Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
    builder.setSpan(new ForegroundColorSpan(ThemeUtils.getColor(context, android.R.attr.textColorPrimary)),
                    builder.length() - arrivalTime.length(), builder.length(), Spannable.SPAN_EXCLUSIVE_EXCLUSIVE);
  }

  // Maps&Fly: offers to save the route as a track and to share it as GPX in one place.
  private void showSaveMenu()
  {
    if (!RoutingController.get().isBuilt())
      return;
    final boolean saved = RoutingController.get().isRouteSaved();
    final CharSequence[] items = saved ? new CharSequence[] {mContext.getString(R.string.route_share_gpx)}
                                       : new CharSequence[] {mContext.getString(R.string.route_save_as_track),
                                                             mContext.getString(R.string.route_share_gpx)};
    new MaterialAlertDialogBuilder(mContext, R.style.MwmTheme_AlertDialog)
        .setTitle(R.string.route_save_title)
        .setItems(items,
                  (dialog, which) -> {
                    if (!saved && which == 0)
                      saveRoute();
                    else
                      shareRouteAsGpx();
                  })
        .show();
  }

  private void shareRouteAsGpx()
  {
    if (!RoutingController.get().isRouteSaved() && !saveRoute())
      return;
    final MwmActivity activity = (MwmActivity) mContext;
    BookmarksSharingHelper.INSTANCE.prepareTrackForSharing(
        activity, activity.getShareLauncher(), RoutingController.get().getSavedRouteTrackId(), FileType.Gpx);
  }

  private boolean saveRoute()
  {
    if (!RoutingController.get().isBuilt())
      return false;
    // Clear elevation preview marker before saving.
    mRouteElevationChartController.clearSelection();
    Framework.nativeRouteRemoveElevationActivePoint();
    final long trackId = Framework.nativeSaveRoute();
    if (trackId == -1)
    {
      Toast.makeText(mContext, R.string.routing_save_route_error, Toast.LENGTH_SHORT).show();
      return false;
    }
    RoutingController.get().setRouteSaved(trackId);
    return true;
  }
}
