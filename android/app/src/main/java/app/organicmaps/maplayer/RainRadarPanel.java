package app.organicmaps.maplayer;

import android.content.Context;
import android.content.res.TypedArray;
import android.os.Handler;
import android.os.Looper;
import android.text.format.DateFormat;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.TextView;
import androidx.annotation.AttrRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import androidx.core.widget.TextViewCompat;
import androidx.lifecycle.LifecycleOwner;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.maplayer.radar.RadarLegend;
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
  @NonNull
  private final ImageButton mPlay;
  @NonNull
  private final LinearLayout mLegend;
  // The source the legend shows, null while it is closed.
  @Nullable
  private String mLegendSource;
  private boolean mPlaying;
  // Steps through the frames, a pause on the last one, then again from the first.
  private final Runnable mPlayer = new Runnable() {
    @Override
    public void run()
    {
      if (!mPlaying || mState.mIndex < 0)
        return;
      final int last = mState.mFrames.size() - 1;
      final int next = mState.mIndex >= last ? 0 : mState.mIndex + 1;
      RainRadar.selectFrame(mPanel.getContext(), next);
      mHandler.postDelayed(this, next == last ? PLAY_END_PAUSE_MS : PLAY_STEP_MS);
    }
  };
  private static final long PLAY_STEP_MS = 800;
  private static final long PLAY_END_PAUSE_MS = 2000;
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

  public RainRadarPanel(@NonNull View panel, @NonNull LifecycleOwner owner, @NonNull Runnable onClosed)
  {
    mPanel = panel;
    mTime = panel.findViewById(R.id.rain_radar_time);
    mCredit = panel.findViewById(R.id.rain_radar_credit);
    mSlider = panel.findViewById(R.id.rain_radar_slider);
    mStatus = panel.findViewById(R.id.rain_radar_status);
    mPlay = panel.findViewById(R.id.rain_radar_play);
    mPlay.setOnClickListener(v -> setPlaying(!mPlaying));
    mLegend = panel.findViewById(R.id.rain_radar_legend);
    panel.findViewById(R.id.rain_radar_legend_button)
        .setOnClickListener(v -> showLegend(mLegendSource == null ? mState.mSource : null));
    mState = RainRadar.getState().getValue();
    panel.findViewById(R.id.rain_radar_close).setOnClickListener(v -> {
      setPlaying(false);
      RainRadar.setEnabled(v.getContext(), false);
      onClosed.run();
    });

    // The label follows the finger; the frame is loaded on release, every frame costs downloads.
    mSlider.addOnChangeListener((slider, value, fromUser) -> {
      if (fromUser)
        showTime((int) value);
    });
    mSlider.addOnSliderTouchListener(new Slider.OnSliderTouchListener() {
      @Override
      public void onStartTrackingTouch(@NonNull Slider slider)
      {
        setPlaying(false);
      }

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
    if (!state.mEnabled || state.mIndex < 0)
      setPlaying(false);
    if (!state.mEnabled)
    {
      showLegend(null);
      return;
    }
    mStatusUpdater.run();
    if (mLegendSource != null && !mLegendSource.equals(state.mSource))
      showLegend(state.mSource);

    mCredit.setText(RainRadar.SOURCE_RAINVIEWER.equals(state.mSource) ? R.string.rain_radar_credit_rainviewer
                                                                      : R.string.rain_radar_credit_dwd);
    final boolean ready = state.mIndex >= 0;
    UiUtils.showIf(ready, mSlider);
    UiUtils.showIf(ready && state.mFrames.size() > 1, mPlay);
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

  /** Opens the colour scale of |source|, or closes it for null. */
  private void showLegend(@Nullable String source)
  {
    mLegendSource = source;
    mLegend.removeAllViews();
    UiUtils.showIf(source != null, mLegend);
    if (source == null)
      return;

    if (RainRadar.SOURCE_RAINVIEWER.equals(source))
    {
      addScale(R.string.rain_radar_legend_rain, RadarLegend.RAINVIEWER_RAIN,
               getString(R.string.rain_radar_legend_light), getString(R.string.rain_radar_legend_heavy_hail));
      addScale(R.string.rain_radar_legend_snow, RadarLegend.RAINVIEWER_SNOW,
               getString(R.string.rain_radar_legend_little), getString(R.string.rain_radar_legend_much));
      return;
    }

    addText(R.string.rain_radar_loading);
    RadarLegend.getDwdLegend(entries -> {
      if (!source.equals(mLegendSource))
        return;
      mLegend.removeAllViews();
      if (entries.isEmpty())
      {
        addText(R.string.rain_radar_legend_unavailable);
        return;
      }
      final int[] colors = new int[entries.size()];
      for (int i = 0; i < colors.length; ++i)
        colors[i] = entries.get(i).mColor;
      addScale(R.string.rain_radar_legend_precipitation, colors, entries.get(0).mLabel,
               entries.get(entries.size() - 1).mLabel);
    });
  }

  private void addScale(@StringRes int title, @NonNull int[] colors, @NonNull String low, @NonNull String high)
  {
    final Context context = mPanel.getContext();
    final float density = context.getResources().getDisplayMetrics().density;

    final TextView titleView = createText(context.getString(title), android.R.attr.textColorPrimary);
    titleView.setPadding(0, (int) (4 * density), 0, (int) (2 * density));
    mLegend.addView(titleView);

    final LinearLayout bar = new LinearLayout(context);
    bar.setOrientation(LinearLayout.HORIZONTAL);
    for (int color : colors)
    {
      final View cell = new View(context);
      cell.setBackgroundColor(color);
      bar.addView(cell, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.MATCH_PARENT, 1));
    }
    mLegend.addView(bar, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, (int) (10 * density)));

    final LinearLayout labels = new LinearLayout(context);
    labels.setOrientation(LinearLayout.HORIZONTAL);
    labels.addView(createText(low, android.R.attr.textColorSecondary),
                   new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
    final TextView highView = createText(high, android.R.attr.textColorSecondary);
    highView.setGravity(Gravity.END);
    labels.addView(highView, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
    mLegend.addView(labels);
  }

  private void addText(@StringRes int text)
  {
    mLegend.addView(createText(getString(text), android.R.attr.textColorSecondary));
  }

  @NonNull
  private TextView createText(@NonNull String text, @AttrRes int color)
  {
    final TextView view = new TextView(mPanel.getContext());
    TextViewCompat.setTextAppearance(view, R.style.MwmTextAppearance_Body4);
    final TypedArray attrs = mPanel.getContext().obtainStyledAttributes(new int[] {color});
    view.setTextColor(attrs.getColorStateList(0));
    attrs.recycle();
    view.setText(text);
    return view;
  }

  @NonNull
  private String getString(@StringRes int text)
  {
    return mPanel.getContext().getString(text);
  }

  private void setPlaying(boolean playing)
  {
    mPlaying = playing;
    mHandler.removeCallbacks(mPlayer);
    mPlay.setImageResource(playing ? R.drawable.ic_pause : R.drawable.ic_play);
    mPlay.setContentDescription(
        mPlay.getContext().getString(playing ? R.string.rain_radar_pause : R.string.rain_radar_play));
    if (playing)
      mPlayer.run();
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
