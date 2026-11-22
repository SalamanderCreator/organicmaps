package app.organicmaps.location;

import static android.Manifest.permission.ACCESS_FINE_LOCATION;
import static android.Manifest.permission.POST_NOTIFICATIONS;
import static android.content.pm.PackageManager.PERMISSION_GRANTED;

import android.app.Notification;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.location.Location;
import android.media.AudioAttributes;
import android.media.RingtoneManager;
import android.os.Build;
import android.os.IBinder;
import android.os.SystemClock;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.annotation.RequiresPermission;
import androidx.core.app.ActivityCompat;
import androidx.core.app.NotificationChannelCompat;
import androidx.core.app.NotificationCompat;
import androidx.core.app.NotificationManagerCompat;
import androidx.core.app.ServiceCompat;
import androidx.core.content.ContextCompat;
import androidx.lifecycle.LiveData;
import androidx.lifecycle.MutableLiveData;
import app.organicmaps.MwmApplication;
import app.organicmaps.R;
import app.organicmaps.sdk.location.LocationHelper;
import app.organicmaps.sdk.location.LocationListener;
import app.organicmaps.sdk.location.LocationUtils;
import app.organicmaps.sdk.location.OffTrackMonitor;
import app.organicmaps.sdk.sound.TtsPlayer;
import app.organicmaps.sdk.util.StringUtils;
import app.organicmaps.sdk.util.log.Logger;

/**
 * Maps&Fly: watches the distance to a chosen track in the background and raises an alarm with sound, vibration and
 * a voice announcement when the position leaves it.
 */
public class OffTrackService extends Service implements LocationListener
{
  private static final String TAG = OffTrackService.class.getSimpleName();
  private static final String STATUS_CHANNEL_ID = "OFF TRACK STATUS";
  private static final String ALARM_CHANNEL_ID = "OFF TRACK ALARM";
  private static final int STATUS_NOTIFICATION_ID = 54322;
  private static final int ALARM_NOTIFICATION_ID = 54323;
  private static final String ACTION_STOP = "STOP_OFF_TRACK_MONITOR";
  private static final String EXTRA_TRACK_NAME = "TRACK_NAME";
  private static final long[] ALARM_VIBRATION = {0, 600, 300, 600, 300, 600};
  // The distance changes with every fix.
  private static final long STATUS_UPDATE_INTERVAL_MS = 3000;
  public static final int DEFAULT_THRESHOLD_METERS = 30;

  // The watched track, or null.
  private static final MutableLiveData<Long> sActiveTrackId = new MutableLiveData<>(null);

  private String mTrackName = "";
  private int mState = OffTrackMonitor.STATE_ON_TRACK;
  private boolean mHasFix;
  private long mStatusTimeMs;
  @Nullable
  private NotificationCompat.Builder mStatusBuilder;

  @NonNull
  public static LiveData<Long> getActiveTrackId()
  {
    return sActiveTrackId;
  }

  public static boolean isWatching(long trackId)
  {
    final Long id = sActiveTrackId.getValue();
    return id != null && id == trackId;
  }

  public static int getThresholdMeters(@NonNull Context context)
  {
    return MwmApplication.prefs(context).getInt(context.getString(R.string.pref_off_track_threshold),
                                                DEFAULT_THRESHOLD_METERS);
  }

  /** Also applies to a running alarm. */
  public static void setThresholdMeters(@NonNull Context context, int meters)
  {
    MwmApplication.prefs(context).edit().putInt(context.getString(R.string.pref_off_track_threshold), meters).apply();
    OffTrackMonitor.nativeSetThreshold(meters);
  }

  /** Returns {@code false} if the track can't be watched. */
  @RequiresPermission(value = ACCESS_FINE_LOCATION)
  public static boolean start(@NonNull Context context, long trackId, @NonNull String trackName)
  {
    if (!OffTrackMonitor.nativeStart(trackId, getThresholdMeters(context)))
    {
      Logger.w(TAG, "Track " + trackId + " has no geometry");
      return false;
    }
    sActiveTrackId.setValue(trackId);
    MwmApplication.from(context).getLocationHelper().restartWithNewMode();
    final Intent intent = new Intent(context, OffTrackService.class).putExtra(EXTRA_TRACK_NAME, trackName);
    ContextCompat.startForegroundService(context, intent);
    return true;
  }

  public static void stop(@NonNull Context context)
  {
    context.stopService(new Intent(context, OffTrackService.class));
  }

  public static void createNotificationChannels(@NonNull Context context)
  {
    final NotificationManagerCompat manager = NotificationManagerCompat.from(context);
    manager.createNotificationChannel(
        new NotificationChannelCompat.Builder(STATUS_CHANNEL_ID, NotificationManagerCompat.IMPORTANCE_LOW)
            .setName(context.getString(R.string.off_track_status_channel))
            .setLightsEnabled(false)
            .setVibrationEnabled(false)
            .build());
    final AudioAttributes audio = new AudioAttributes.Builder()
                                      .setUsage(AudioAttributes.USAGE_ALARM)
                                      .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                                      .build();
    manager.createNotificationChannel(
        new NotificationChannelCompat.Builder(ALARM_CHANNEL_ID, NotificationManagerCompat.IMPORTANCE_HIGH)
            .setName(context.getString(R.string.off_track_alarm_channel))
            .setSound(RingtoneManager.getDefaultUri(RingtoneManager.TYPE_NOTIFICATION), audio)
            .setVibrationEnabled(true)
            .setVibrationPattern(ALARM_VIBRATION)
            .build());
  }

  @Nullable
  @Override
  public IBinder onBind(Intent intent)
  {
    return null;
  }

  @Override
  public int onStartCommand(@Nullable Intent intent, int flags, int startId)
  {
    if (!MwmApplication.from(this).getOrganicMaps().arePlatformAndCoreInitialized() || !OffTrackMonitor.nativeIsActive()
        || !LocationUtils.checkFineLocationPermission(this))
    {
      // E.g. a start command re-delivered after the process died: the watched track is gone with it.
      Logger.w(TAG, "Nothing to watch");
      stopSelf();
      return START_NOT_STICKY;
    }

    if (intent != null && ACTION_STOP.equals(intent.getAction()))
    {
      stopSelf();
      return START_NOT_STICKY;
    }

    if (intent != null && intent.hasExtra(EXTRA_TRACK_NAME))
    {
      mTrackName = intent.getStringExtra(EXTRA_TRACK_NAME);
      mState = OffTrackMonitor.STATE_ON_TRACK;
      mHasFix = false;
      mStatusBuilder = null;
      NotificationManagerCompat.from(this).cancel(ALARM_NOTIFICATION_ID);
    }

    try
    {
      ServiceCompat.startForeground(
          this, STATUS_NOTIFICATION_ID, getStatusBuilder().build(),
          Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ? ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION : 0);
    }
    catch (IllegalStateException | SecurityException e)
    {
      // Android 14+ checks location access here.
      Logger.e(TAG, "The system refused the location foreground service", e);
      stopSelf();
      return START_NOT_STICKY;
    }

    final LocationHelper locationHelper = MwmApplication.from(this).getLocationHelper();
    locationHelper.addListener(this);
    locationHelper.restartWithNewMode();
    return START_NOT_STICKY;
  }

  @Override
  public void onDestroy()
  {
    if (MwmApplication.from(this).getOrganicMaps().arePlatformAndCoreInitialized())
    {
      OffTrackMonitor.nativeStop();
      MwmApplication.from(this).getLocationHelper().removeListener(this);
      MwmApplication.from(this).onNavigationOrRecordingStopped();
    }
    sActiveTrackId.setValue(null);
    NotificationManagerCompat.from(this).cancel(ALARM_NOTIFICATION_ID);
    super.onDestroy();
  }

  @Override
  public void onLocationUpdated(@NonNull Location location)
  {
    if (!OffTrackMonitor.nativeIsActive())
      return;

    // Without an accuracy the fix can't be trusted.
    final double accuracy = location.hasAccuracy() ? location.getAccuracy() : Double.MAX_VALUE;
    final int state = OffTrackMonitor.nativeUpdate(location.getLatitude(), location.getLongitude(), accuracy,
                                                   location.getElapsedRealtimeNanos() / 1e9);
    final int previous = mState;
    mState = state;
    mHasFix = true;

    if (state == OffTrackMonitor.STATE_OFF_TRACK && previous != OffTrackMonitor.STATE_OFF_TRACK)
      raiseAlarm();
    else if (state != OffTrackMonitor.STATE_OFF_TRACK && previous == OffTrackMonitor.STATE_OFF_TRACK)
    {
      NotificationManagerCompat.from(this).cancel(ALARM_NOTIFICATION_ID);
      if (state == OffTrackMonitor.STATE_ON_TRACK)
        TtsPlayer.INSTANCE.speak(getString(R.string.off_track_back_speech));
    }

    final long now = SystemClock.elapsedRealtime();
    if (state != previous || now - mStatusTimeMs >= STATUS_UPDATE_INTERVAL_MS)
    {
      mStatusTimeMs = now;
      updateStatus(accuracy);
    }
  }

  @NonNull
  private String formatDistance(double meters)
  {
    return StringUtils.nativeFormatDistance(meters).toString(this);
  }

  private void raiseAlarm()
  {
    Logger.i(TAG, "Off the track, distance " + OffTrackMonitor.nativeGetDistance());
    TtsPlayer.INSTANCE.speak(getString(R.string.off_track_alarm_speech));
    if (!canNotify())
      return;
    final String text = getString(R.string.off_track_alarm_text, formatDistance(OffTrackMonitor.nativeGetDistance()));
    final Notification alarm = new NotificationCompat.Builder(this, ALARM_CHANNEL_ID)
                                   .setCategory(NotificationCompat.CATEGORY_ALARM)
                                   .setPriority(NotificationCompat.PRIORITY_HIGH)
                                   .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
                                   .setSmallIcon(R.drawable.warning_icon)
                                   .setContentTitle(getString(R.string.off_track_alarm_title))
                                   .setContentText(text)
                                   .setVibrate(ALARM_VIBRATION)
                                   .setDefaults(NotificationCompat.DEFAULT_SOUND)
                                   .setAutoCancel(true)
                                   .setContentIntent(TrackRecordingService.createContentPendingIntent(this))
                                   .addAction(0, getString(R.string.off_track_monitor_stop), getStopIntent())
                                   .setColor(ContextCompat.getColor(this, R.color.notification_warning))
                                   .build();
    NotificationManagerCompat.from(this).notify(ALARM_NOTIFICATION_ID, alarm);
  }

  @Override
  public void onLocationUpdateTimeout()
  {
    mHasFix = false;
    updateStatus(0);
  }

  @Override
  public void onLocationUnavailable()
  {
    mHasFix = false;
    updateStatus(0);
  }

  private void updateStatus(double accuracy)
  {
    final String text;
    if (!mHasFix)
      text = getString(R.string.off_track_waiting_for_gps);
    else if (mState == OffTrackMonitor.STATE_GPS_INACCURATE)
      text = getString(R.string.off_track_gps_inaccurate, formatDistance(accuracy));
    else if (mState == OffTrackMonitor.STATE_OFF_TRACK)
      text = getString(R.string.off_track_alarm_text, formatDistance(OffTrackMonitor.nativeGetDistance()));
    else
      text = getString(R.string.off_track_on_track, formatDistance(OffTrackMonitor.nativeGetDistance()));
    getStatusBuilder().setContentText(text);
    if (canNotify())
      NotificationManagerCompat.from(this).notify(STATUS_NOTIFICATION_ID, getStatusBuilder().build());
  }

  private boolean canNotify()
  {
    return Build.VERSION.SDK_INT < Build.VERSION_CODES.TIRAMISU
 || ActivityCompat.checkSelfPermission(this, POST_NOTIFICATIONS) == PERMISSION_GRANTED;
  }

  @NonNull
  private PendingIntent getStopIntent()
  {
    final int flags = PendingIntent.FLAG_UPDATE_CURRENT
                    | (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M ? PendingIntent.FLAG_IMMUTABLE : 0);
    return PendingIntent.getService(this, 0, new Intent(this, OffTrackService.class).setAction(ACTION_STOP), flags);
  }

  @NonNull
  private NotificationCompat.Builder getStatusBuilder()
  {
    if (mStatusBuilder == null)
      mStatusBuilder = new NotificationCompat.Builder(this, STATUS_CHANNEL_ID)
                           .setCategory(NotificationCompat.CATEGORY_SERVICE)
                           .setPriority(NotificationCompat.PRIORITY_LOW)
                           .setVisibility(NotificationCompat.VISIBILITY_PUBLIC)
                           .setOngoing(true)
                           .setOnlyAlertOnce(true)
                           .setSmallIcon(app.organicmaps.branding.R.drawable.ic_splash)
                           .setContentTitle(getString(R.string.off_track_watching, mTrackName))
                           .setContentText(getString(R.string.off_track_waiting_for_gps))
                           .setContentIntent(TrackRecordingService.createContentPendingIntent(this))
                           .addAction(0, getString(R.string.off_track_monitor_stop), getStopIntent())
                           .setColor(ContextCompat.getColor(this, R.color.notification));
    return mStatusBuilder;
  }
}
