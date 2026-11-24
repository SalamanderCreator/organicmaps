package app.organicmaps.widget.menu;

import android.view.View;
import android.widget.Toast;
import androidx.annotation.DrawableRes;
import androidx.annotation.NonNull;
import androidx.annotation.StringRes;
import androidx.core.widget.ImageViewCompat;
import app.organicmaps.R;
import app.organicmaps.sdk.Framework;
import app.organicmaps.sdk.Map;
import app.organicmaps.sdk.location.LocationState;
import com.google.android.material.floatingactionbutton.FloatingActionButton;

/**
 * Maps&Fly: the north arrow map button. A tap turns the map between north up and heading up, a long press locks
 * north up (also in routing, and two fingers no longer turn the map) and a second long press unlocks it.
 */
public class CompassButton implements Framework.MapAzimuthListener
{
  @NonNull
  private final FloatingActionButton mButton;
  private boolean mLocked;
  private int mMode;

  public CompassButton(@NonNull View button)
  {
    mButton = (FloatingActionButton) button;
    // The arrow keeps its red and grey halves.
    ImageViewCompat.setImageTintList(mButton, null);
    mLocked = Framework.nativeIsNorthLocked();
    mMode = Map.isEngineCreated() ? LocationState.getMode() : LocationState.NOT_FOLLOW_NO_POSITION;
    mButton.setOnClickListener(v -> {
      if (mLocked)
        showToast(R.string.compass_north_locked_hint);
      else
        Framework.nativeToggleNorthUp();
    });
    mButton.setOnLongClickListener(v -> {
      mLocked = !mLocked;
      Framework.nativeSetNorthLocked(mLocked);
      showToast(mLocked ? R.string.compass_north_locked : R.string.compass_north_unlocked);
      updateIcon();
      return true;
    });
    updateIcon();
  }

  public void updateMode(int mode)
  {
    mMode = mode;
    updateIcon();
  }

  @Override
  public void onMapAzimuthChanged(double azimuthDeg)
  {
    mButton.setRotation(mLocked ? 0 : (float) azimuthDeg);
  }

  private void updateIcon()
  {
    @DrawableRes
    final int icon;
    if (mLocked)
      icon = R.drawable.ic_compass_locked;
    else if (mMode == LocationState.FOLLOW_AND_ROTATE)
      icon = R.drawable.ic_compass_heading;
    else
      icon = R.drawable.ic_compass_north;
    mButton.setImageResource(icon);
    if (mLocked)
      mButton.setRotation(0);
  }

  private void showToast(@StringRes int text)
  {
    Toast.makeText(mButton.getContext(), text, Toast.LENGTH_SHORT).show();
  }
}
