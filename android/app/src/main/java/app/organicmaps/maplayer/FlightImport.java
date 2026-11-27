package app.organicmaps.maplayer;

import android.net.Uri;
import android.widget.Toast;
import androidx.activity.ComponentActivity;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.annotation.NonNull;
import androidx.annotation.StringRes;
import app.organicmaps.R;
import app.organicmaps.sdk.maplayer.flight.FlightDhvImport;
import app.organicmaps.sdk.maplayer.flight.FlightLayer;
import com.google.android.material.dialog.MaterialAlertDialogBuilder;

/**
 * Maps&Fly: the private import of the DHV site file. The pilot downloads "DHV XML" at dhv.de and picks it
 * here; the sites stay on this phone only, as the DHV allows private use only.
 */
public final class FlightImport
{
  @NonNull
  private final ComponentActivity mActivity;
  @NonNull
  private final ActivityResultLauncher<String[]> mPicker;

  /** Must be created before the activity is started, like any activity result launcher. */
  public FlightImport(@NonNull ComponentActivity activity)
  {
    mActivity = activity;
    mPicker = activity.registerForActivityResult(new ActivityResultContracts.OpenDocument(), this::onPicked);
  }

  public void showDialog()
  {
    final int takeoffs = FlightLayer.getDhvTakeoffs();
    String message = mActivity.getString(R.string.flight_import_text);
    if (takeoffs > 0)
    {
      message += "\n\n" + mActivity.getString(R.string.flight_import_current, takeoffs, FlightLayer.getDhvGenerated());
    }
    final MaterialAlertDialogBuilder builder =
        new MaterialAlertDialogBuilder(mActivity, R.style.MwmTheme_AlertDialog)
            .setTitle(R.string.flight_import_title)
            .setMessage(message)
            // The DHV offers .xml and .zip, and some file pickers know neither type.
            .setPositiveButton(R.string.flight_import_choose,
                               (d, w) -> mPicker.launch(new String[] {"text/xml", "application/xml", "application/zip",
                                                                      "application/octet-stream", "*/*"}))
            .setNegativeButton(R.string.cancel, null);
    if (takeoffs > 0)
    {
      builder.setNeutralButton(R.string.flight_import_remove, (d, w) -> {
        FlightLayer.removeDhv(mActivity);
        Toast.makeText(mActivity, R.string.flight_import_removed, Toast.LENGTH_LONG).show();
      });
    }
    builder.show();
  }

  private void onPicked(Uri uri)
  {
    if (uri == null)
      return;
    FlightLayer.importDhv(mActivity, () -> mActivity.getContentResolver().openInputStream(uri),
                          new FlightLayer.ImportListener() {
                            @Override
                            public void onImported(int takeoffs)
                            {
                              Toast.makeText(mActivity, mActivity.getString(R.string.flight_import_done, takeoffs),
                                             Toast.LENGTH_LONG)
                                  .show();
                            }

                            @Override
                            public void onFailed(@NonNull Exception e)
                            {
                              @StringRes
                              int message = R.string.flight_import_failed;
                              if (e instanceof FlightDhvImport.FormatException format)
                              {
                                message = switch (format.mReason)
                                {
                                  case NO_XML_IN_ZIP -> R.string.flight_import_no_xml_in_zip;
                                  case INVALID_XML, NOT_DHV -> R.string.flight_import_not_dhv;
                                };
                              }
                              new MaterialAlertDialogBuilder(mActivity, R.style.MwmTheme_AlertDialog)
                                  .setTitle(R.string.flight_import_title)
                                  .setMessage(message)
                                  .setPositiveButton(R.string.ok, null)
                                  .show();
                            }
                          });
  }
}
