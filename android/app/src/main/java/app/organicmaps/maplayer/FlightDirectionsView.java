package app.organicmaps.maplayer;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.AttributeSet;
import android.view.View;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;

/**
 * Maps&Fly: the takeoff directions as in the flight layer, without the paraglider: a small white dot with
 * the green sectors around it. Bit i of the directions is i * 22.5 degrees clockwise from north.
 */
public class FlightDirectionsView extends View
{
  private static final int SECTORS = 16;
  private static final float SECTOR_DEG = 360f / SECTORS;
  // A small dot: no paraglider inside, unlike the layer.
  private static final float DISC = 0.35f;

  private final Paint mSector = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mSeparator = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mDisc = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final Paint mRing = new Paint(Paint.ANTI_ALIAS_FLAG);
  private final RectF mOval = new RectF();
  private int mDirections;

  public FlightDirectionsView(@NonNull Context context, @Nullable AttributeSet attrs)
  {
    super(context, attrs);
    final float dp = getResources().getDisplayMetrics().density;
    mSector.setColor(Color.argb(235, 40, 170, 70));
    mSeparator.setColor(Color.rgb(235, 250, 235));
    mSeparator.setStyle(Paint.Style.STROKE);
    mSeparator.setStrokeWidth(dp);
    mDisc.setColor(Color.WHITE);
    mRing.setColor(Color.rgb(60, 60, 60));
    mRing.setStyle(Paint.Style.STROKE);
    mRing.setStrokeWidth(1.5f * dp);
  }

  public void setDirections(int directions)
  {
    mDirections = directions;
    setVisibility(directions == 0 ? GONE : VISIBLE);
    invalidate();
  }

  @Override
  protected void onDraw(@NonNull Canvas canvas)
  {
    final float cx = getWidth() / 2f, cy = getHeight() / 2f;
    final float radius = Math.min(cx, cy) - mSeparator.getStrokeWidth();
    mOval.set(cx - radius, cy - radius, cx + radius, cy + radius);

    // With only the 8 main directions each one is 45 degrees wide, as in the layer.
    boolean onlyEven = true;
    for (int i = 1; i < SECTORS; i += 2)
      if ((mDirections & (1 << i)) != 0)
        onlyEven = false;
    final float width = onlyEven ? 2 * SECTOR_DEG : SECTOR_DEG;
    for (int i = 0; i < SECTORS; ++i)
    {
      if ((mDirections & (1 << i)) == 0)
        continue;
      // Canvas angles start at east and turn clockwise; compass angles start at north.
      final float start = i * SECTOR_DEG - 90 - width / 2;
      canvas.drawArc(mOval, start, width, true, mSector);
      canvas.drawArc(mOval, start, width, true, mSeparator);
    }
    final float disc = radius * DISC;
    canvas.drawCircle(cx, cy, disc, mDisc);
    canvas.drawCircle(cx, cy, disc, mRing);
  }
}
