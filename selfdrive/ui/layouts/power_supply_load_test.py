import datetime
import time
from collections import deque
from dataclasses import dataclass
from enum import IntEnum

import pyray as rl
from cereal import log

from openpilot.common.params import Params
from openpilot.common.swaglog import cloudlog
from openpilot.selfdrive.selfdrived.alertmanager import set_offroad_alert
from openpilot.selfdrive.ui.ui_state import ui_state
from openpilot.system.hardware import HARDWARE, PC
from openpilot.system.ui.lib.application import FONT_SCALE, FontWeight, GL_VERSION, gui_app
from openpilot.system.ui.lib.wrap_text import wrap_text
from openpilot.system.ui.widgets import Widget
from openpilot.system.ui.widgets.button import Button, ButtonStyle

SUPPORTED_DEVICE_TYPES = {"tici", "tizi"}
POWER_SUPPLY_LOAD_TEST_DURATION_S = 10.0
POWER_SUPPLY_LOAD_TEST_SAMPLE_INTERVAL_S = 0.1
POWER_SUPPLY_LOAD_TEST_MIN_DEVICE_VOLTAGE_PARAM = "PowerSupplyLoadTestMinDeviceVoltageMv"
POWER_SUPPLY_LOAD_TEST_MAX_TEMP_PARAM = "PowerSupplyLoadTestMaxTempC"
POWER_SUPPLY_LOAD_TEST_RESULT_PARAM = "PowerSupplyLoadTestLastResult"
POWER_SUPPLY_LOAD_TESTED_COMMIT_PARAM = "PowerSupplyLoadTestedCommit"

TITLE_FONT_SIZE = 92
BODY_FONT_SIZE = 46
LOG_FONT_SIZE = 36
PROGRESS_HEIGHT = 30
PANEL_RADIUS = 0.04
PANEL_MARGIN = 70

STATUS_COLORS = {
  "preparing": rl.Color(255, 255, 255, 255),
  "running": rl.Color(255, 206, 92, 255),
  "passed": rl.Color(82, 214, 131, 255),
  "failed": rl.Color(255, 89, 79, 255),
}

# This shader is intentionally math-heavy so the diagnostics page can keep the
# GPU busy without depending on other openpilot rendering pipelines.
STRESS_VERTEX_SHADER = GL_VERSION + """
in vec3 vertexPosition;
in vec2 vertexTexCoord;
out vec2 fragTexCoord;
uniform mat4 mvp;

void main() {
  fragTexCoord = vertexTexCoord;
  gl_Position = mvp * vec4(vertexPosition, 1.0);
}
"""

STRESS_FRAGMENT_SHADER = GL_VERSION + """
precision highp float;
in vec2 fragTexCoord;
out vec4 finalColor;

uniform vec2 resolution;
uniform float clock;

void main() {
  vec2 uv = (gl_FragCoord.xy / resolution.xy) * 2.0 - 1.0;
  uv.x *= resolution.x / max(resolution.y, 1.0);

  vec2 z = uv * 1.7;
  float energy = 0.0;
  vec3 color = vec3(0.0);

  for (int i = 0; i < 56; ++i) {
    float fi = float(i);
    vec2 offset = vec2(sin(clock * 0.27 + fi * 0.13), cos(clock * 0.31 + fi * 0.17));
    vec2 w = z + offset;
    float denom = max(dot(w, w), 0.2);
    z = vec2(w.x * w.x - w.y * w.y, 2.0 * w.x * w.y) / denom + uv;
    energy += exp(-abs(dot(z, z) - 1.4) * 2.2);
    color += 0.55 + 0.45 * cos(vec3(0.0, 2.0, 4.0) + fi * 0.21 + clock + energy);
  }

  color = pow(color / 56.0, vec3(1.2));
  finalColor = vec4(color, 1.0);
}
"""


class LoadTestPhase(IntEnum):
  PREPARING = 0
  RUNNING = 1
  PASSED = 2
  FAILED = 3


@dataclass
class PowerSupplySnapshot:
  elapsed_s: float
  device_voltage_mv: int | None
  car_voltage_mv: int | None
  device_temp_c: float | None
  gpu_usage_percent: int | None


def power_supply_load_test_supported() -> bool:
  return not PC and HARDWARE.get_device_type() in SUPPORTED_DEVICE_TYPES


def power_supply_load_test_required(params: Params, current_commit: str | None) -> bool:
  # Run once per installed commit so the same gate naturally covers both
  # first install and any later software update.
  if not power_supply_load_test_supported() or not current_commit:
    return False
  return params.get(POWER_SUPPLY_LOAD_TESTED_COMMIT_PARAM) != current_commit


class PowerSupplyLoadTestWindow(Widget):
  def __init__(self):
    super().__init__()
    self.params = Params()

    self._continue_button = self._child(Button("Continue", button_style=ButtonStyle.PRIMARY, click_callback=self._close))

    self._shader: rl.Shader | None = None
    self._shader_clock_loc = -1
    self._shader_resolution_loc = -1
    self._shader_clock_ptr = rl.ffi.new("float[]", [0.0])
    self._shader_resolution_ptr = rl.ffi.new("float[]", [0.0, 0.0])

    self._phase = LoadTestPhase.PREPARING
    self._status_key = "preparing"
    self._status_text = "Preparing load test..."
    self._current_commit: str | None = None
    self._min_device_voltage_limit_mv = 7000
    self._max_temp_limit_c = -1
    self._start_time: float | None = None
    self._last_sample_time: float | None = None
    self._baseline: PowerSupplySnapshot | None = None
    self._latest_snapshot: PowerSupplySnapshot | None = None
    self._samples: list[PowerSupplySnapshot] = []
    self._log_lines: deque[str] = deque(maxlen=14)
    self._min_device_voltage_seen_mv: int | None = None
    self._min_car_voltage_seen_mv: int | None = None
    self._max_device_temp_seen_c: float | None = None
    self._failure_reason = ""
    self._result_code = ""

  def show_event(self):
    super().show_event()
    self._reset()
    self._ensure_shader()

  def hide_event(self):
    super().hide_event()
    self._continue_button.set_visible(False)

  def _reset(self):
    self._phase = LoadTestPhase.PREPARING
    self._status_key = "preparing"
    self._status_text = "Preparing load test..."
    self._current_commit = self.params.get("GitCommit")
    self._min_device_voltage_limit_mv = self.params.get(POWER_SUPPLY_LOAD_TEST_MIN_DEVICE_VOLTAGE_PARAM, return_default=True) or 7000
    self._max_temp_limit_c = self.params.get(POWER_SUPPLY_LOAD_TEST_MAX_TEMP_PARAM, return_default=True) or -1
    self._start_time = None
    self._last_sample_time = None
    self._baseline = None
    self._latest_snapshot = None
    self._samples = []
    self._log_lines.clear()
    self._min_device_voltage_seen_mv = None
    self._min_car_voltage_seen_mv = None
    self._max_device_temp_seen_c = None
    self._failure_reason = ""
    self._result_code = ""
    self._continue_button.set_visible(False)

  def _ensure_shader(self):
    if self._shader is not None:
      return

    self._shader = rl.load_shader_from_memory(STRESS_VERTEX_SHADER, STRESS_FRAGMENT_SHADER)
    self._shader_clock_loc = rl.get_shader_location(self._shader, "clock")
    self._shader_resolution_loc = rl.get_shader_location(self._shader, "resolution")

  def _close(self):
    gui_app.pop_widget()

  def _update_state(self):
    try:
      if ui_state.started:
        if self._phase in (LoadTestPhase.PREPARING, LoadTestPhase.RUNNING):
          cloudlog.warning("deferring power supply load test until next offroad session")
        self._close()
        return

      if self._phase == LoadTestPhase.PREPARING:
        self._start_test()
      elif self._phase == LoadTestPhase.RUNNING:
        self._collect_samples()
    except Exception:
      cloudlog.exception("power supply load test failed unexpectedly")
      if self._phase not in (LoadTestPhase.PASSED, LoadTestPhase.FAILED):
        self._finish_failure("the diagnostics screen hit an unexpected error", "internal_error")

  def _start_test(self):
    self._baseline = self._read_snapshot(0.0)
    self._latest_snapshot = self._baseline
    self._record_snapshot(self._baseline, prefix="baseline")

    if self._baseline.device_voltage_mv is None:
      self._finish_failure("internal device voltage is unavailable before the load test starts", "telemetry")
      return

    self._status_key = "running"
    self._status_text = "Running GPU load for 10 seconds..."
    self._phase = LoadTestPhase.RUNNING
    self._start_time = time.monotonic()
    self._last_sample_time = self._start_time
    set_offroad_alert("Offroad_PowerSupplyLoadTestFailed", False)
    cloudlog.event(
      "power supply load test started",
      commit=self._current_commit,
      min_device_voltage_mv=self._min_device_voltage_limit_mv,
      max_temp_c=self._max_temp_limit_c,
      baseline_device_voltage_mv=self._baseline.device_voltage_mv,
      baseline_car_voltage_mv=self._baseline.car_voltage_mv,
      baseline_device_temp_c=self._baseline.device_temp_c,
    )

  def _collect_samples(self):
    assert self._start_time is not None
    assert self._last_sample_time is not None

    now = time.monotonic()
    # Catch up in fixed sampling steps if the UI stalls briefly so the
    # voltage/temperature thresholds still see the full 10-second window.
    while (now - self._last_sample_time) >= POWER_SUPPLY_LOAD_TEST_SAMPLE_INTERVAL_S and self._phase == LoadTestPhase.RUNNING:
      self._last_sample_time += POWER_SUPPLY_LOAD_TEST_SAMPLE_INTERVAL_S
      elapsed_s = min(self._last_sample_time - self._start_time, POWER_SUPPLY_LOAD_TEST_DURATION_S)
      snapshot = self._read_snapshot(elapsed_s)
      self._latest_snapshot = snapshot
      self._record_snapshot(snapshot)

      if snapshot.device_voltage_mv is None:
        self._finish_failure("internal device voltage telemetry disappeared during the load test", "telemetry")
        return

      if snapshot.device_voltage_mv < self._min_device_voltage_limit_mv:
        detail = f"device voltage dropped to {self._format_voltage(snapshot.device_voltage_mv)}"
        self._finish_failure(detail, "voltage_drop")
        return

      if self._max_temp_limit_c >= 0 and snapshot.device_temp_c is not None and snapshot.device_temp_c > self._max_temp_limit_c:
        detail = f"device temperature reached {snapshot.device_temp_c:.1f} C"
        self._finish_failure(detail, "temperature_limit")
        return

      if elapsed_s >= POWER_SUPPLY_LOAD_TEST_DURATION_S:
        self._finish_success()
        return

  def _finish_success(self):
    self._phase = LoadTestPhase.PASSED
    self._status_key = "passed"
    self._status_text = "Power supply load test passed."
    self._result_code = "passed"
    self._continue_button.set_visible(True)
    self._add_log("Result: load test passed.")
    self._store_result()
    set_offroad_alert("Offroad_PowerSupplyLoadTestFailed", False)
    cloudlog.event("power supply load test passed", **self._build_result())

  def _finish_failure(self, detail: str, result_code: str):
    self._phase = LoadTestPhase.FAILED
    self._status_key = "failed"
    self._status_text = "Power supply load test failed."
    self._failure_reason = detail
    self._result_code = result_code
    self._continue_button.set_visible(True)

    if result_code == "voltage_drop":
      limit_text = self._format_voltage(self._min_device_voltage_limit_mv)
      extra_text = f"{detail}, below the configured {limit_text} limit."
      self.params.put("LastPowerDropDetected", extra_text)
    elif result_code == "temperature_limit":
      extra_text = f"{detail}, above the configured {self._max_temp_limit_c} C limit."
    else:
      extra_text = detail

    self._add_log(f"Result: {extra_text}")
    self._store_result()
    set_offroad_alert("Offroad_PowerSupplyLoadTestFailed", True, extra_text=extra_text)
    cloudlog.event("power supply load test failed", **self._build_result())

  def _store_result(self):
    if self._current_commit:
      self.params.put(POWER_SUPPLY_LOAD_TESTED_COMMIT_PARAM, self._current_commit)
    self.params.put(POWER_SUPPLY_LOAD_TEST_RESULT_PARAM, self._build_result())

  def _build_result(self) -> dict:
    end_time = datetime.datetime.now(datetime.UTC).isoformat()
    duration_s = min((time.monotonic() - self._start_time), POWER_SUPPLY_LOAD_TEST_DURATION_S) if self._start_time is not None else 0.0
    return {
      "commit": self._current_commit,
      "deviceType": HARDWARE.get_device_type(),
      "endedAt": end_time,
      "result": self._result_code or ("passed" if self._phase == LoadTestPhase.PASSED else "failed"),
      "detail": self._failure_reason,
      "durationSec": round(duration_s, 3),
      "sampleCount": len(self._samples),
      "minDeviceVoltageMv": self._min_device_voltage_seen_mv,
      "minCarVoltageMv": self._min_car_voltage_seen_mv,
      "maxDeviceTempC": round(self._max_device_temp_seen_c, 2) if self._max_device_temp_seen_c is not None else None,
      "thresholdDeviceVoltageMv": self._min_device_voltage_limit_mv,
      "thresholdMaxTempC": self._max_temp_limit_c,
      "baselineDeviceVoltageMv": self._baseline.device_voltage_mv if self._baseline is not None else None,
      "baselineCarVoltageMv": self._baseline.car_voltage_mv if self._baseline is not None else None,
      "baselineDeviceTempC": round(self._baseline.device_temp_c, 2) if self._baseline is not None and self._baseline.device_temp_c is not None else None,
    }

  def _record_snapshot(self, snapshot: PowerSupplySnapshot, prefix: str | None = None):
    self._samples.append(snapshot)

    if snapshot.device_voltage_mv is not None:
      if self._min_device_voltage_seen_mv is None or snapshot.device_voltage_mv < self._min_device_voltage_seen_mv:
        self._min_device_voltage_seen_mv = snapshot.device_voltage_mv

    if snapshot.car_voltage_mv is not None:
      if self._min_car_voltage_seen_mv is None or snapshot.car_voltage_mv < self._min_car_voltage_seen_mv:
        self._min_car_voltage_seen_mv = snapshot.car_voltage_mv

    if snapshot.device_temp_c is not None:
      if self._max_device_temp_seen_c is None or snapshot.device_temp_c > self._max_device_temp_seen_c:
        self._max_device_temp_seen_c = snapshot.device_temp_c

    line = self._format_log_line(snapshot, prefix=prefix)
    self._add_log(line)
    cloudlog.info(f"power supply load test sample: {line}")

  def _add_log(self, line: str):
    timestamp = datetime.datetime.now().strftime("%H:%M:%S")
    self._log_lines.appendleft(f"[{timestamp}] {line}")

  def _read_snapshot(self, elapsed_s: float) -> PowerSupplySnapshot:
    device_voltage_mv = None
    try:
      device_voltage_mv = int(HARDWARE.get_voltage())
      if device_voltage_mv <= 0:
        device_voltage_mv = None
    except Exception:
      device_voltage_mv = None

    return PowerSupplySnapshot(
      elapsed_s=elapsed_s,
      device_voltage_mv=device_voltage_mv,
      car_voltage_mv=self._read_car_voltage_mv(),
      device_temp_c=self._read_device_temp_c(),
      gpu_usage_percent=self._read_gpu_usage_percent(),
    )

  def _read_car_voltage_mv(self) -> int | None:
    panda_states = ui_state.sm["pandaStates"]
    for panda_state in panda_states:
      if panda_state.pandaType != log.PandaState.PandaType.unknown:
        return int(panda_state.voltage)
    return None

  def _read_device_temp_c(self) -> float | None:
    if not ui_state.sm.alive["deviceState"]:
      return None

    device_state = ui_state.sm["deviceState"]
    # Use the hottest reported internal component as the single device
    # temperature guard rail for the test.
    temps = [device_state.memoryTempC]
    temps.extend(device_state.cpuTempC)
    temps.extend(device_state.gpuTempC)
    temps.extend(device_state.pmicTempC)
    valid_temps = [float(t) for t in temps if t is not None]
    return max(valid_temps, default=None)

  def _read_gpu_usage_percent(self) -> int | None:
    if not ui_state.sm.alive["deviceState"]:
      return None
    return int(ui_state.sm["deviceState"].gpuUsagePercent)

  def _format_log_line(self, snapshot: PowerSupplySnapshot, prefix: str | None = None) -> str:
    parts = []
    if prefix:
      parts.append(prefix)
    else:
      parts.append(f"t={snapshot.elapsed_s:4.1f}s")

    parts.append(f"device={self._format_voltage(snapshot.device_voltage_mv)}")
    parts.append(f"car={self._format_voltage(snapshot.car_voltage_mv)}")
    parts.append(f"temp={self._format_temp(snapshot.device_temp_c)}")
    parts.append(f"gpu={self._format_percent(snapshot.gpu_usage_percent)}")
    return "  ".join(parts)

  def _render(self, rect: rl.Rectangle):
    if self._phase == LoadTestPhase.RUNNING:
      self._render_stress_background(rect)
    else:
      self._render_idle_background(rect)

    rl.draw_rectangle(int(rect.x), int(rect.y), int(rect.width), int(rect.height), rl.Color(0, 0, 0, 120))

    title_rect = rl.Rectangle(rect.x + PANEL_MARGIN, rect.y + PANEL_MARGIN, rect.width - 2 * PANEL_MARGIN, TITLE_FONT_SIZE * FONT_SCALE)
    title_font = gui_app.font(FontWeight.BOLD)
    rl.draw_text_ex(title_font, "Power Supply Load Test", rl.Vector2(title_rect.x, title_rect.y), TITLE_FONT_SIZE, 0, rl.WHITE)

    status_y = title_rect.y + TITLE_FONT_SIZE * FONT_SCALE + 16
    status_color = STATUS_COLORS[self._status_key]
    rl.draw_text_ex(gui_app.font(FontWeight.MEDIUM), self._status_text, rl.Vector2(title_rect.x, status_y), 58, 0, status_color)

    if self._phase == LoadTestPhase.FAILED and self._failure_reason:
      detail_lines = wrap_text(gui_app.font(FontWeight.NORMAL), self._failure_reason, BODY_FONT_SIZE, int(rect.width - 2 * PANEL_MARGIN))
      for i, line in enumerate(detail_lines[:2]):
        rl.draw_text_ex(gui_app.font(FontWeight.NORMAL), line, rl.Vector2(title_rect.x, status_y + 78 + i * BODY_FONT_SIZE * FONT_SCALE), BODY_FONT_SIZE, 0, rl.WHITE)

    progress_y = rect.y + (355 if self._phase == LoadTestPhase.FAILED and self._failure_reason else 265)
    progress_rect = rl.Rectangle(rect.x + PANEL_MARGIN, progress_y, rect.width - 2 * PANEL_MARGIN, PROGRESS_HEIGHT)
    self._render_progress_bar(progress_rect)

    panels_y = progress_rect.y + PROGRESS_HEIGHT + 35
    left_panel = rl.Rectangle(rect.x + PANEL_MARGIN, panels_y, (rect.width - PANEL_MARGIN * 3) * 0.47, rect.height - (panels_y - rect.y) - 140)
    right_panel = rl.Rectangle(left_panel.x + left_panel.width + PANEL_MARGIN, left_panel.y, rect.width - (left_panel.width + PANEL_MARGIN * 3), left_panel.height)
    self._draw_panel(left_panel, rl.Color(18, 18, 18, 220))
    self._draw_panel(right_panel, rl.Color(18, 18, 18, 220))

    self._render_metrics(left_panel)
    self._render_log_panel(right_panel)

    self._continue_button.set_visible(self._phase in (LoadTestPhase.PASSED, LoadTestPhase.FAILED))
    if self._continue_button.is_visible:
      self._continue_button.render(rl.Rectangle(rect.x + rect.width - PANEL_MARGIN - 460, rect.y + rect.height - PANEL_MARGIN - 150, 460, 150))
    return -1

  def _render_progress_bar(self, rect: rl.Rectangle):
    self._draw_panel(rect, rl.Color(34, 34, 34, 230), roundness=0.4)
    progress = 0.0
    if self._phase == LoadTestPhase.PASSED:
      progress = 1.0
    elif self._phase == LoadTestPhase.RUNNING and self._start_time is not None:
      progress = min((time.monotonic() - self._start_time) / POWER_SUPPLY_LOAD_TEST_DURATION_S, 1.0)
    elif self._phase == LoadTestPhase.FAILED and self._start_time is not None:
      progress = min((time.monotonic() - self._start_time) / POWER_SUPPLY_LOAD_TEST_DURATION_S, 1.0)

    fill_width = max(rect.width * progress, 0)
    if fill_width > 0:
      color = STATUS_COLORS["running"] if self._phase == LoadTestPhase.RUNNING else STATUS_COLORS[self._status_key]
      fill_rect = rl.Rectangle(rect.x, rect.y, fill_width, rect.height)
      self._draw_panel(fill_rect, color, roundness=0.4)

  def _render_metrics(self, rect: rl.Rectangle):
    title_x = rect.x + 44
    title_y = rect.y + 36
    rl.draw_text_ex(gui_app.font(FontWeight.MEDIUM), "Live measurements", rl.Vector2(title_x, title_y), 52, 0, rl.WHITE)

    snapshot = self._latest_snapshot or self._baseline
    elapsed_s = snapshot.elapsed_s if snapshot is not None else 0.0
    metrics = [
      ("Elapsed", f"{elapsed_s:.1f} s / {POWER_SUPPLY_LOAD_TEST_DURATION_S:.0f} s"),
      ("Device voltage", self._format_voltage(snapshot.device_voltage_mv if snapshot else None)),
      ("Min device voltage", self._format_voltage(self._min_device_voltage_seen_mv)),
      ("Car voltage", self._format_voltage(snapshot.car_voltage_mv if snapshot else None)),
      ("Min car voltage", self._format_voltage(self._min_car_voltage_seen_mv)),
      ("Device temperature", self._format_temp(snapshot.device_temp_c if snapshot else None)),
      ("Max device temperature", self._format_temp(self._max_device_temp_seen_c)),
      ("GPU usage", self._format_percent(snapshot.gpu_usage_percent if snapshot else None)),
      ("Voltage limit", self._format_voltage(self._min_device_voltage_limit_mv)),
      ("Temperature limit", "ignored" if self._max_temp_limit_c < 0 else f"{self._max_temp_limit_c} C"),
    ]

    label_font = gui_app.font(FontWeight.NORMAL)
    value_font = gui_app.font(FontWeight.MEDIUM)
    label_font_size = 38
    value_font_size = 50
    column_width = (rect.width - 120) / 2
    row_height = 80
    row_y = title_y + 94
    for i, (label, value) in enumerate(metrics):
      column = i % 2
      row = i // 2
      x = title_x + column * column_width
      y = row_y + row * row_height
      rl.draw_text_ex(label_font, label, rl.Vector2(x, y), label_font_size, 0, rl.Color(180, 180, 180, 255))
      rl.draw_text_ex(value_font, value, rl.Vector2(x, y + 24), value_font_size, 0, rl.WHITE)

  def _render_log_panel(self, rect: rl.Rectangle):
    title_x = rect.x + 44
    title_y = rect.y + 36
    rl.draw_text_ex(gui_app.font(FontWeight.MEDIUM), "Sample log", rl.Vector2(title_x, title_y), 52, 0, rl.WHITE)

    log_y = title_y + 88
    available_width = int(rect.width - 88)
    for raw_line in self._log_lines:
      wrapped_lines = wrap_text(gui_app.font(FontWeight.NORMAL), raw_line, LOG_FONT_SIZE, available_width)
      for line in wrapped_lines:
        if log_y > rect.y + rect.height - 50:
          return
        rl.draw_text_ex(gui_app.font(FontWeight.NORMAL), line, rl.Vector2(title_x, log_y), LOG_FONT_SIZE, 0, rl.Color(215, 215, 215, 255))
        log_y += LOG_FONT_SIZE * FONT_SCALE
      log_y += 14

  def _render_stress_background(self, rect: rl.Rectangle):
    if self._shader is None:
      self._render_idle_background(rect)
      return

    # Rendering the full-screen shader every frame is the artificial GPU load.
    self._shader_clock_ptr[0] = float(time.monotonic())
    self._shader_resolution_ptr[0] = rect.width
    self._shader_resolution_ptr[1] = rect.height
    rl.set_shader_value(self._shader, self._shader_clock_loc, self._shader_clock_ptr, rl.ShaderUniformDataType.SHADER_UNIFORM_FLOAT)
    rl.set_shader_value(self._shader, self._shader_resolution_loc, self._shader_resolution_ptr, rl.ShaderUniformDataType.SHADER_UNIFORM_VEC2)

    rl.begin_shader_mode(self._shader)
    rl.draw_rectangle(int(rect.x), int(rect.y), int(rect.width), int(rect.height), rl.WHITE)
    rl.end_shader_mode()

    for i in range(14):
      alpha = max(16, 70 - i * 4)
      inset = i * 26
      rl.draw_rectangle_lines_ex(
        rl.Rectangle(rect.x + inset, rect.y + inset, rect.width - inset * 2, rect.height - inset * 2),
        3,
        rl.Color(255, 255 - i * 10, 180 - i * 8, alpha),
      )

  def _render_idle_background(self, rect: rl.Rectangle):
    rl.draw_rectangle_gradient_v(
      int(rect.x),
      int(rect.y),
      int(rect.width),
      int(rect.height),
      rl.Color(24, 28, 44, 255),
      rl.Color(10, 11, 18, 255),
    )

  @staticmethod
  def _draw_panel(rect: rl.Rectangle, color: rl.Color, roundness: float = PANEL_RADIUS):
    rl.draw_rectangle_rounded(rect, roundness, 12, color)

  @staticmethod
  def _format_voltage(voltage_mv: int | None) -> str:
    return "unavailable" if voltage_mv is None else f"{voltage_mv / 1000.0:.2f} V"

  @staticmethod
  def _format_temp(temp_c: float | None) -> str:
    return "unavailable" if temp_c is None else f"{temp_c:.1f} C"

  @staticmethod
  def _format_percent(value: int | None) -> str:
    return "unavailable" if value is None else f"{value}%"
