package app.organicmaps.maplayer;

import android.text.format.DateFormat;
import android.view.View;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.lifecycle.LifecycleOwner;
import app.organicmaps.R;
import app.organicmaps.sdk.maplayer.radar.RainRadar;
import app.organicmaps.util.UiUtils;
import com.google.android.material.slider.Slider;
import java.util.Date;
import java.util.concurrent.TimeUnit;

/** Maps&Fly B3: the rain radar time slider on the map. */
public class RainRadarPanel
{
  @NonNull
  private final View mPanel;
  @NonNull
  private final TextView mTime;
  @NonNull
  private final TextView mCredit;
  @NonNull
  private final Slider mSlider;
  @NonNull
  private RainRadar.State mState;

  public RainRadarPanel(@NonNull View panel, @NonNull LifecycleOwner owner)
  {
    mPanel = panel;
    mTime = panel.findViewById(R.id.rain_radar_time);
    mCredit = panel.findViewById(R.id.rain_radar_credit);
    mSlider = panel.findViewById(R.id.rain_radar_slider);
    mState = RainRadar.getState().getValue();

    // The label follows the finger; the frame is loaded on release, every frame costs downloads.
    mSlider.addOnChangeListener((slider, value, fromUser) -> {
      if (fromUser)
        showTime((int) value);
    });
    mSlider.addOnSliderTouchListener(new Slider.OnSliderTouchListener() {
      @Override
      public void onStartTrackingTouch(@NonNull Slider slider)
      {}

      @Override
      public void onStopTrackingTouch(@NonNull Slider slider)
      {
        RainRadar.selectFrame(slider.getContext(), (int) slider.getValue());
      }
    });

    RainRadar.getState().observe(owner, this::update);
  }

  @NonNull
  public View getView()
  {
    return mPanel;
  }

  private void update(@NonNull RainRadar.State state)
  {
    mState = state;
    UiUtils.showIf(state.mEnabled, mPanel);
    if (!state.mEnabled)
      return;

    mCredit.setText(RainRadar.SOURCE_RAINVIEWER.equals(state.mSource) ? R.string.rain_radar_credit_rainviewer
                                                                       : R.string.rain_radar_credit_dwd);
    final boolean ready = state.mIndex >= 0;
    UiUtils.showIf(ready, mSlider);
    if (!ready)
    {
      mTime.setText(state.mFailed ? R.string.rain_radar_failed : R.string.rain_radar_loading);
      return;
    }

    final int last = state.mFrames.size() - 1;
    if (last == 0)
    {
      UiUtils.hide(mSlider);
    }
    else
    {
      // Slider throws when the value is outside the range: set the range first.
      mSlider.setValueFrom(0);
      mSlider.setValueTo(last);
      mSlider.setValue(state.mIndex);
    }
    showTime(state.mIndex);
  }

  private void showTime(int index)
  {
    if (index < 0 || index >= mState.mFrames.size())
      return;
    final RainRadar.Frame frame = mState.mFrames.get(index);
    final RainRadar.Frame now = mState.mFrames.get(mState.mNowIndex);
    final String clock = DateFormat.getTimeFormat(mPanel.getContext()).format(new Date(frame.mTimeMs));
    final long minutes = TimeUnit.MILLISECONDS.toMinutes(Math.abs(frame.mTimeMs - now.mTimeMs));
    final String text;
    if (index == mState.mNowIndex)
      text = mPanel.getContext().getString(R.string.rain_radar_latest, clock);
    else if (frame.mForecast)
      text = mPanel.getContext().getString(R.string.rain_radar_forecast, clock, (int) minutes);
    else
      text = mPanel.getContext().getString(R.string.rain_radar_past, clock, (int) minutes);
    mTime.setText(text);
  }
}
