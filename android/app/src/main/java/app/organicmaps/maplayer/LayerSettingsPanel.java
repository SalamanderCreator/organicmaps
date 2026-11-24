package app.organicmaps.maplayer;

import android.content.Context;
import android.view.View;
import android.widget.CompoundButton;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.appcompat.widget.SwitchCompat;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.maplayer.DistanceLayer;
import app.organicmaps.sdk.maplayer.Mode;
import app.organicmaps.sdk.maplayer.SatelliteSource;
import app.organicmaps.sdk.maplayer.radar.RainRadar;
import app.organicmaps.util.UiUtils;
import com.google.android.material.button.MaterialButtonToggleGroup;
import com.google.android.material.slider.Slider;

/**
 * Maps&Fly B3/B4: settings of one map layer, opened with a long press in the layers menu. A small
 * card at the bottom of the map, so the effect of every slider is visible at once.
 */
public class LayerSettingsPanel
{
  private static final int[] LIGHT_DIRECTIONS = {
      R.string.hillshade_light_n, R.string.hillshade_light_ne, R.string.hillshade_light_e, R.string.hillshade_light_se,
      R.string.hillshade_light_s, R.string.hillshade_light_sw, R.string.hillshade_light_w, R.string.hillshade_light_nw};

  @NonNull
  private final View mPanel;
  @NonNull
  private final Context mContext;
  @NonNull
  private final TextView mTitle;
  @NonNull
  private final View mRadar;
  @NonNull
  private final View mSatellite;
  @NonNull
  private final View mTerrain;
  @NonNull
  private final View mDistance;
  @NonNull
  private final Runnable mOnLayerChanged;
  // Set while the controls are filled in, so their listeners do not apply the values back.
  private boolean mBinding;

  public static boolean hasSettings(@NonNull Mode mode)
  {
    return mode == Mode.RAIN_RADAR || mode == Mode.SATELLITE || mode == Mode.ISOLINES || mode == Mode.DISTANCE;
  }

  /** @param onLayerChanged called when a setting switched a layer on, to refresh the layers button. */
  public LayerSettingsPanel(@NonNull View panel, @NonNull Runnable onLayerChanged)
  {
    mPanel = panel;
    mContext = panel.getContext();
    mOnLayerChanged = onLayerChanged;
    mTitle = panel.findViewById(R.id.layer_settings_title);
    mRadar = panel.findViewById(R.id.layer_settings_radar);
    mSatellite = panel.findViewById(R.id.layer_settings_satellite);
    mTerrain = panel.findViewById(R.id.layer_settings_terrain);
    mDistance = panel.findViewById(R.id.layer_settings_distance);
    panel.findViewById(R.id.layer_settings_close).setOnClickListener(v -> hide());
    initRadar();
    initSatellite();
    initTerrain();
    initDistance();
  }

  public void show(@NonNull Mode mode)
  {
    mBinding = true;
    UiUtils.showIf(mode == Mode.RAIN_RADAR, mRadar);
    UiUtils.showIf(mode == Mode.SATELLITE, mSatellite);
    UiUtils.showIf(mode == Mode.ISOLINES, mTerrain);
    UiUtils.showIf(mode == Mode.DISTANCE, mDistance);
    switch (mode)
    {
    case RAIN_RADAR ->
    {
      mTitle.setText(R.string.button_layer_rain_radar);
      bindRadar();
    }
    case SATELLITE ->
    {
      mTitle.setText(R.string.button_layer_satellite);
      bindSatellite();
    }
    case ISOLINES ->
    {
      mTitle.setText(R.string.button_layer_terrain);
      bindTerrain();
    }
    case DISTANCE ->
    {
      mTitle.setText(R.string.button_layer_distance);
      bindDistance();
    }
    default ->
    {
    }
    }
    mBinding = false;
    UiUtils.show(mPanel);
  }

  public void hide()
  {
    UiUtils.hide(mPanel);
  }

  public boolean isShowing()
  {
    return UiUtils.isVisible(mPanel);
  }

  private void initRadar()
  {
    final MaterialButtonToggleGroup source = mPanel.findViewById(R.id.radar_source);
    source.addOnButtonCheckedListener((group, checkedId, isChecked) -> {
      if (!isChecked)
        return;
      final String value =
          checkedId == R.id.radar_source_rainviewer ? RainRadar.SOURCE_RAINVIEWER : RainRadar.SOURCE_DWD;
      showRadarNote(value);
      if (mBinding)
        return;
      RainRadar.setSource(mContext, value);
      enableLayer(Mode.RAIN_RADAR);
    });
    final Slider opacity = mPanel.findViewById(R.id.radar_opacity);
    bindPercent(opacity, R.id.radar_opacity_value);
    // On release: every change re-reads the visible tiles.
    onRelease(opacity, value -> {
      RainRadar.setOpacity(mContext, value);
      enableLayer(Mode.RAIN_RADAR);
    });
  }

  private void bindRadar()
  {
    final String source = RainRadar.getSource(mContext);
    ((MaterialButtonToggleGroup) mPanel.findViewById(R.id.radar_source))
        .check(RainRadar.SOURCE_RAINVIEWER.equals(source) ? R.id.radar_source_rainviewer : R.id.radar_source_dwd);
    showRadarNote(source);
    setSlider(mPanel.findViewById(R.id.radar_opacity), RainRadar.getOpacity(mContext));
  }

  private void showRadarNote(@NonNull String source)
  {
    ((TextView) mPanel.findViewById(R.id.radar_source_note))
        .setText(RainRadar.SOURCE_RAINVIEWER.equals(source) ? R.string.rain_radar_source_rainviewer
                                                            : R.string.rain_radar_source_dwd);
  }

  private void initSatellite()
  {
    final MaterialButtonToggleGroup source = mPanel.findViewById(R.id.satellite_source);
    source.addOnButtonCheckedListener((group, checkedId, isChecked) -> {
      if (!isChecked || mBinding)
        return;
      (checkedId == R.id.satellite_source_sentinel ? SatelliteSource.SENTINEL : SatelliteSource.ESRI).apply(true);
      mOnLayerChanged.run();
    });
    final Slider areaOpacity = mPanel.findViewById(R.id.satellite_area_opacity);
    bindPercent(areaOpacity, R.id.satellite_area_opacity_value);
    onRelease(areaOpacity, value -> {
      final SatelliteSource current = SatelliteSource.current();
      (current == null ? SatelliteSource.ESRI : current).applyWithAreaOpacity(true, value);
      mOnLayerChanged.run();
    });
  }

  private void bindSatellite()
  {
    final SatelliteSource current = SatelliteSource.current();
    final MaterialButtonToggleGroup source = mPanel.findViewById(R.id.satellite_source);
    if (current == SatelliteSource.SENTINEL)
      source.check(R.id.satellite_source_sentinel);
    else if (current == SatelliteSource.ESRI || Framework.nativeGetBackgroundTilesUrl().isEmpty())
      source.check(R.id.satellite_source_esri);
    else
      source.clearChecked(); // a custom URL from the settings
    setSlider(mPanel.findViewById(R.id.satellite_area_opacity), Framework.nativeGetBackgroundTilesAreaOpacity());
  }

  private void initTerrain()
  {
    final SwitchCompat contours = mPanel.findViewById(R.id.terrain_contour_lines);
    contours.setOnCheckedChangeListener((button, isChecked) -> {
      if (mBinding)
        return;
      Framework.nativeSetContourLinesEnabled(isChecked);
      enableLayer(Mode.ISOLINES);
    });

    final Slider strength = mPanel.findViewById(R.id.terrain_strength);
    final Slider light = mPanel.findViewById(R.id.terrain_light);
    final Slider altitude = mPanel.findViewById(R.id.terrain_altitude);
    bindPercent(strength, R.id.terrain_strength_value);
    final TextView lightValue = mPanel.findViewById(R.id.terrain_light_value);
    light.addOnChangeListener((s, value, fromUser) -> lightValue.setText(LIGHT_DIRECTIONS[(int) value]));
    final TextView altitudeValue = mPanel.findViewById(R.id.terrain_altitude_value);
    altitude.addOnChangeListener((s, value, fromUser) -> altitudeValue.setText((int) value + "°"));

    // The relief light is a per-frame value: apply while dragging.
    final Slider.OnChangeListener apply = (s, value, fromUser) ->
    {
      if (!fromUser)
        return;
      Framework.nativeSetTerrainShading(light.getValue() * 45, altitude.getValue(), strength.getValue() / 100.0);
      enableLayer(Mode.ISOLINES);
    };
    strength.addOnChangeListener(apply);
    light.addOnChangeListener(apply);
    altitude.addOnChangeListener(apply);
  }

  private void bindTerrain()
  {
    ((SwitchCompat) mPanel.findViewById(R.id.terrain_contour_lines))
        .setChecked(Framework.nativeIsContourLinesEnabled());
    final double[] shading = Framework.nativeGetTerrainShading();
    setSlider(mPanel.findViewById(R.id.terrain_strength), (int) Math.round(shading[2] * 100));
    setSlider(mPanel.findViewById(R.id.terrain_light), (int) (Math.round(shading[0] / 45.0) % 8));
    setSlider(mPanel.findViewById(R.id.terrain_altitude), (int) Math.round(shading[1]));
  }

  private void initDistance()
  {
    final SwitchCompat circles = mPanel.findViewById(R.id.distance_circles);
    final SwitchCompat line = mPanel.findViewById(R.id.distance_line);
    final CompoundButton.OnCheckedChangeListener listener = (button, isChecked) ->
    {
      if (mBinding)
        return;
      // At least one part stays on: switching off the last one keeps it.
      if (!circles.isChecked() && !line.isChecked())
      {
        mBinding = true;
        button.setChecked(true);
        mBinding = false;
        return;
      }
      DistanceLayer.setParts(mContext, circles.isChecked(), line.isChecked());
      mOnLayerChanged.run();
    };
    circles.setOnCheckedChangeListener(listener);
    line.setOnCheckedChangeListener(listener);
  }

  private void bindDistance()
  {
    ((SwitchCompat) mPanel.findViewById(R.id.distance_circles)).setChecked(DistanceLayer.hasCircles(mContext));
    ((SwitchCompat) mPanel.findViewById(R.id.distance_line)).setChecked(DistanceLayer.hasLine(mContext));
  }

  /** Changing a setting of a switched off layer shows it, otherwise nothing would change. */
  private void enableLayer(@NonNull Mode mode)
  {
    if (mode.isEnabled(mContext))
      return;
    mode.setEnabled(mContext, true);
    mOnLayerChanged.run();
  }

  private interface IntConsumer
  {
    void accept(int value);
  }

  private static void onRelease(@NonNull Slider slider, @NonNull IntConsumer consumer)
  {
    slider.addOnSliderTouchListener(new Slider.OnSliderTouchListener() {
      @Override
      public void onStartTrackingTouch(@NonNull Slider s)
      {}

      @Override
      public void onStopTrackingTouch(@NonNull Slider s)
      {
        consumer.accept((int) s.getValue());
      }
    });
  }

  private void bindPercent(@NonNull Slider slider, int valueId)
  {
    final TextView label = mPanel.findViewById(valueId);
    slider.addOnChangeListener((s, value, fromUser) -> label.setText((int) value + " %"));
  }

  private static void setSlider(@NonNull Slider slider, int value)
  {
    final float step = slider.getStepSize();
    float v = Math.max(slider.getValueFrom(), Math.min(slider.getValueTo(), value));
    // Slider throws for a value off its step grid.
    if (step > 0)
      v = slider.getValueFrom() + Math.round((v - slider.getValueFrom()) / step) * step;
    slider.setValue(v);
  }
}
