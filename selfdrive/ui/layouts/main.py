import pyray as rl
from enum import IntEnum
import cereal.messaging as messaging
from openpilot.common.swaglog import cloudlog
from openpilot.system.ui.lib.application import gui_app
from openpilot.selfdrive.ui.layouts.sidebar import Sidebar, SIDEBAR_WIDTH
from openpilot.selfdrive.ui.layouts.home import HomeLayout
from openpilot.selfdrive.ui.layouts.settings.settings import SettingsLayout, PanelType
from openpilot.selfdrive.ui.onroad.augmented_road_view import AugmentedRoadView
from openpilot.selfdrive.ui.ui_state import device, ui_state
from openpilot.system.ui.widgets import Widget
from openpilot.selfdrive.ui.layouts.onboarding import OnboardingWindow


class MainState(IntEnum):
  HOME = 0
  SETTINGS = 1
  ONROAD = 2


class MainLayout(Widget):
  def __init__(self):
    super().__init__()

    self._pm = messaging.PubMaster(['bookmarkButton'])

    self._sidebar = Sidebar()
    self._current_mode = MainState.HOME
    self._prev_onroad = False

    # Initialize layouts
    self._layouts = {MainState.HOME: HomeLayout(), MainState.SETTINGS: SettingsLayout(), MainState.ONROAD: AugmentedRoadView()}

    self._sidebar_rect = rl.Rectangle(0, 0, 0, 0)
    self._content_rect = rl.Rectangle(0, 0, 0, 0)

    # Set callbacks
    self._setup_callbacks()

    gui_app.push_widget(self)

    # Start onboarding if terms or training not completed, make sure to push after self
    self._onboarding_window = OnboardingWindow()
    self._power_supply_load_test_window = None
    self._power_supply_load_test_required_fn = None
    self._power_supply_load_test_loading_failed = False
    gui_app.add_nav_stack_tick(self._nav_stack_tick)
    if not self._onboarding_window.completed:
      gui_app.push_widget(self._onboarding_window)

  def _render(self, _):
    self._handle_onroad_transition()
    self._render_main_content()

  def _nav_stack_tick(self):
    self._show_power_supply_load_test_if_needed()

  def _setup_callbacks(self):
    self._sidebar.set_callbacks(on_settings=self._on_settings_clicked,
                                on_flag=self._on_bookmark_clicked,
                                open_settings=lambda: self.open_settings(PanelType.TOGGLES))
    self._layouts[MainState.HOME]._setup_widget.set_open_settings_callback(lambda: self.open_settings(PanelType.FIREHOSE))
    self._layouts[MainState.HOME].set_settings_callback(lambda: self.open_settings(PanelType.TOGGLES))
    self._layouts[MainState.SETTINGS].set_callbacks(on_close=self._set_mode_for_state)
    self._layouts[MainState.ONROAD].set_click_callback(self._on_onroad_clicked)
    device.add_interactive_timeout_callback(self._set_mode_for_state)

  def _update_layout_rects(self):
    self._sidebar_rect = rl.Rectangle(self._rect.x, self._rect.y, SIDEBAR_WIDTH, self._rect.height)

    x_offset = SIDEBAR_WIDTH if self._sidebar.is_visible else 0
    self._content_rect = rl.Rectangle(self._rect.y + x_offset, self._rect.y, self._rect.width - x_offset, self._rect.height)

  def _handle_onroad_transition(self):
    if ui_state.started != self._prev_onroad:
      self._prev_onroad = ui_state.started

      self._set_mode_for_state()

  def _set_mode_for_state(self):
    if ui_state.started:
      # Don't hide sidebar from interactive timeout
      if self._current_mode != MainState.ONROAD:
        self._sidebar.set_visible(False)
      self._set_current_layout(MainState.ONROAD)
    else:
      self._set_current_layout(MainState.HOME)
      self._sidebar.set_visible(True)

  def _set_current_layout(self, layout: MainState):
    if layout != self._current_mode:
      self._layouts[self._current_mode].hide_event()
      self._current_mode = layout
      self._layouts[self._current_mode].show_event()

  def open_settings(self, panel_type: PanelType):
    self._layouts[MainState.SETTINGS].set_current_panel(panel_type)
    self._set_current_layout(MainState.SETTINGS)
    self._sidebar.set_visible(False)

  def _on_settings_clicked(self):
    self.open_settings(PanelType.DEVICE)

  def _on_bookmark_clicked(self):
    user_bookmark = messaging.new_message('bookmarkButton')
    user_bookmark.valid = True
    self._pm.send('bookmarkButton', user_bookmark)

  def _on_onroad_clicked(self):
    self._sidebar.set_visible(not self._sidebar.is_visible)

  def _render_main_content(self):
    # Render sidebar
    if self._sidebar.is_visible:
      self._sidebar.render(self._sidebar_rect)

    content_rect = self._content_rect if self._sidebar.is_visible else self._rect
    self._layouts[self._current_mode].render(content_rect)

  def _ensure_power_supply_load_test_ready(self) -> bool:
    if self._power_supply_load_test_loading_failed:
      return False

    if self._power_supply_load_test_required_fn is not None and self._power_supply_load_test_window is not None:
      return True

    try:
      from openpilot.selfdrive.ui.layouts.power_supply_load_test import (
        PowerSupplyLoadTestWindow,
        power_supply_load_test_required,
        power_supply_load_test_supported,
      )
    except Exception:
      cloudlog.exception("failed to import power supply load test, disabling startup hook")
      self._power_supply_load_test_loading_failed = True
      return False

    if not power_supply_load_test_supported():
      self._power_supply_load_test_loading_failed = True
      return False

    try:
      self._power_supply_load_test_window = PowerSupplyLoadTestWindow()
      self._power_supply_load_test_required_fn = power_supply_load_test_required
      return True
    except Exception:
      cloudlog.exception("failed to initialize power supply load test, disabling startup hook")
      self._power_supply_load_test_loading_failed = True
      self._power_supply_load_test_window = None
      self._power_supply_load_test_required_fn = None
      return False

  def _show_power_supply_load_test_if_needed(self):
    if ui_state.started or not self._ensure_power_supply_load_test_ready():
      return

    current_commit = ui_state.params.get("GitCommit")
    if not self._power_supply_load_test_required_fn(ui_state.params, current_commit):
      return

    if not self._onboarding_window.completed:
      return

    active_widget = gui_app.get_active_widget()
    if active_widget not in (self, self._power_supply_load_test_window):
      return

    if not gui_app.widget_in_stack(self._power_supply_load_test_window):
      try:
        gui_app.push_widget(self._power_supply_load_test_window)
      except Exception:
        cloudlog.exception("failed to show power supply load test, disabling startup hook")
        self._power_supply_load_test_loading_failed = True
        self._power_supply_load_test_window = None
        self._power_supply_load_test_required_fn = None
