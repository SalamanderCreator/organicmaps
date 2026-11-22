package app.organicmaps.location;

import android.content.ClipData;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.location.Location;
import android.net.Uri;
import android.os.Build;
import android.widget.Toast;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.FileProvider;
import androidx.core.location.LocationCompat;
import app.organicmaps.BuildConfig;
import app.organicmaps.R;
import app.organicmaps.sdk.location.LocationHelper;
import app.organicmaps.sdk.util.log.Logger;
import java.io.BufferedWriter;
import java.io.File;
import java.io.FileWriter;
import java.io.IOException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.Locale;
import java.util.TimeZone;

/**
 * Maps&Fly debug mode: while a track is recorded, writes every fix of the location provider to a CSV file, with its
 * accuracy, source and whether the app used it. Used to tune the track filter.
 */
public final class TrackRecordingDebugLog implements LocationHelper.RawLocationListener
{
  private static final String TAG = TrackRecordingDebugLog.class.getSimpleName();
  private static final String PREFS = "track_debug";
  private static final String KEY_ENABLED = "enabled";
  private static final String DIR = "track_debug";
  private static final String HEADER = "time_utc,provider,status,lat,lon,accuracy_m,altitude_m,vertical_accuracy_m,"
                                     + "speed_mps,speed_accuracy_mps,bearing_deg,satellites,mock";

  @Nullable
  private static TrackRecordingDebugLog sInstance;

  @NonNull
  private final BufferedWriter mWriter;
  @NonNull
  private final SimpleDateFormat mTimeFormat = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'", Locale.US);

  private TrackRecordingDebugLog(@NonNull BufferedWriter writer)
  {
    mWriter = writer;
    mTimeFormat.setTimeZone(TimeZone.getTimeZone("UTC"));
  }

  public static boolean isEnabled(@NonNull Context context)
  {
    return prefs(context).getBoolean(KEY_ENABLED, false);
  }

  public static void setEnabled(@NonNull Context context, boolean enabled)
  {
    prefs(context).edit().putBoolean(KEY_ENABLED, enabled).apply();
  }

  /** Starts a new file if the debug mode is on. Does nothing if a file is already being written. */
  static void start(@NonNull Context context, @NonNull LocationHelper locationHelper)
  {
    if (sInstance != null || !isEnabled(context))
      return;

    final File dir = getDir(context);
    if (dir == null || (!dir.isDirectory() && !dir.mkdirs()))
    {
      Logger.e(TAG, "Cannot create the debug directory");
      return;
    }
    final SimpleDateFormat nameFormat = new SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US);
    final File file = new File(dir, "track-raw-" + nameFormat.format(new Date()) + ".csv");
    try
    {
      final BufferedWriter writer = new BufferedWriter(new FileWriter(file));
      writer.write(HEADER);
      writer.newLine();
      writer.flush();
      sInstance = new TrackRecordingDebugLog(writer);
      locationHelper.setRawLocationListener(sInstance);
      Logger.i(TAG, "Writing raw fixes to " + file);
    }
    catch (IOException e)
    {
      Logger.e(TAG, "Cannot create " + file, e);
    }
  }

  static void stop(@NonNull LocationHelper locationHelper)
  {
    if (sInstance == null)
      return;
    locationHelper.setRawLocationListener(null);
    try
    {
      sInstance.mWriter.close();
    }
    catch (IOException e)
    {
      Logger.e(TAG, "Cannot close the debug file", e);
    }
    sInstance = null;
  }

  @Override
  public void onRawLocation(@NonNull Location location, @NonNull String status)
  {
    final StringBuilder line = new StringBuilder(160);
    line.append(mTimeFormat.format(new Date(location.getTime()))).append(',');
    line.append(location.getProvider()).append(',');
    line.append(status).append(',');
    line.append(location.getLatitude()).append(',');
    line.append(location.getLongitude()).append(',');
    line.append(location.hasAccuracy() ? location.getAccuracy() : "").append(',');
    line.append(location.hasAltitude() ? location.getAltitude() : "").append(',');
    line.append(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && location.hasVerticalAccuracy()
                    ? location.getVerticalAccuracyMeters()
                    : "")
        .append(',');
    line.append(location.hasSpeed() ? location.getSpeed() : "").append(',');
    line.append(Build.VERSION.SDK_INT >= Build.VERSION_CODES.O && location.hasSpeedAccuracy()
                    ? location.getSpeedAccuracyMetersPerSecond()
                    : "")
        .append(',');
    line.append(location.hasBearing() ? location.getBearing() : "").append(',');
    final int satellites = location.getExtras() != null ? location.getExtras().getInt("satellites", -1) : -1;
    line.append(satellites >= 0 ? satellites : "").append(',');
    line.append(LocationCompat.isMock(location) ? 1 : 0);
    try
    {
      mWriter.write(line.toString());
      mWriter.newLine();
      // A recording can end with the process being killed.
      mWriter.flush();
    }
    catch (IOException e)
    {
      Logger.e(TAG, "Cannot write a raw fix", e);
    }
  }

  /** Opens the share dialog for the newest file. */
  public static void shareLatest(@NonNull Context context)
  {
    final File dir = getDir(context);
    final File[] files = dir != null ? dir.listFiles((d, name) -> name.endsWith(".csv")) : null;
    File latest = null;
    if (files != null)
    {
      for (File file : files)
      {
        if (latest == null || file.lastModified() > latest.lastModified())
          latest = file;
      }
    }
    if (latest == null)
    {
      Toast.makeText(context, R.string.track_debug_nothing_to_share, Toast.LENGTH_SHORT).show();
      return;
    }

    final Uri uri = FileProvider.getUriForFile(context, BuildConfig.FILE_PROVIDER_AUTHORITY, latest);
    final Intent intent = new Intent(Intent.ACTION_SEND);
    intent.setType("text/csv");
    intent.putExtra(Intent.EXTRA_STREAM, uri);
    intent.putExtra(Intent.EXTRA_SUBJECT, latest.getName());
    // The chooser grants the read permission only for the ClipData.
    intent.setClipData(ClipData.newRawUri(latest.getName(), uri));
    intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
    context.startActivity(Intent.createChooser(intent, latest.getName()));
  }

  @Nullable
  private static File getDir(@NonNull Context context)
  {
    final File base = context.getExternalFilesDir(null);
    return base != null ? new File(base, DIR) : null;
  }

  @NonNull
  private static SharedPreferences prefs(@NonNull Context context)
  {
    return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
  }
}
