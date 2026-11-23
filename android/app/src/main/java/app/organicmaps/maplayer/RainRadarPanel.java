package app.organicmaps.maplayer;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.text.format.DateFormat;
import android.view.View;
import android.widget.TextView;
import androidx.annotation.NonNull;
import androidx.lifecycle.LifecycleOwner;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
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
  private final TextView mStatus;
  private final Handler mHandler = new Handler(Looper.getMainLooper());
  private final Runnable mStatusUpdater = new Runnable() {
    @Override
    public void run()
    {
      showStatus();
      if (UiUtils.isVisible(mPanel))
        mHandler.postDelayed(this, 1000);
    }
  };
  @NonNull
  private RainRadar.State mState;

  public RainRadarPanel(@NonNull View panel, @NonNull LifecycleOwner owner)
  {
    mPanel = panel;
    mTime = panel.findViewById(R.id.rain_radar_time);
    mCredit = panel.findViewById(R.id.rain_radar_credit);
    mSlider = panel.findViewById(R.id.rain_radar_slider);
    mStatus = panel.findViewById(R.id.rain_radar_status);
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
    mHandler.removeCallbacks(mStatusUpdater);
    if (!state.mEnabled)
      return;
    mStatusUpdater.run();

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

  private void showStatus()
  {
    if (mState.mIndex < 0)
    {
      mStatus.setText("");
      return;
    }
    final int[] stats = Framework.nativeGetTileOverlayStats();
    final Context context = mPanel.getContext();
    String text = context.getString(R.string.rain_radar_status, stats[0], stats[1]);
    if (stats[1] > 0)
    {
      final String error = switch (stats[2])
      {
        case -1 -> context.getString(R.string.rain_radar_error_network);
        case -2 -> context.getString(R.string.rain_radar_error_image);
        default -> "HTTP " + stats[2];
      };
      text += " (" + error + ")";
    }
    mStatus.setText(text);
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
