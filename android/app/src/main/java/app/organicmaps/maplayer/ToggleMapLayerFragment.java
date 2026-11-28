package app.organicmaps.maplayer;

import android.content.Context;
import android.os.Bundle;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.fragment.app.Fragment;
import androidx.recyclerview.widget.LinearLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import app.organicmaps.MwmActivity;
import app.organicmaps.MwmApplication;
import app.organicmaps.R;
import app.organicmaps.sdk.maplayer.Mode;
import app.organicmaps.sdk.util.SharedPropertiesUtils;
import app.organicmaps.util.ThemeSwitcher;
import app.organicmaps.util.Utils;
import app.organicmaps.util.bottomsheet.MenuBottomSheetFragment;
import app.organicmaps.widget.recycler.SpanningLinearLayoutManager;
import com.google.android.material.button.MaterialButton;
import java.util.ArrayList;
import java.util.List;

public class ToggleMapLayerFragment extends Fragment
{
  private static final String LAYERS_MENU_ID = "LAYERS_MENU_BOTTOM_SHEET";
  @Nullable
  private LayersAdapter mAdapter;
  private MapButtonsController mMapButtonsController;

  @Nullable
  @Override
  public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
                           @Nullable Bundle savedInstanceState)
  {
    View mRoot = inflater.inflate(R.layout.fragment_toggle_map_layer, container, false);

    mMapButtonsController =
        (MapButtonsController) requireActivity().getSupportFragmentManager().findFragmentById(R.id.map_buttons);
    MaterialButton mCloseButton = mRoot.findViewById(R.id.close_button);
    mCloseButton.setOnClickListener(view -> closeLayerBottomSheet());

    initRecycler(mRoot);
    return mRoot;
  }

  private void initRecycler(@NonNull View root)
  {
    RecyclerView recycler = root.findViewById(R.id.recycler);
    RecyclerView.LayoutManager layoutManager =
        new SpanningLinearLayoutManager(requireContext(), LinearLayoutManager.HORIZONTAL, false);
    recycler.setLayoutManager(layoutManager);
    mAdapter = new LayersAdapter(getLayersItems());
    recycler.setAdapter(mAdapter);
    recycler.setNestedScrollingEnabled(false);
  }

  private List<LayerBottomSheetItem> getLayersItems()
  {
    List<Mode> availableLayers = LayersUtils.getAvailableLayers();
    List<LayerBottomSheetItem> items = new ArrayList<>();
    for (Mode layer : availableLayers)
    {
      final LayerBottomSheetItem item = LayerBottomSheetItem.create(requireContext(), layer, this::onItemClick);
      if (LayerSettingsPanel.hasSettings(layer))
        item.setLongClickListener(this::onItemLongClick);
      items.add(item);
    }
    return items;
  }

  private void onItemClick(@NonNull View v, @NonNull LayerBottomSheetItem item)
  {
    Mode mode = item.getMode();
    Context context = v.getContext();
    SharedPropertiesUtils.setLayerMarkerShownForLayerMode(mode);
    mode.setEnabled(context, !mode.isEnabled(context));
    // TODO: dirty hack :(
    if (mode == Mode.OUTDOORS || mode == Mode.FLIGHT)
      ThemeSwitcher.INSTANCE.synchronizeMapStyle(v.getContext(), true);
    mAdapter.notifyDataSetChanged();
    mMapButtonsController.updateLayerButton();
    if (MwmApplication.from(context).getIsolinesManager().shouldShowNotification())
      Utils.showSnackbar(context, v.getRootView(), R.string.isolines_toast_zooms_1_10);
  }

  // Maps&Fly: the layer settings sit at the bottom of the map, so the map stays visible.
  private void onItemLongClick(@NonNull View v, @NonNull LayerBottomSheetItem item)
  {
    final LayerSettingsPanel panel =
        requireActivity() instanceof MwmActivity activity ? activity.getLayerSettingsPanel() : null;
    closeLayerBottomSheet();
    if (panel != null)
      panel.show(item.getMode());
  }

  private void closeLayerBottomSheet()
  {
    MenuBottomSheetFragment bottomSheet =
        (MenuBottomSheetFragment) requireActivity().getSupportFragmentManager().findFragmentByTag(LAYERS_MENU_ID);
    if (bottomSheet != null)
      bottomSheet.dismiss();
  }
}
