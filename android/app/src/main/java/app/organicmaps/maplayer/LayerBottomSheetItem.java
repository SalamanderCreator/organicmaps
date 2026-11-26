package app.organicmaps.maplayer;

import android.content.Context;
import android.view.View;
import androidx.annotation.AttrRes;
import androidx.annotation.DrawableRes;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.StringRes;
import app.organicmaps.R;
import app.organicmaps.adapter.OnItemClickListener;
import app.organicmaps.sdk.maplayer.Mode;
import app.organicmaps.util.ThemeUtils;

public class LayerBottomSheetItem
{
  @DrawableRes
  private final int mDrawableResId;
  @StringRes
  private final int mTitleResId;
  @NonNull
  private final Mode mMode;
  @NonNull
  private final OnItemClickListener<LayerBottomSheetItem> mItemClickListener;
  @Nullable
  private OnItemClickListener<LayerBottomSheetItem> mLongClickListener;

  LayerBottomSheetItem(@DrawableRes int drawableResId, @StringRes int titleResId, @NonNull Mode mode,
                       @NonNull OnItemClickListener<LayerBottomSheetItem> itemClickListener)
  {
    mDrawableResId = drawableResId;
    mTitleResId = titleResId;
    mMode = mode;
    mItemClickListener = itemClickListener;
  }

  @Nullable
  OnItemClickListener<LayerBottomSheetItem> getLongClickListener()
  {
    return mLongClickListener;
  }

  void setLongClickListener(@Nullable OnItemClickListener<LayerBottomSheetItem> listener)
  {
    mLongClickListener = listener;
  }

  public static LayerBottomSheetItem create(@NonNull Context mContext, Mode mode,
                                            @NonNull OnItemClickListener<LayerBottomSheetItem> layerItemClickListener)
  {
    @DrawableRes
    int drawableResId = 0;
    @StringRes
    int buttonTextResource = R.string.layers_title;
    switch (mode)
    {
    case OUTDOORS:
      drawableResId = R.drawable.ic_layers_outdoors;
      buttonTextResource = R.string.button_layer_outdoor;
      break;
    case SUBWAY:
      drawableResId = R.drawable.ic_layers_subway;
      buttonTextResource = R.string.button_layer_subway;
      break;
    case ISOLINES:
      drawableResId = R.drawable.ic_layers_isoline;
      buttonTextResource = R.string.button_layer_terrain;
      break;
    case TRAFFIC:
      drawableResId = R.drawable.ic_layers_traffic;
      buttonTextResource = R.string.button_layer_traffic;
      break;
    case HIKING:
      drawableResId = R.drawable.ic_layers_hiking;
      buttonTextResource = R.string.button_layer_hiking;
      break;
    case CYCLING:
      drawableResId = R.drawable.ic_layers_cycling;
      buttonTextResource = R.string.button_layer_cycling;
      break;
    case DISTANCE:
      drawableResId = R.drawable.ic_layers_distance_circles;
      buttonTextResource = R.string.button_layer_distance;
      break;
    case FLIGHT:
      drawableResId = R.drawable.ic_layers_flight;
      buttonTextResource = R.string.button_layer_flight;
      break;
    case SATELLITE:
      drawableResId = R.drawable.ic_layers_satellite;
      buttonTextResource = R.string.button_layer_satellite;
      break;
    case RAIN_RADAR:
      drawableResId = R.drawable.ic_layers_rain_radar;
      buttonTextResource = R.string.button_layer_rain_radar;
      break;
    }
    return new LayerBottomSheetItem(drawableResId, buttonTextResource, mode, layerItemClickListener);
  }

  @NonNull
  public Mode getMode()
  {
    return mMode;
  }

  @DrawableRes
  public int getDrawable()
  {
    return mDrawableResId;
  }

  @StringRes
  public int getTitle()
  {
    return mTitleResId;
  }

  public void onClick(@NonNull View v, @NonNull LayerBottomSheetItem item)
  {
    mItemClickListener.onItemClick(v, item);
  }
}
