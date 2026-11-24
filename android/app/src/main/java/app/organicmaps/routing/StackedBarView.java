package app.organicmaps.routing;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;
import androidx.annotation.ColorInt;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

// Maps&Fly: a horizontal bar split into colored parts by their share, like the route statistics in Komoot.
public class StackedBarView extends View
{
  private final Paint mPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final RectF mRect = new RectF();
  private final float mGap;
  @NonNull
  private double[] mValues = new double[0];
  @NonNull
  private int[] mColors = new int[0];

  public StackedBarView(Context context, @Nullable AttributeSet attrs)
  {
    super(context, attrs);
    mGap = 2 * getResources().getDisplayMetrics().density;
  }

  public void setParts(@NonNull double[] values, @NonNull @ColorInt int[] colors)
  {
    mValues = values;
    mColors = colors;
    invalidate();
  }

  @Override
  protected void onDraw(@NonNull Canvas canvas)
  {
    double total = 0;
    int count = 0;
    for (double value : mValues)
    {
      if (value > 0)
      {
        total += value;
        ++count;
      }
    }
    if (total <= 0)
      return;

    final float height = getHeight();
    final float radius = height / 2;
    final float width = getWidth() - mGap * (count - 1);
    float left = 0;
    for (int i = 0; i < mValues.length; ++i)
    {
      if (mValues[i] <= 0)
        continue;
      // Keep tiny parts visible as a dot.
      final float partWidth = Math.max(height, (float) (width * mValues[i] / total));
      mRect.set(left, 0, Math.min(left + partWidth, getWidth()), height);
      mPaint.setColor(mColors[i]);
      canvas.drawRoundRect(mRect, radius, radius, mPaint);
      left = mRect.right + mGap;
      if (left >= getWidth())
        break;
    }
  }
}
