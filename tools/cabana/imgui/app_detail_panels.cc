#include "tools/cabana/imgui/app.h"
#include "tools/cabana/imgui/app_util.h"
#include "tools/cabana/imgui/app_video_state.h"
#include "tools/cabana/imgui/icons.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

#include "imgui.h"
#include "implot.h"
#include "tools/cabana/imgui/commands.h"
#include "tools/cabana/imgui/dbcmanager.h"
#include "tools/cabana/imgui/settings.h"
#include "tools/cabana/imgui/stream.h"
#include "tools/cabana/imgui/replaystream.h"
#include "tools/cabana/imgui/export.h"
#include "tools/cabana/imgui/util.h"
#include "tools/cabana/core/workspace_state.h"
#include "tools/replay/replay.h"
#include "tools/replay/timeline.h"

void CabanaImguiApp::drawLogsPanel(const ImVec2 &size) {
  IM_UNUSED(size);
  const cabana::Msg *msg = selectedDbcMessage();
  std::vector<cabana::Signal *> msg_signals;
  if (msg) {
    msg_signals = msg->getSignals();
    logs_filter_signal_idx_ = std::clamp(logs_filter_signal_idx_, 0, std::max(0, static_cast<int>(msg_signals.size()) - 1));
  }
  // Force hex mode when no DBC message or message has no signals (matches Qt: historylog falls back to hex)
  const bool force_hex = !msg || msg_signals.empty();

  if (ImGui::Button("Export CSV")) {
    const std::string base_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
    const std::string route_name = stream_ ? stream_->routeName() : "cabana";
    const std::string msg_name = msgName(selected_id_);
    const std::string default_name = base_dir + "/" + route_name + "_" + msg_name + ".csv";
    const bool hex = logs_hex_mode_ || force_hex;
    MessageId export_id = selected_id_;
    auto do_export = [this, hex, export_id](const std::string &fn) {
      hex ? utils::exportToCSV(fn, export_id) : utils::exportSignalsToCSV(fn, export_id);
      settings.last_dir = pathDirname(fn);
      setStatusMessage("CSV exported: " + pathBasename(fn));
    };
    if (hasNativeFileDialogs()) {
      const std::string fn = nativeSaveFileDialog("Export " + msg_name + " to CSV file", default_name, "CSV", "*.csv");
      if (!fn.empty()) do_export(fn);
    } else {
      showImguiSaveDialog("Export " + msg_name + " to CSV file", base_dir, route_name + "_" + msg_name + ".csv", ".csv", do_export);
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(force_hex);
  ImGui::Checkbox("Hex mode", &logs_hex_mode_);
  ImGui::EndDisabled();
  const bool effective_hex = logs_hex_mode_ || force_hex;
  if (!effective_hex && !msg_signals.empty()) {
    ImGui::SameLine();
    ImGui::SetNextItemWidth(170.0f);
    const char *signal_preview = msg_signals[logs_filter_signal_idx_]->name.c_str();
    if (ImGui::BeginCombo("##log_signal", signal_preview)) {
      for (int i = 0; i < static_cast<int>(msg_signals.size()); ++i) {
        const bool selected = logs_filter_signal_idx_ == i;
        if (ImGui::Selectable(msg_signals[i]->name.c_str(), selected)) logs_filter_signal_idx_ = i;
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    const char *comparators[] = {">", "=", "!=", "<"};
    ImGui::SetNextItemWidth(62.0f);
    ImGui::Combo("##log_cmp", &logs_compare_idx_, comparators, IM_ARRAYSIZE(comparators));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::InputTextWithHint("##log_filter", "value", logs_filter_text_.data(), logs_filter_text_.size());
  }
  ImGui::SameLine();
  {
    const auto &ev = stream_->events(selected_id_);
    const uint64_t mono = stream_->toMonoTime(stream_->currentSec());
    auto eit = std::upper_bound(ev.cbegin(), ev.cend(), mono, CompareCanEvent());
    ImGui::TextDisabled("%d events", static_cast<int>(std::distance(ev.cbegin(), eit)));
  }
  ImGui::Separator();

  ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable;
  if (effective_hex) {
    if (ImGui::BeginTable("LogsHex", 2, flags, ImVec2(0, 0))) {
      ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 84.0f);
      ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();

      const auto &all_events = stream_->events(selected_id_);
      // Filter to events up to current playback time (matching Qt's seek-anchored behavior)
      const double cur_sec = stream_->currentSec();
      const uint64_t cur_mono = stream_->toMonoTime(cur_sec);
      auto end_it = std::upper_bound(all_events.cbegin(), all_events.cend(), cur_mono, CompareCanEvent());
      const int total = static_cast<int>(std::distance(all_events.cbegin(), end_it));
      ImGuiListClipper clipper;
      clipper.Begin(total);
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          const CanEvent *event = all_events[total - 1 - row];  // newest first
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%.3f", stream_->toSeconds(event->mono_time));
          ImGui::TableSetColumnIndex(1);
          // Color-coded hex bytes in logs (compute change colors vs previous event)
          const int prev_idx = total - 1 - row + 1;
          const CanEvent *prev_event = (prev_idx >= 0 && prev_idx < total) ? all_events[prev_idx] : nullptr;
          for (size_t b = 0; b < event->size; ++b) {
            if (b > 0) { ImGui::SameLine(0, 0); ImGui::TextUnformatted(" "); ImGui::SameLine(0, 0); }
            char hex[4];
            std::snprintf(hex, sizeof(hex), "%02X", event->dat[b]);
            if (prev_event && b < prev_event->size && event->dat[b] != prev_event->dat[b]) {
              // Byte changed: blue for increase, red for decrease (matches Qt CanData::compute)
              if (event->dat[b] > prev_event->dat[b]) {
                ImGui::TextColored(ImVec4(0.25f, 0.45f, 1.0f, 1.0f), "%s", hex);
              } else {
                ImGui::TextColored(ImVec4(0.9f, 0.2f, 0.2f, 1.0f), "%s", hex);
              }
            } else if (prev_event && b < prev_event->size && event->dat[b] == prev_event->dat[b]) {
              // Constant byte: gray (matches Qt CanData::compute for unchanged bytes)
              ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 1.0f), "%s", hex);
            } else {
              ImGui::TextUnformatted(hex);
            }
          }
        }
      }
      ImGui::EndTable();
    }
    return;
  }

  bool has_filter = logs_filter_text_[0] != '\0';
  double filter_value = 0.0;
  if (has_filter) {
    char *end = nullptr;
    filter_value = std::strtod(logs_filter_text_.data(), &end);
    // Reject non-numeric or partially-numeric input (matching Qt's DoubleValidator which
    // rejects strings like "1abc" — the entire string must be a valid number)
    if (end == logs_filter_text_.data() || (*end != '\0' && !std::isspace(static_cast<unsigned char>(*end)))) {
      has_filter = false;
    }
  }

  if (ImGui::BeginTable("LogsSignal", static_cast<int>(msg_signals.size()) + 1, flags, ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 84.0f);
    for (const auto *sig : msg_signals) {
      std::string hdr = sig->name;
      if (!sig->unit.empty()) hdr += " (" + sig->unit + ")";
      ImGui::TableSetupColumn(hdr.c_str(), ImGuiTableColumnFlags_WidthFixed, 110.0f);
    }
    // Custom header row with signal-colored backgrounds (matching Qt)
    ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
    ImGui::TableSetColumnIndex(0);
    ImGui::TableHeader("Time");
    for (int col = 0; col < (int)msg_signals.size(); ++col) {
      ImGui::TableSetColumnIndex(col + 1);
      const auto &sc = msg_signals[col]->color;
      ImVec4 bg(sc.red() / 255.0f, sc.green() / 255.0f, sc.blue() / 255.0f, 0.5f);
      ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(bg));
      std::string hdr = msg_signals[col]->name;
      if (!msg_signals[col]->unit.empty()) hdr += " (" + msg_signals[col]->unit + ")";
      ImGui::TableHeader(hdr.c_str());
    }

    const auto &events = stream_->events(selected_id_);
    // Filter to events up to current playback time
    const double cur_sec_log = stream_->currentSec();
    const uint64_t cur_mono_log = stream_->toMonoTime(cur_sec_log);
    auto end_it_log = std::upper_bound(events.cbegin(), events.cend(), cur_mono_log, CompareCanEvent());
    const int event_count_log = static_cast<int>(std::distance(events.cbegin(), end_it_log));
    // Cached filtered index list - rebuild when events, filter, stream, DBC definitions, or DBC file set change
    static MessageId cached_msg_id;
    static const void *cached_stream = nullptr;
    static int cached_event_count = 0;
    static uint64_t cached_last_mono = 0;
    static int cached_filter_sig = -1;
    static int cached_compare = -1;
    static int cached_undo_idx = -1;
    static int cached_dbc_count = -1;
    static size_t cached_dbc_sig_count = 0;  // DBC content fingerprint: detect swaps with same dbcCount
    static std::string cached_filter_text;
    static std::vector<int> filtered_indices;
    const std::string cur_filter(logs_filter_text_.data());
    const int cur_undo_idx = UndoStack::instance()->index();
    const int cur_dbc_count = dbc()->dbcCount();
    // DBC content fingerprint: use signal count for the selected message as a cheap proxy for DBC identity.
    // A DBC swap that replaces messages with different signal definitions will change this.
    const auto *cur_msg_def = dbc()->msg(selected_id_);
    const size_t cur_dbc_sig_count = cur_msg_def ? cur_msg_def->sigs.size() : 0;
    // Detect middle-insertion from replay merges: if the event at our old boundary changed, force rebuild
    const uint64_t cur_last_mono = (cached_event_count > 0 && cached_event_count <= static_cast<int>(events.size()))
      ? events[cached_event_count - 1]->mono_time : 0;
    const bool middle_insert = cached_event_count > 0 && cached_last_mono != 0 && cur_last_mono != cached_last_mono;
    // Detect whether we need a full rebuild or can incrementally append new events
    const bool full_rebuild = cached_msg_id != selected_id_ || cached_stream != stream_ ||
                              cached_filter_sig != logs_filter_signal_idx_ || cached_compare != logs_compare_idx_ ||
                              cached_filter_text != cur_filter || cached_undo_idx != cur_undo_idx ||
                              cached_dbc_count != cur_dbc_count || cached_dbc_sig_count != cur_dbc_sig_count ||
                              event_count_log < cached_event_count || middle_insert;
    const bool incremental = !full_rebuild && event_count_log > cached_event_count;
    if (full_rebuild || incremental) {
      const int scan_start = event_count_log - 1;
      const int scan_end = full_rebuild ? 0 : cached_event_count;  // incremental: only scan new events
      if (full_rebuild) {
        filtered_indices.clear();
        filtered_indices.reserve(event_count_log);
      }
      cached_msg_id = selected_id_;
      cached_stream = stream_;
      cached_filter_sig = logs_filter_signal_idx_;
      cached_compare = logs_compare_idx_;
      cached_undo_idx = cur_undo_idx;
      cached_dbc_count = cur_dbc_count;
      cached_dbc_sig_count = cur_dbc_sig_count;
      cached_filter_text = cur_filter;
      cached_event_count = event_count_log;
      cached_last_mono = (event_count_log > 0 && event_count_log <= static_cast<int>(events.size()))
        ? events[event_count_log - 1]->mono_time : 0;
      // For incremental, collect new matching indices to prepend (newest first)
      std::vector<int> new_indices;
      for (int idx = scan_start; idx >= scan_end; --idx) {
        const CanEvent *event = events[idx];
        if (has_filter) {
          double value = 0.0;
          if (!msg_signals[logs_filter_signal_idx_]->getValue(event->dat, event->size, &value)) continue;
          const bool matches = [&]() {
            switch (logs_compare_idx_) {
              case 0: return value > filter_value;
              case 1: return std::abs(value - filter_value) < 1e-6;
              case 2: return std::abs(value - filter_value) >= 1e-6;
              case 3: return value < filter_value;
              default: return true;
            }
          }();
          if (!matches) continue;
        }
        if (full_rebuild) {
          filtered_indices.push_back(idx);
        } else {
          new_indices.push_back(idx);
        }
      }
      if (incremental && !new_indices.empty()) {
        filtered_indices.insert(filtered_indices.begin(), new_indices.begin(), new_indices.end());
      }
    }

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(filtered_indices.size()));
    while (clipper.Step()) {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        const CanEvent *event = events[filtered_indices[row]];
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%.3f", stream_->toSeconds(event->mono_time));
        for (int i = 0; i < static_cast<int>(msg_signals.size()); ++i) {
          ImGui::TableSetColumnIndex(i + 1);
          double value = 0.0;
          if (msg_signals[i]->getValue(event->dat, event->size, &value)) {
            ImGui::TextUnformatted(msg_signals[i]->formatValue(value, false).c_str());
          } else {
            ImGui::TextDisabled("-");
          }
        }
      }
    }
    ImGui::EndTable();
  }
}

void CabanaImguiApp::drawChartPanel(const ImVec2 &size) {
  ImGui::BeginChild("ChartPanel", size, ImGuiChildFlags_Borders);
  ensureChartTabs();

  constexpr ImGuiMouseButton kChartZoomButton = ImGuiMouseButton_Left;
  constexpr float kMinChartZoomDragPx = 10.0f;
  constexpr double kMinChartZoomSeconds = 0.01;
  struct ScopedQtChartInputMap {
    ImPlotInputMap previous;
    ScopedQtChartInputMap() : previous(ImPlot::GetInputMap()) {
      ImPlotInputMap qt_map = previous;
      qt_map.Pan = ImGuiMouseButton_Middle;
      qt_map.PanMod = ImGuiMod_None;
      qt_map.Fit = ImGuiMouseButton_Middle;
      qt_map.Select = kChartZoomButton;
      qt_map.SelectCancel = ImGuiMouseButton_Middle;
      qt_map.SelectMod = ImGuiMod_None;
      qt_map.SelectHorzMod = ImGuiMod_Alt;
      qt_map.SelectVertMod = ImGuiMod_None;  // Qt used a horizontal rubber-band zoom.
      qt_map.Menu = ImGuiMouseButton_Right;
      ImPlot::GetInputMap() = qt_map;
    }
    ~ScopedQtChartInputMap() { ImPlot::GetInputMap() = previous; }
  } qt_chart_input_map;

  // === Toolbar ===
  // Note: do NOT cache currentCharts() reference here — tab mutations below can invalidate it
  ImGui::TextUnformatted("Charts");
  ImGui::SameLine();
  ImGui::TextDisabled("%d", static_cast<int>(currentCharts().size()));
  ImGui::SameLine();
  const char *series_types[] = {"Line", "Step", "Scatter"};
  ImGui::SetNextItemWidth(86.0f);
  int global_type = currentCharts().empty() ? settings.chart_series_type : currentCharts().front().series_type;
  if (ImGui::Combo("##series_type", &global_type, series_types, IM_ARRAYSIZE(series_types))) {
    // Apply globally across all tabs (matching Qt behavior)
    for (auto &tab : chart_tabs_) {
      for (auto &c : tab.charts) c.series_type = global_type;
    }
    settings.chart_series_type = global_type;
  }
  ImGui::SameLine();
  for (int col = 1; col <= 4; ++col) {
    if (col > 1) ImGui::SameLine();
    char col_label[8];
    std::snprintf(col_label, sizeof(col_label), "%d", col);
    if (chart_columns_ == col) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyle().Colors[ImGuiCol_ButtonActive]);
      ImGui::SmallButton(col_label);
      ImGui::PopStyleColor();
    } else {
      if (ImGui::SmallButton(col_label)) chart_columns_ = col;
    }
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(chart_zoom_history_.empty());
  if (smallIconButton("##undo_zoom", BootstrapIcon::ArrowCounterclockwise, "Undo Zoom")) {
    if (!chart_zoom_history_.empty()) {
      chart_zoom_redo_.push_back(chart_range_);
      chart_range_ = chart_zoom_history_.back();
      chart_zoom_history_.pop_back();
      if (chart_range_) stream_->setTimeRange(chart_range_);
      else stream_->setTimeRange(std::nullopt);
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(chart_zoom_redo_.empty());
  if (smallIconButton("##redo_zoom", BootstrapIcon::ArrowClockwise, "Redo Zoom")) {
    if (!chart_zoom_redo_.empty()) {
      chart_zoom_history_.push_back(chart_range_);
      chart_range_ = chart_zoom_redo_.back();
      chart_zoom_redo_.pop_back();
      if (chart_range_) stream_->setTimeRange(chart_range_);
      else stream_->setTimeRange(std::nullopt);
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (smallIconButton("##reset_zoom", BootstrapIcon::ZoomOut, "Reset Zoom")) {
    chart_zoom_history_.clear();
    chart_zoom_redo_.clear();
    resetChartRange();
  }
  // Wrap to next line if toolbar is getting too wide
  if (ImGui::GetCursorPosX() + 200 > ImGui::GetContentRegionMax().x) {
    // Don't SameLine — start a new row
  } else {
    ImGui::SameLine();
  }
  if (smallIconButton("##new_chart", BootstrapIcon::Plus, "New Chart")) {
    // Open signal selector to create a new chart
    signal_selector_chart_id_ = -1;  // -1 means "create new chart"
    signal_selector_selected_.clear();
    signal_selector_msg_idx_ = -1;
    signal_selector_filter_ = {};
    signal_selector_avail_idx_ = -1;
    signal_selector_sel_idx_ = -1;
    show_signal_selector_ = true;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(selectedSignal() == nullptr || !has_selected_id_);
  if (smallIconButton("##add_signal", BootstrapIcon::GraphUp, "Add Signal")) showChart(selected_id_, selectedSignal(), true, false);
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (smallIconButton("##dock_toggle", charts_floating_ ? BootstrapIcon::DashSquare : BootstrapIcon::WindowStack, charts_floating_ ? "Dock" : "Pop Out")) {
    charts_floating_ = !charts_floating_;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(currentCharts().empty());
  if (smallIconButton("##remove_all", BootstrapIcon::XLg, "Remove All")) removeAllCharts();
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (chart_range_) {
    ImGui::TextDisabled("%.2f - %.2f", chart_range_->first, chart_range_->second);
  } else {
    ImGui::SetNextItemWidth(100.0f);
    // Logarithmic slider matching Qt's LogSlider (factor=1000): min 1s, log-scaled
    const int range_min = 1;
    const int range_max = settings.max_cached_minutes * 60;
    int range_val = settings.chart_range;
    if (ImGui::SliderInt("##follow_range", &range_val, range_min, range_max, "%d s", ImGuiSliderFlags_Logarithmic)) {
      settings.chart_range = range_val;
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Follow");
  }

  // === Tab Bar ===
  pushQtTabBarStyle();
  if (ImGui::BeginTabBar("chart_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_Reorderable)) {
    int remove_tab = -1, dup_tab = -1, close_other_tab = -1;
    for (int i = 0; i < static_cast<int>(chart_tabs_.size()); ++i) {
      bool open = true;
      std::string label = "Tab " + std::to_string(i + 1) + " (" + std::to_string(chart_tabs_[i].charts.size()) + ")";
      ImGuiTabItemFlags flags = active_chart_tab_ == i ? ImGuiTabItemFlags_SetSelected : 0;
      if (ImGui::BeginTabItem(label.c_str(), &open, flags)) {
        active_chart_tab_ = i;
        ImGui::EndTabItem();
      }
      if (ImGui::BeginPopupContextItem(("chart_tab_ctx_" + std::to_string(chart_tabs_[i].id)).c_str())) {
        if (ImGui::MenuItem("Duplicate Tab")) dup_tab = i;
        if (ImGui::MenuItem("Close Other Tabs", nullptr, false, chart_tabs_.size() > 1)) close_other_tab = i;
        if (ImGui::MenuItem("Close Tab", nullptr, false, chart_tabs_.size() > 1)) remove_tab = i;
        ImGui::EndPopup();
      }
      if (!open && chart_tabs_.size() > 1) remove_tab = i;
    }
    if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing)) {
      chart_tabs_.push_back(ChartTabState{.id = next_chart_tab_id_++});
      active_chart_tab_ = static_cast<int>(chart_tabs_.size()) - 1;
    }
    if (dup_tab >= 0) {
      ChartTabState dup = chart_tabs_[dup_tab];
      dup.id = next_chart_tab_id_++;
      for (auto &c : dup.charts) c.id = next_chart_id_++;
      chart_tabs_.insert(chart_tabs_.begin() + dup_tab + 1, std::move(dup));
      active_chart_tab_ = dup_tab + 1;
    }
    if (close_other_tab >= 0) {
      ChartTabState keep = std::move(chart_tabs_[close_other_tab]);
      chart_tabs_.clear();
      chart_tabs_.push_back(std::move(keep));
      active_chart_tab_ = 0;
    }
    if (remove_tab >= 0) {
      chart_tabs_.erase(chart_tabs_.begin() + remove_tab);
      active_chart_tab_ = std::clamp(active_chart_tab_, 0, static_cast<int>(chart_tabs_.size()) - 1);
    }
    ImGui::EndTabBar();
  }
  popQtTabBarStyle();
  ImGui::Separator();

  // Safe to cache reference now — tab mutations above are done
  auto &charts = currentCharts();

  if (charts.empty()) {
    ImGui::TextDisabled("No charts. Use 'Add Signal' or enable signals to plot.");
    ImGui::EndChild();
    return;
  }

  double hover_sec_this_frame = -1.0;

  const auto live_chart_display_range = currentChartDisplayRange();
  double x_min = chart_timeline_zoom_drag_.active ? chart_timeline_zoom_drag_.range_min : live_chart_display_range.first;
  double x_max = chart_timeline_zoom_drag_.active ? chart_timeline_zoom_drag_.range_max : live_chart_display_range.second;

  const ImVec2 timeline_pos = ImGui::GetCursorScreenPos();
  const ImVec2 timeline_size(ImGui::GetContentRegionAvail().x, 14.0f);
  ImGui::InvisibleButton("chart_timeline", timeline_size);
  const bool timeline_hovered = ImGui::IsItemHovered();
  if (timeline_hovered && ImGui::IsMouseClicked(kChartZoomButton)) {
    chart_timeline_zoom_drag_.active = true;
    chart_timeline_zoom_drag_.start_x = std::clamp(ImGui::GetIO().MousePos.x, timeline_pos.x, timeline_pos.x + timeline_size.x);
    chart_timeline_zoom_drag_.min_x = timeline_pos.x;
    chart_timeline_zoom_drag_.max_x = timeline_pos.x + timeline_size.x;
    chart_timeline_zoom_drag_.range_min = live_chart_display_range.first;
    chart_timeline_zoom_drag_.range_max = live_chart_display_range.second;
    x_min = chart_timeline_zoom_drag_.range_min;
    x_max = chart_timeline_zoom_drag_.range_max;
  }
  const bool timeline_drag_active = chart_timeline_zoom_drag_.active;
  const double timeline_hover_sec = (timeline_hovered || timeline_drag_active)
                                     ? timelineSecFromMouseX(x_min, x_max, timeline_pos.x, timeline_size.x, ImGui::GetIO().MousePos.x)
                                     : -1.0;
  drawTimelineStrip(ImGui::GetWindowDrawList(), timeline_pos, timeline_size, x_min, x_max, stream_->currentSec(),
                    std::nullopt, timeline_hover_sec >= 0 ? timeline_hover_sec : chart_hover_sec_);
  if (timeline_drag_active) {
    const float cur_x = std::clamp(ImGui::GetIO().MousePos.x, chart_timeline_zoom_drag_.min_x, chart_timeline_zoom_drag_.max_x);
    const float drag_width_px = std::abs(cur_x - chart_timeline_zoom_drag_.start_x);
    if (ImGui::IsMouseDown(kChartZoomButton) && drag_width_px > kMinChartZoomDragPx) {
      const float sel_min_x = std::min(chart_timeline_zoom_drag_.start_x, cur_x);
      const float sel_max_x = std::max(chart_timeline_zoom_drag_.start_x, cur_x);
      ImDrawList *overlay = ImGui::GetWindowDrawList();
      overlay->AddRectFilled(ImVec2(sel_min_x, timeline_pos.y), ImVec2(sel_max_x, timeline_pos.y + timeline_size.y), IM_COL32(180, 205, 230, 40));
      overlay->AddRect(ImVec2(sel_min_x, timeline_pos.y), ImVec2(sel_max_x, timeline_pos.y + timeline_size.y), IM_COL32(180, 205, 230, 180), 0.0f, 0, 1.0f);
    }
    if (ImGui::IsMouseReleased(kChartZoomButton)) {
      if (drag_width_px > kMinChartZoomDragPx) {
        const float sel_min_x = std::min(chart_timeline_zoom_drag_.start_x, cur_x);
        const float sel_max_x = std::max(chart_timeline_zoom_drag_.start_x, cur_x);
        const double zoom_min = timelineSecFromMouseX(chart_timeline_zoom_drag_.range_min, chart_timeline_zoom_drag_.range_max,
                                                      chart_timeline_zoom_drag_.min_x,
                                                      chart_timeline_zoom_drag_.max_x - chart_timeline_zoom_drag_.min_x,
                                                      sel_min_x);
        const double zoom_max = timelineSecFromMouseX(chart_timeline_zoom_drag_.range_min, chart_timeline_zoom_drag_.range_max,
                                                      chart_timeline_zoom_drag_.min_x,
                                                      chart_timeline_zoom_drag_.max_x - chart_timeline_zoom_drag_.min_x,
                                                      sel_max_x);
        if ((zoom_max - zoom_min) > kMinChartZoomSeconds) {
          updateChartRange((zoom_min + zoom_max) * 0.5, zoom_max - zoom_min);
        }
      } else if (timeline_hover_sec >= 0) {
        stream_->seekTo(timeline_hover_sec);
      }
      chart_timeline_zoom_drag_ = {};
      x_min = currentChartDisplayRange().first;
      x_max = currentChartDisplayRange().second;
    }
  }
  if (timeline_hovered || timeline_drag_active) hover_sec_this_frame = timeline_hover_sec;
  ImGui::Dummy(ImVec2(0.0f, 4.0f));

  const int undo_idx = UndoStack::instance()->index();

  // === Layout ===
  const int chart_count = static_cast<int>(charts.size());
  const int eff_columns = std::min(std::clamp(chart_columns_, 1, 4), chart_count);
  const int rows = (chart_count + eff_columns - 1) / eff_columns;
  const float gap = 4.0f * cabanaUiScale();
  const float cell_w = std::max(200.0f, (ImGui::GetContentRegionAvail().x - gap * (eff_columns - 1)) / eff_columns);
  const float cell_h = std::max(120.0f, static_cast<float>(settings.chart_height));

  int remove_chart_idx = -1;
  int split_chart_idx = -1;
  int manage_chart_idx = -1;
  int drag_src_idx = -1, drag_dst_idx = -1;
  bool drag_insert_after = false;
  std::vector<int> deferred_cache_invalidations;

  for (int ci = 0; ci < chart_count; ++ci) {
    auto &chart = charts[ci];
    if ((ci % eff_columns) != 0) ImGui::SameLine(0.0f, gap);

    // --- Build/validate per-chart cache ---
    // Cache stores FULL series data (all events). Only y-range is recomputed per visible window.
    auto &cache = chart_caches_[chart.id];
    size_t total_events = 0;
    for (const auto &ref : chart.series) total_events += stream_->events(ref.msg_id).size();
    const bool structure_valid = cache.chart_id == chart.id &&
                                 cache.series_refs == chart.series &&
                                 cache.undo_index == undo_idx;
    if (!structure_valid) {
      // Full rebuild: series refs changed or DBC edited
      cache = {};
      cache.chart_id = chart.id;
      cache.series_refs = chart.series;
      cache.total_event_count = total_events;
      cache.undo_index = undo_idx;
      for (int si = 0; si < static_cast<int>(chart.series.size()); ++si) {
        const auto &ref = chart.series[si];
        const auto *sig_msg = dbc()->msg(ref.msg_id);
        const auto *sig = sig_msg ? sig_msg->sig(ref.signal_name) : nullptr;
        if (!sig) continue;
        ChartSeriesCache::Series ps;
        ps.ref = ref;
        ps.color = sig->color;
        ps.unit = sig->unit;
        ps.y_min = DBL_MAX;
        ps.y_max = -DBL_MAX;
        ps.decoded_event_count = 0;
        for (const CanEvent *ev : stream_->events(ref.msg_id)) {
          const double ts = stream_->toSeconds(ev->mono_time);
          double value = 0.0;
          if (sig->getValue(ev->dat, ev->size, &value)) {
            ps.xs.push_back(ts);
            ps.ys.push_back(value);
          }
          ++ps.decoded_event_count;
        }
        const auto &evs = stream_->events(ref.msg_id);
        ps.last_decoded_mono_time = evs.empty() ? 0 : evs.back()->mono_time;
        cache.series.push_back(std::move(ps));
      }
    } else if (cache.total_event_count != total_events) {
      // Check if events were only appended (safe for incremental) or inserted in the middle (requires full rebuild).
      // Replay merges events by timestamp, so middle-insertion is common during segment loading.
      bool needs_full_rebuild = false;
      for (const auto &ps : cache.series) {
        const auto &events = stream_->events(ps.ref.msg_id);
        if (ps.decoded_event_count > 0 && ps.decoded_event_count <= events.size()) {
          // If events were inserted before our last decoded position, the event at our old index changed
          if (ps.last_decoded_mono_time != 0 && events[ps.decoded_event_count - 1]->mono_time != ps.last_decoded_mono_time) {
            needs_full_rebuild = true;
            break;
          }
        } else if (ps.decoded_event_count > events.size()) {
          needs_full_rebuild = true;
          break;
        }
      }
      if (needs_full_rebuild) {
        // Middle-insertion detected: do a full rebuild now (same as structure_valid=false path)
        cache = {};
        cache.chart_id = chart.id;
        cache.series_refs = chart.series;
        cache.total_event_count = total_events;
        cache.undo_index = undo_idx;
        for (int si2 = 0; si2 < static_cast<int>(chart.series.size()); ++si2) {
          const auto &ref2 = chart.series[si2];
          const auto *sig_msg2 = dbc()->msg(ref2.msg_id);
          const auto *sig2 = sig_msg2 ? sig_msg2->sig(ref2.signal_name) : nullptr;
          if (!sig2) continue;
          ChartSeriesCache::Series ps2;
          ps2.ref = ref2;
          ps2.color = sig2->color;
          ps2.unit = sig2->unit;
          ps2.y_min = DBL_MAX;
          ps2.y_max = -DBL_MAX;
          ps2.decoded_event_count = 0;
          for (const CanEvent *ev : stream_->events(ref2.msg_id)) {
            const double ts = stream_->toSeconds(ev->mono_time);
            double value = 0.0;
            if (sig2->getValue(ev->dat, ev->size, &value)) {
              ps2.xs.push_back(ts);
              ps2.ys.push_back(value);
            }
            ++ps2.decoded_event_count;
          }
          const auto &evs2 = stream_->events(ref2.msg_id);
          ps2.last_decoded_mono_time = evs2.empty() ? 0 : evs2.back()->mono_time;
          cache.series.push_back(std::move(ps2));
        }
      } else {
        // Incremental append: only decode new events at the tail
        cache.total_event_count = total_events;
        for (auto &ps : cache.series) {
          const auto &events = stream_->events(ps.ref.msg_id);
          const auto *sig_msg = dbc()->msg(ps.ref.msg_id);
          const auto *sig = sig_msg ? sig_msg->sig(ps.ref.signal_name) : nullptr;
          if (!sig) continue;
          for (size_t ei = ps.decoded_event_count; ei < events.size(); ++ei) {
            const CanEvent *ev = events[ei];
            const double ts = stream_->toSeconds(ev->mono_time);
            double value = 0.0;
            if (sig->getValue(ev->dat, ev->size, &value)) {
              ps.xs.push_back(ts);
              ps.ys.push_back(value);
            }
          }
          ps.last_decoded_mono_time = events.empty() ? 0 : events.back()->mono_time;
          ps.decoded_event_count = events.size();
        }
      }
    }

    auto &series = cache.series;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImPlot::GetStyle().Colors[ImPlotCol_FrameBg]);
    ImGui::BeginChild(("ChartCell" + std::to_string(chart.id)).c_str(), ImVec2(cell_w, cell_h), ImGuiChildFlags_None);

    if (series.empty()) {
      ImGui::TextDisabled("No data");
      if (ImGui::BeginPopupContextWindow(("chart_ctx_" + std::to_string(chart.id)).c_str())) {
        if (ImGui::MenuItem("Close Chart")) remove_chart_idx = ci;
        ImGui::EndPopup();
      }
      ImGui::EndChild();
      ImGui::PopStyleColor();
      continue;
    }

    // --- Title/legend with current values at playback time ---
    ImGui::BeginGroup();  // Group the entire title bar for context menu hit-testing
    const double cur_sec = stream_ ? stream_->currentSec() : 0.0;
    for (int j = 0; j < static_cast<int>(series.size()); ++j) {
      const auto &ps = series[j];
      if (j > 0) ImGui::SameLine();
      const auto *msg_obj = dbc()->msg(ps.ref.msg_id);
      const auto *sig_obj = msg_obj ? msg_obj->sig(ps.ref.signal_name) : nullptr;
      // Hidden series get strikethrough-style dimming
      bool is_hidden = false;
      for (int si = 0; si < static_cast<int>(chart.series.size()); ++si) {
        if (chart.series[si] == ps.ref && si < static_cast<int>(chart.hidden.size()) && chart.hidden[si]) {
          is_hidden = true; break;
        }
      }
      std::string label = ps.ref.signal_name;
      if (series.size() > 1 && msg_obj) {
        char addr_buf[16];
        std::snprintf(addr_buf, sizeof(addr_buf), "0x%X", ps.ref.msg_id.address);
        label += " (" + msg_obj->name + ":" + std::string(addr_buf) + ")";
      }
      if (is_hidden) {
        ImGui::TextDisabled("[%s]", label.c_str());
      } else {
        ImGui::TextColored(imColor(ps.color), "%s", label.c_str());
      }
      // Show current value at playback time (last sample <= cur_sec)
      if (!is_hidden && sig_obj && !ps.xs.empty()) {
        auto vit = std::upper_bound(ps.xs.begin(), ps.xs.end(), cur_sec);
        int vidx = (vit == ps.xs.begin()) ? 0 : static_cast<int>(vit - ps.xs.begin()) - 1;
        ImGui::SameLine();
        ImGui::TextDisabled("%s", sig_obj->formatValue(ps.ys[vidx], false).c_str());
      }
    }
    // Per-chart action buttons (matching Qt's per-chart header controls)
    ImGui::SameLine(ImGui::GetContentRegionMax().x - 55.0f);
    if (smallIconButton(("##manage_" + std::to_string(chart.id)).c_str(), BootstrapIcon::Gear, "Manage Signals")) manage_chart_idx = ci;
    ImGui::SameLine();
    if (smallIconButton(("##close_" + std::to_string(chart.id)).c_str(), BootstrapIcon::X)) remove_chart_idx = ci;
    ImGui::EndGroup();

    // Chart context menu triggered from title/legend area (entire group)
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
      ImGui::OpenPopup(("chart_ctx_" + std::to_string(chart.id)).c_str());
    }

    // --- Y-axis range (derived from visible window using binary search on cached full data) ---
    double local_min = DBL_MAX, local_max = -DBL_MAX;
    for (int si = 0; si < static_cast<int>(series.size()); ++si) {
      bool hidden = si < static_cast<int>(chart.hidden.size()) && chart.hidden[si];
      if (hidden || series[si].xs.empty()) continue;
      // Binary search to find the visible x-range within the full cached data
      auto lo = std::lower_bound(series[si].xs.begin(), series[si].xs.end(), x_min);
      auto hi = std::upper_bound(series[si].xs.begin(), series[si].xs.end(), x_max);
      int lo_idx = static_cast<int>(lo - series[si].xs.begin());
      int hi_idx = static_cast<int>(hi - series[si].xs.begin());
      for (int k = lo_idx; k < hi_idx; ++k) {
        local_min = std::min(local_min, series[si].ys[k]);
        local_max = std::max(local_max, series[si].ys[k]);
      }
    }
    // Fallback when no visible data exists (all hidden or empty range)
    if (local_min > local_max) { local_min = 0.0; local_max = 0.0; }
    if (std::abs(local_max - local_min) < 1e-3) {
      local_min -= 1.0;
      local_max += 1.0;
    } else {
      double delta = (local_max - local_min) * 0.05;
      double range = local_max - local_min + 2.0 * delta;
      auto niceNumber = [](double x, bool ceiling) {
        double z = std::pow(10, std::floor(std::log10(x)));
        double q = x / z;
        if (ceiling) { q = q <= 1.0 ? 1 : q <= 2.0 ? 2 : q <= 5.0 ? 5 : 10; }
        else { q = q < 1.5 ? 1 : q < 3.0 ? 2 : q < 7.0 ? 5 : 10; }
        return q * z;
      };
      double nice_range = niceNumber(range, true);
      double step = niceNumber(nice_range / 2.0, false);
      local_min = std::floor((local_min - delta) / step) * step;
      local_max = std::ceil((local_max + delta) / step) * step;
    }

    // Y-axis unit: show if all visible series share the same unit
    // Keep y_unit_str alive through the plot call to avoid dangling pointer
    std::string y_unit_str;
    {
      auto isSeriesHidden = [&](int si) -> bool {
        if (si >= static_cast<int>(chart.hidden.size())) return false;
        return chart.hidden[si];
      };
      std::string first_visible_unit;
      bool found_first = false;
      bool all_same = true;
      for (int si = 0; si < static_cast<int>(series.size()); ++si) {
        if (isSeriesHidden(si)) continue;
        if (!found_first) { first_visible_unit = series[si].unit; found_first = true; }
        else if (series[si].unit != first_visible_unit) { all_same = false; break; }
      }
      if (found_first && all_same && !first_visible_unit.empty()) y_unit_str = first_visible_unit;
    }
    const char *y_unit = y_unit_str.empty() ? nullptr : y_unit_str.c_str();

    // --- ImPlot ---
      ImPlotFlags plot_flags = ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect | ImPlotFlags_NoMouseText;
      if (series.size() <= 1) plot_flags |= ImPlotFlags_NoLegend;
      if (ImPlot::BeginPlot(("##chart_" + std::to_string(chart.id)).c_str(), ImVec2(-1, -1), plot_flags)) {
      ImPlotAxisFlags x_flags = rows > 1 && ci < chart_count - eff_columns ? ImPlotAxisFlags_NoTickLabels : ImPlotAxisFlags_None;
      ImPlot::SetupAxes(ci >= chart_count - eff_columns ? "Time (s)" : nullptr, y_unit, x_flags, ImPlotAxisFlags_None);
      ImPlot::SetupAxisLimits(ImAxis_X1, x_min, x_max, ImPlotCond_Always);
      ImPlot::SetupAxisLimits(ImAxis_Y1, local_min, local_max, ImPlotCond_Always);
      if (series.size() > 1) {
        ImPlot::SetupLegend(ImPlotLocation_NorthEast, ImPlotLegendFlags_Horizontal);
      }
      // Check if zoomed in enough to show individual point markers
      const double plot_width_px = ImPlot::GetPlotSize().x;
      for (const auto &ps : series) {
        // Check hidden state
        bool is_hidden = false;
        for (int si = 0; si < static_cast<int>(chart.series.size()); ++si) {
          if (chart.series[si] == ps.ref && si < static_cast<int>(chart.hidden.size()) && chart.hidden[si]) {
            is_hidden = true;
            break;
          }
        }
        const int n = static_cast<int>(ps.xs.size());
        ImU32 col = is_hidden ? IM_COL32(128, 128, 128, 40) : packedColor(ps.color);
        ImPlotSpec spec(ImPlotProp_LineColor, col, ImPlotProp_LineWeight, is_hidden ? 0.5f : 2.0f);
        // Include message identity in label to avoid collisions (matches Qt: signal msgName bus:addr)
        const std::string legend_label = series.size() > 1
          ? ps.ref.signal_name + " (" + msgName(ps.ref.msg_id) + " " + ps.ref.msg_id.toString() + ")"
          : ps.ref.signal_name;
        if (chart.series_type == 1) {
          ImPlot::PlotStairs(legend_label.c_str(), ps.xs.data(), ps.ys.data(), n, spec);
        } else if (chart.series_type == 2) {
          ImPlot::PlotScatter(legend_label.c_str(), ps.xs.data(), ps.ys.data(), n, spec);
        } else {
          ImPlot::PlotLine(legend_label.c_str(), ps.xs.data(), ps.ys.data(), n, spec);
        }
        // Show point markers when zoomed in (line/step charts only, not scatter)
        if (!is_hidden && chart.series_type != 2 && n > 0 && n < static_cast<int>(plot_width_px / 8.0)) {
          ImPlotSpec pt_spec(ImPlotProp_MarkerSize, 3.0f, ImPlotProp_LineColor, col);
          ImPlot::PlotScatter(("##pts_" + legend_label).c_str(), ps.xs.data(), ps.ys.data(), n, pt_spec);
        }
      }
      // Legend click-to-hide for multi-series charts
      // Defer cache invalidation to after the chart loop to avoid use-after-free
      if (series.size() > 1) {
        for (int si = 0; si < static_cast<int>(chart.series.size()); ++si) {
          const std::string hover_label = chart.series[si].signal_name + " (" + msgName(chart.series[si].msg_id) + " " + chart.series[si].msg_id.toString() + ")";
          if (ImPlot::IsLegendEntryHovered(hover_label.c_str())) {
            if (ImGui::IsMouseClicked(0)) {
              if (static_cast<int>(chart.hidden.size()) <= si) chart.hidden.resize(si + 1, false);
              chart.hidden[si] = !chart.hidden[si];
              deferred_cache_invalidations.push_back(chart.id);
            }
          }
        }
      }

      // Current time indicator
      double cur = stream_ ? stream_->currentSec() : 0.0;
      ImPlotSpec cur_spec(ImPlotProp_LineColor, IM_COL32(255, 255, 255, 140), ImPlotProp_LineWeight, 1.0f);
      ImPlot::PlotInfLines("##Current", &cur, 1, cur_spec);
      if (ImPlot::DragLineX(chart.id, &cur, ImVec4(1.0f, 1.0f, 1.0f, 0.65f), 1.0f)) {
        stream_->seekTo(std::clamp(cur, stream_->minSeconds(), stream_->maxSeconds()));
      }

      // Cross-chart synchronized hover: use live hover first, then this frame's synced hover,
      // then the previous frame's shared hover state.
      const bool this_hovered = ImPlot::IsPlotHovered();
      const double hover_sec = this_hovered ? ImPlot::GetPlotMousePos().x
                                            : (hover_sec_this_frame >= 0 ? hover_sec_this_frame : chart_hover_sec_);
      const bool show_track = hover_sec >= 0;
      if (this_hovered) hover_sec_this_frame = ImPlot::GetPlotMousePos().x;

      bool left_drag_zoomed = false;
      const ImVec2 plot_pos = ImPlot::GetPlotPos();
      const ImVec2 plot_size = ImPlot::GetPlotSize();
      const float plot_min_x = plot_pos.x;
      const float plot_max_x = plot_pos.x + plot_size.x;
      const float plot_min_y = plot_pos.y;
      const float plot_max_y = plot_pos.y + plot_size.y;
      const auto clampPlotX = [&](float x) { return std::clamp(x, plot_min_x, plot_max_x); };
      const ImVec2 mouse = ImGui::GetIO().MousePos;
      if (this_hovered && ImGui::IsMouseClicked(kChartZoomButton) && !ImGui::GetIO().KeyShift) {
        chart_zoom_drag_.active = true;
        chart_zoom_drag_.chart_id = chart.id;
        chart_zoom_drag_.plot_min_x = plot_min_x;
        chart_zoom_drag_.plot_min_y = plot_min_y;
        chart_zoom_drag_.plot_max_x = plot_max_x;
        chart_zoom_drag_.plot_max_y = plot_max_y;
        chart_zoom_drag_.start_x = clampPlotX(mouse.x);
      }
      if (chart_zoom_drag_.active && chart_zoom_drag_.chart_id == chart.id) {
        const float cur_x = std::clamp(mouse.x, chart_zoom_drag_.plot_min_x, chart_zoom_drag_.plot_max_x);
        const float drag_width_px = std::abs(cur_x - chart_zoom_drag_.start_x);
        if (ImGui::IsMouseDown(kChartZoomButton) && !ImGui::GetIO().KeyShift && drag_width_px > kMinChartZoomDragPx) {
          ImDrawList *overlay = ImPlot::GetPlotDrawList();
          const float sel_min_x = std::min(chart_zoom_drag_.start_x, cur_x);
          const float sel_max_x = std::max(chart_zoom_drag_.start_x, cur_x);
          overlay->AddRectFilled(ImVec2(sel_min_x, chart_zoom_drag_.plot_min_y),
                                 ImVec2(sel_max_x, chart_zoom_drag_.plot_max_y),
                                 IM_COL32(180, 205, 230, 40));
          overlay->AddRect(ImVec2(sel_min_x, chart_zoom_drag_.plot_min_y),
                           ImVec2(sel_max_x, chart_zoom_drag_.plot_max_y),
                           IM_COL32(180, 205, 230, 180), 0.0f, 0, 1.0f);
        }
        if (ImGui::IsMouseReleased(kChartZoomButton)) {
          if (!ImGui::GetIO().KeyShift && drag_width_px > kMinChartZoomDragPx) {
            const float sel_min_x = std::min(chart_zoom_drag_.start_x, cur_x);
            const float sel_max_x = std::max(chart_zoom_drag_.start_x, cur_x);
            const double min_x = std::clamp(ImPlot::PixelsToPlot(ImVec2(sel_min_x, plot_min_y)).x, stream_->minSeconds(), stream_->maxSeconds());
            const double max_x = std::clamp(ImPlot::PixelsToPlot(ImVec2(sel_max_x, plot_min_y)).x, stream_->minSeconds(), stream_->maxSeconds());
            if ((max_x - min_x) > kMinChartZoomSeconds) {
              updateChartRange((min_x + max_x) * 0.5, max_x - min_x);
              left_drag_zoomed = true;
            }
          }
          chart_zoom_drag_ = {};
        }
      }

      // Track points using "last sample at or before hover time" semantics (skip hidden series)
      if (show_track) {
        ImDrawList *draw = ImPlot::GetPlotDrawList();
        struct HoverAnnotation {
          ImVec2 point;
          ImU32 color;
          std::string text;
        };
        std::vector<HoverAnnotation> annotations;
        annotations.reserve(series.size());
        for (int si = 0; si < static_cast<int>(series.size()); ++si) {
          const auto &ps = series[si];
          bool hidden = si < static_cast<int>(chart.hidden.size()) && chart.hidden[si];
          if (hidden || ps.xs.empty()) continue;
          auto it = std::upper_bound(ps.xs.begin(), ps.xs.end(), hover_sec);
          int idx = (it == ps.xs.begin()) ? 0 : static_cast<int>(it - ps.xs.begin()) - 1;
          ImVec2 pos = ImPlot::PlotToPixels(ps.xs[idx], ps.ys[idx]);
          draw->AddCircleFilled(pos, 5.5f, packedColor(ps.color));
          draw->AddCircle(pos, 5.5f, IM_COL32(255, 255, 255, 200), 0, 1.5f);
          if (show_track) {
            const auto *m = dbc()->msg(ps.ref.msg_id);
            const auto *s = m ? m->sig(ps.ref.signal_name) : nullptr;
            std::string val = s ? s->formatValue(ps.ys[idx], false) : std::to_string(ps.ys[idx]);
            annotations.push_back({.point = pos,
                                   .color = packedColor(ps.color),
                                   .text = val + "  " + formatTime(ps.xs[idx], false)});
          }
        }

        std::sort(annotations.begin(), annotations.end(), [](const auto &a, const auto &b) {
          return a.point.y < b.point.y;
        });

        struct LabelRect {
          float x1, y1, x2, y2;
        };
        std::vector<LabelRect> placed_rects;
        placed_rects.reserve(annotations.size());
        const float label_pad_x = 6.0f;
        const float label_pad_y = 4.0f;
        const float label_gap = 4.0f;
        for (const auto &annotation : annotations) {
          const ImVec2 text_size = ImGui::CalcTextSize(annotation.text.c_str());
          const float box_w = text_size.x + label_pad_x * 2.0f;
          const float box_h = text_size.y + label_pad_y * 2.0f;
          const float box_x = std::clamp(annotation.point.x + 12.0f,
                                         plot_min_x + 2.0f,
                                         std::max(plot_min_x + 2.0f, plot_max_x - box_w - 2.0f));
          float box_y = std::clamp(annotation.point.y - box_h * 0.5f,
                                   plot_min_y + 2.0f,
                                   std::max(plot_min_y + 2.0f, plot_max_y - box_h - 2.0f));

          auto overlaps = [&](float y) {
            for (const auto &rect : placed_rects) {
              if (!(box_x + box_w <= rect.x1 || box_x >= rect.x2 || y + box_h <= rect.y1 || y >= rect.y2)) {
                return true;
              }
            }
            return false;
          };

          while (overlaps(box_y) && box_y + box_h + label_gap <= plot_max_y - 2.0f) {
            box_y += label_gap;
          }
          while (overlaps(box_y) && box_y - label_gap >= plot_min_y + 2.0f) {
            box_y -= label_gap;
          }

          const ImVec2 offset(box_x - annotation.point.x + label_pad_x, box_y - annotation.point.y + label_pad_y);
          ImPlot::Annotation(ImPlot::PixelsToPlot(annotation.point).x, ImPlot::PixelsToPlot(annotation.point).y,
                             ImColor(annotation.color), offset, true, "%s", annotation.text.c_str());
          placed_rects.push_back({box_x, box_y, box_x + box_w, box_y + box_h});
        }
      }

      if (this_hovered) {
        // Shift+drag to scrub
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetIO().KeyShift) {
          const double seek = std::clamp(hover_sec, stream_->minSeconds(), stream_->maxSeconds());
          if (!chart_scrub_was_playing_ && !stream_->isPaused()) {
            chart_scrub_was_playing_ = true;
            stream_->pause(true);
          }
          stream_->seekTo(seek);
        }
        // Scroll wheel zoom
        if (ImGui::GetIO().MouseWheel != 0.0f) {
          const double center = std::clamp(hover_sec, stream_->minSeconds(), stream_->maxSeconds());
          const double width = std::clamp((x_max - x_min) * (ImGui::GetIO().MouseWheel > 0.0f ? 0.8 : 1.25),
                                          0.05, stream_->maxSeconds() - stream_->minSeconds());
          updateChartRange(center, width);
        }
        // Middle-mouse drag to pan
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
          ImVec2 delta_px = ImGui::GetIO().MouseDelta;
          if (std::abs(delta_px.x) > 0.1f) {
            ImPlotRect limits = ImPlot::GetPlotLimits();
            double pps = ImPlot::GetPlotSize().x / (limits.X.Max - limits.X.Min);
            updateChartRange((x_min + x_max) * 0.5 - delta_px.x / pps, x_max - x_min);
          }
        }
        // Click to seek
        if (ImGui::IsMouseReleased(kChartZoomButton) && !ImGui::GetIO().KeyShift && !left_drag_zoomed) {
          ImVec2 dd = ImGui::GetMouseDragDelta(kChartZoomButton);
          if (std::abs(dd.x) < 4.0f && std::abs(dd.y) < 4.0f) {
            stream_->seekTo(std::clamp(hover_sec, stream_->minSeconds(), stream_->maxSeconds()));
          }
        }
        // Right-click: undo zoom (matching Qt's mouseReleaseEvent behavior)
        // Long-press right-click opens context menu instead
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Right) && !ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
          if (!chart_zoom_history_.empty()) {
            chart_zoom_redo_.push_back(chart_range_);
            chart_range_ = chart_zoom_history_.back();
            chart_zoom_history_.pop_back();
            if (chart_range_) stream_->setTimeRange(chart_range_);
            else stream_->setTimeRange(std::nullopt);
          }
        }
      }
      ImPlot::EndPlot();
    }

    // Per-chart context menu (opened from title area right-click)
    if (ImGui::BeginPopup(("chart_ctx_" + std::to_string(chart.id)).c_str())) {
      const char *types[] = {"Line", "Step", "Scatter"};
      for (int t = 0; t < 3; ++t) {
        if (ImGui::MenuItem(types[t], nullptr, chart.series_type == t)) chart.series_type = t;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Manage Signals...")) manage_chart_idx = ci;
      if (chart.series.size() > 1 && ImGui::MenuItem("Split Chart")) split_chart_idx = ci;
      if (ImGui::MenuItem("Close Chart")) remove_chart_idx = ci;
      ImGui::Separator();
      if (ImGui::MenuItem("Undo Zoom", nullptr, false, !chart_zoom_history_.empty())) {
        chart_zoom_redo_.push_back(chart_range_);
        chart_range_ = chart_zoom_history_.back();
        chart_zoom_history_.pop_back();
        if (chart_range_) stream_->setTimeRange(chart_range_);
        else stream_->setTimeRange(std::nullopt);
      }
      if (ImGui::MenuItem("Redo Zoom", nullptr, false, !chart_zoom_redo_.empty())) {
        chart_zoom_history_.push_back(chart_range_);
        chart_range_ = chart_zoom_redo_.back();
        chart_zoom_redo_.pop_back();
        if (chart_range_) stream_->setTimeRange(chart_range_);
        else stream_->setTimeRange(std::nullopt);
      }
      if (ImGui::MenuItem("Reset Zoom")) {
        chart_zoom_history_.clear();
        chart_zoom_redo_.clear();
        resetChartRange();
      }
      ImGui::EndPopup();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();

    // Drag-drop: source (matches Qt: default = merge onto target, Shift = reorder)
    if (!(chart_zoom_drag_.active && chart_zoom_drag_.chart_id == chart.id) &&
        ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
      ImGui::SetDragDropPayload("CHART_REORDER", &ci, sizeof(int));
      ImGui::Text("Drop onto chart to merge (hold Shift to reorder)");
      ImGui::EndDragDropSource();
    }
    // Drag-drop: target
    if (ImGui::BeginDragDropTarget()) {
      // Draw insertion indicator when Shift is held (reorder mode)
      if (const ImGuiPayload *preview = ImGui::GetDragDropPayload()) {
        if (preview->IsDataType("CHART_REORDER") && ImGui::GetIO().KeyShift) {
          ImVec2 item_min = ImGui::GetItemRectMin();
          ImVec2 item_max = ImGui::GetItemRectMax();
          float mid_y = (item_min.y + item_max.y) * 0.5f;
          bool insert_above = ImGui::GetMousePos().y < mid_y;
          float line_y = insert_above ? item_min.y - 2.0f : item_max.y + 2.0f;
          ImDrawList *draw = ImGui::GetWindowDrawList();
          draw->AddLine(ImVec2(item_min.x, line_y), ImVec2(item_max.x, line_y),
                        ImGui::GetColorU32(ImGuiCol_DragDropTarget), 3.0f);
        }
      }
      if (const ImGuiPayload *payload = ImGui::AcceptDragDropPayload("CHART_REORDER")) {
        drag_src_idx = *static_cast<const int *>(payload->Data);
        drag_dst_idx = ci;
        // Compute insertion position based on mouse relative to target midpoint
        ImVec2 item_min2 = ImGui::GetItemRectMin();
        ImVec2 item_max2 = ImGui::GetItemRectMax();
        float mid_y2 = (item_min2.y + item_max2.y) * 0.5f;
        drag_insert_after = ImGui::GetMousePos().y >= mid_y2;
      }
      ImGui::EndDragDropTarget();
    }
  }

  // Apply deferred cache invalidations from legend toggling (safe now that rendering is complete)
  for (int id : deferred_cache_invalidations) chart_caches_.erase(id);

  // Auto-scroll during chart drag (matches Qt ChartsWidget::doAutoScroll)
  if (ImGui::GetDragDropPayload() && ImGui::GetDragDropPayload()->IsDataType("CHART_REORDER")) {
    ImVec2 mouse = ImGui::GetMousePos();
    ImVec2 win_min = ImGui::GetWindowPos();
    ImVec2 win_max = ImVec2(win_min.x + ImGui::GetWindowSize().x, win_min.y + ImGui::GetWindowSize().y);
    float scroll_zone = 40.0f;
    if (mouse.y < win_min.y + scroll_zone && ImGui::GetScrollY() > 0) {
      ImGui::SetScrollY(ImGui::GetScrollY() - 8.0f);
    } else if (mouse.y > win_max.y - scroll_zone && ImGui::GetScrollY() < ImGui::GetScrollMaxY()) {
      ImGui::SetScrollY(ImGui::GetScrollY() + 8.0f);
    }
  }

  // Shift-scrub resume — checked at panel level for release outside plots (bug #15)
  if (chart_scrub_was_playing_ && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
    stream_->pause(false);
    chart_scrub_was_playing_ = false;
  }

  // Update cross-chart hover for next frame
  chart_hover_sec_ = hover_sec_this_frame;

  // Deferred chart mutations (safe after iteration)
  if (split_chart_idx >= 0 && split_chart_idx < static_cast<int>(charts.size())) {
    auto &src = charts[split_chart_idx];
    if (src.series.size() > 1) {
      int pos = split_chart_idx + 1;
      for (size_t si = 1; si < src.series.size(); ++si) {
        ChartState nc;
        nc.id = next_chart_id_++;
        nc.series_type = src.series_type;
        nc.series.push_back(src.series[si]);
        charts.insert(charts.begin() + pos, std::move(nc));
        ++pos;
      }
      src.series.resize(1);
      src.hidden.clear();
    }
  }
  if (remove_chart_idx >= 0 && remove_chart_idx < static_cast<int>(charts.size())) {
    chart_caches_.erase(charts[remove_chart_idx].id);
    charts.erase(charts.begin() + remove_chart_idx);
  }
  if (manage_chart_idx >= 0 && manage_chart_idx < static_cast<int>(charts.size())) {
    auto &chart = charts[manage_chart_idx];
    signal_selector_chart_id_ = chart.id;
    signal_selector_selected_ = chart.series;
    signal_selector_msg_idx_ = -1;
    signal_selector_filter_ = {};
    signal_selector_avail_idx_ = -1;
    signal_selector_sel_idx_ = -1;
    show_signal_selector_ = true;
  }
  if (drag_src_idx >= 0 && drag_dst_idx >= 0 && drag_src_idx != drag_dst_idx &&
      drag_src_idx < static_cast<int>(charts.size()) && drag_dst_idx < static_cast<int>(charts.size())) {
    if (ImGui::GetIO().KeyShift) {
      // Shift+drop = reorder using insertion indicator position (matches Qt ChartsWidget::dropEvent)
      auto moved = std::move(charts[drag_src_idx]);
      charts.erase(charts.begin() + drag_src_idx);
      // Compute insertion index: insert after target if mouse was below midpoint
      int dst = drag_insert_after ? drag_dst_idx + 1 : drag_dst_idx;
      // Adjust for the removed source element
      if (drag_src_idx < dst) dst--;
      dst = std::clamp(dst, 0, static_cast<int>(charts.size()));
      charts.insert(charts.begin() + dst, std::move(moved));
    } else {
      // Default drop = merge: move all series from source into destination (matches Qt ChartView::dropEvent)
      auto &src = charts[drag_src_idx];
      auto &dst = charts[drag_dst_idx];
      for (auto &ref : src.series) {
        if (std::find(dst.series.begin(), dst.series.end(), ref) == dst.series.end()) {
          dst.series.push_back(std::move(ref));
        }
      }
      dst.hidden.clear();
      chart_caches_.erase(src.id);
      charts.erase(charts.begin() + drag_src_idx);
    }
  }

  // Cleanup stale caches
  {
    std::set<int> active_ids;
    for (const auto &tab : chart_tabs_) {
      for (const auto &c : tab.charts) active_ids.insert(c.id);
    }
    for (auto it = chart_caches_.begin(); it != chart_caches_.end(); ) {
      if (active_ids.count(it->first) == 0) it = chart_caches_.erase(it);
      else ++it;
    }
  }

  ImGui::EndChild();
}

void CabanaImguiApp::drawBinaryPanel(const ImVec2 &size) {
  ImGui::BeginChild("BinaryPanel", size, ImGuiChildFlags_Borders);
  ensureDetailTabs();
  ImGui::TextUnformatted("Detail");
  ImGui::SameLine();
  if (has_selected_id_) ImGui::TextDisabled("%s", selected_id_.toString().c_str());
  if (!detail_tabs_.empty()) {
    pushQtTabBarStyle();
    if (ImGui::BeginTabBar("message_tabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_FittingPolicyResizeDown)) {
      int remove_index = -1;
      int close_other_index = -1;
      for (int i = 0; i < static_cast<int>(detail_tabs_.size()); ++i) {
        MessageId id = detail_tabs_[i];
        const auto *msg = dbc()->msg(id);
        const std::string id_str = id.toString();
        const std::string name = msg ? msg->name : msgName(id);
        bool open = true;
        ImGuiTabItemFlags flags = pending_tab_select_ && has_selected_id_ && selected_id_ == id ? ImGuiTabItemFlags_SetSelected : 0;
        // Use MessageId as tab label (matching Qt), with message name as tooltip
        ImGui::PushID(id_str.c_str());
        if (ImGui::BeginTabItem(id_str.c_str(), &open, flags)) {
          if (!pending_tab_select_) {
            selected_id_ = id;
            has_selected_id_ = true;
          }
          ImGui::EndTabItem();
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name.c_str());
        if (ImGui::BeginPopupContextItem("msg_tab_ctx")) {
          if (ImGui::MenuItem("Edit Message...")) {
            selected_id_ = id;
            has_selected_id_ = true;
            openMessageEditor();
          }
          if (ImGui::MenuItem("Export CSV...")) {
            exportToCsvDialog(id);
          }
          ImGui::Separator();
          if (ImGui::MenuItem("Close Tab")) {
            remove_index = i;
          }
          if (ImGui::MenuItem("Close Other Tabs", nullptr, false, detail_tabs_.size() > 1)) {
            close_other_index = i;
          }
          ImGui::EndPopup();
        }
        if (!open) remove_index = i;
        ImGui::PopID();
      }
      if (close_other_index >= 0) {
        MessageId keep = detail_tabs_[close_other_index];
        detail_tabs_.assign(1, keep);
        selected_id_ = keep;
        has_selected_id_ = true;
      }
      if (remove_index >= 0) {
        const bool removing_selected = has_selected_id_ && detail_tabs_[remove_index] == selected_id_;
        detail_tabs_.erase(detail_tabs_.begin() + remove_index);
        if (removing_selected) {
          if (!detail_tabs_.empty()) {
            selected_id_ = detail_tabs_[std::min(remove_index, static_cast<int>(detail_tabs_.size()) - 1)];
          } else {
            has_selected_id_ = false;
            selected_id_ = {};
          }
        }
        ensureDetailTabs();
      }
      pending_tab_select_ = false;
      ImGui::EndTabBar();
    }
    popQtTabBarStyle();
  }
  ImGui::Separator();

  const MessageListItem *selected = selectedItem();
  if (!selected) {
    ImGui::TextDisabled("No message selected.");
    ImGui::EndChild();
    return;
  }

  const auto &last = stream_->lastMessage(selected->id);
  const cabana::Msg *msg = selectedDbcMessage();
  // Use the larger of DBC size and actual payload size (Qt sizes from DBC when present)
  const int dbc_size = (msg && msg->size > 0) ? static_cast<int>(msg->size) : 0;
  const int payload_size = static_cast<int>(last.dat.size());
  const int default_size = (msg && msg->size > 0) ? static_cast<int>(msg->size) : 8;
  const int grid_size = std::max({dbc_size, payload_size, last.dat.empty() ? default_size : 0});
  std::vector<uint8_t> padded_dat(grid_size, 0);
  std::memcpy(padded_dat.data(), last.dat.data(), std::min(static_cast<int>(last.dat.size()), grid_size));
  const auto &dat = padded_dat;

  const bool can_edit_signals = dbc()->findDBCFile(selected->id) != nullptr;
  // Compact toolbar: name + heatmap mode + actions all on one line
  ImGui::Text("%s", selected->name.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("%s", selected->node.empty() ? selected->id.toString().c_str() : selected->node.c_str());
  ImGui::SameLine();
  ImGui::TextDisabled("Heatmap:");
  ImGui::SameLine();
  if (ImGui::RadioButton("Live", heatmap_live_)) heatmap_live_ = true;
  ImGui::SameLine();
  {
    char range_buf[64];
    std::string range_label;
    if (chart_range_.has_value()) {
      snprintf(range_buf, sizeof(range_buf), "%.3f - %.3f", chart_range_->first, chart_range_->second);
      range_label = range_buf;
    } else {
      range_label = "All";
    }
    if (ImGui::RadioButton(range_label.c_str(), !heatmap_live_)) heatmap_live_ = false;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_edit_signals);
  if (smallIconButton("##edit_msg", BootstrapIcon::Pencil, "Edit Message")) openMessageEditor();
  ImGui::SameLine();
  if (smallIconButton("##rm_msg", BootstrapIcon::XLg, "Remove Message")) {
    UndoStack::push(new RemoveMsgCommand(selected->id));
    selected_signal_name_.clear();
    hovered_signal_name_.clear();
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(!can_edit_signals);
  if (smallIconButton("##add_sig", BootstrapIcon::Plus, "Add Signal")) openSignalEditor(false);
  ImGui::EndDisabled();
  if (const cabana::Signal *sig = selectedSignal()) {
    ImGui::SameLine();
    ImGui::TextColored(imColor(sig->color), "%s", sig->name.c_str());
    ImGui::SameLine();
    if (smallIconButton("##plot_sig", BootstrapIcon::GraphUp, has_selected_id_ && findChart(selected_id_, sig) ? "Unplot" : "Plot")) {
      toggleSelectedSignalPlot(false);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(sig->is_little_endian ? "LE" : "BE")) updateSignalEndian(!sig->is_little_endian);
    ImGui::SameLine();
    if (ImGui::SmallButton(sig->is_signed ? "S" : "U")) updateSignalSigned(!sig->is_signed);
    ImGui::SameLine();
    ImGui::BeginDisabled(!can_edit_signals);
    if (smallIconButton("##edit_sig", BootstrapIcon::Pencil)) openSignalEditor(true);
    ImGui::EndDisabled();
  }
  // Warnings matching Qt DetailWidget::refresh()
  if (!msg) {
    ImGui::TextColored(ImVec4(0.95f, 0.76f, 0.25f, 1.0f), "No DBC entry");
  } else {
    if (selected_id_.source == INVALID_SOURCE) {
      ImGui::TextColored(ImVec4(0.95f, 0.76f, 0.25f, 1.0f), "No messages received.");
      ImGui::SameLine();
    } else if (static_cast<int>(msg->size) != payload_size) {
      ImGui::TextColored(ImVec4(0.95f, 0.76f, 0.25f, 1.0f), "Message size (%d) is incorrect.", static_cast<int>(msg->size));
      ImGui::SameLine();
    }
    // Detect overlapping signals (matches Qt's getOverlappingSignals)
    const int total_bits = static_cast<int>(dat.size()) * 8;
    std::vector<std::vector<const cabana::Signal *>> sigs_at(total_bits);
    for (const auto *sig : msg->getSignals()) {
      for (int j = 0; j < sig->size; ++j) {
        int pos = sig->is_little_endian ? flipBitPos(sig->start_bit + j) : flipBitPos(sig->start_bit) + j;
        if (pos >= 0 && pos < total_bits) {
          sigs_at[pos].push_back(sig);
        }
      }
    }
    std::set<std::string> overlap_names;
    for (int i = 0; i < total_bits; ++i) {
      if (sigs_at[i].size() > 1) {
        for (const auto *s : sigs_at[i]) {
          if (s->type == cabana::Signal::Type::Normal) overlap_names.insert(s->name);
        }
      }
    }
    for (const auto &name : overlap_names) {
      ImGui::TextColored(ImVec4(0.95f, 0.76f, 0.25f, 1.0f), "%s has overlapping bits.", name.c_str());
      ImGui::SameLine();
    }
  }
  ImGui::TextDisabled("%s", formatPayload(dat).c_str());

  const auto bit_flips = heatmap_live_ ? last.bit_flip_counts : bitFlipCounts(dat.size());

  // Logarithmic heatmap scaling matching Qt version
  uint32_t max_bit_flip_count = 1;
  for (const auto &row : bit_flips) {
    for (uint32_t count : row) max_bit_flip_count = std::max(max_bit_flip_count, count);
  }
  const double log_factor = 1.2;
  const double log_scaler = 255.0 / std::log2(log_factor * max_bit_flip_count);

  // Build per-cell signal coverage map for border drawing
  struct CellInfo {
    std::vector<const cabana::Signal *> sigs;
    bool is_msb = false;
    bool is_lsb = false;
  };
  const int total_bits = static_cast<int>(dat.size()) * 8;
  std::vector<CellInfo> cell_info(total_bits);
  if (msg) {
    for (const auto *sig : msg->getSignals()) {
      for (int j = 0; j < sig->size; ++j) {
        int pos = sig->is_little_endian ? flipBitPos(sig->start_bit + j) : flipBitPos(sig->start_bit) + j;
        if (pos < 0 || pos >= total_bits) break;
        auto &sigs = cell_info[pos].sigs;
        sigs.push_back(sig);
        // Sort by descending size so sigs.back() is the smallest (matching Qt precedence)
        if (sigs.size() > 1) {
          std::sort(sigs.begin(), sigs.end(), [](const cabana::Signal *l, const cabana::Signal *r) { return l->size > r->size; });
        }
        if (j == 0) { sig->is_little_endian ? cell_info[pos].is_lsb = true : cell_info[pos].is_msb = true; }
        if (j == sig->size - 1) { sig->is_little_endian ? cell_info[pos].is_msb = true : cell_info[pos].is_lsb = true; }
      }
    }
  }

  bool binary_hovered = false;

  // Helper: check if a neighbor cell contains a given signal
  auto neighborHasSig = [&](int logical_bit, int dx, int dy, const cabana::Signal *sig) -> bool {
    int row = logical_bit / 8 + dy;
    int col = logical_bit % 8 + dx;
    if (row < 0 || row >= static_cast<int>(dat.size()) || col < 0 || col >= 8) return false;
    int idx = row * 8 + col;
    auto &s = cell_info[idx].sigs;
    return std::find(s.begin(), s.end(), sig) != s.end();
  };

  // Custom-drawn binary grid matching Qt Cabana's look: big cells, no grid lines, stretched columns
  {
    ImDrawList *dl = ImGui::GetWindowDrawList();
    const float avail_w = ImGui::GetContentRegionAvail().x;
    const float avail_h = ImGui::GetContentRegionAvail().y - 4.0f;  // leave room for popups
    const float byte_col_w = 28.0f;
    const float hex_col_w = 32.0f;
    const float grid_w = avail_w - byte_col_w - hex_col_w;
    const float cell_w = grid_w / 8.0f;
    const float cell_h = std::max(32.0f, std::min(42.0f, avail_h / std::max(1.0f, static_cast<float>(dat.size()))));
    const float total_h = cell_h * dat.size();
    const float font_size = ImGui::GetFontSize();

    // Scrollable child window with manual hit testing
    ImGui::BeginChild("BinaryGrid", ImVec2(0, -4), ImGuiChildFlags_None);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    // InvisibleButton sets scrollable content height AND captures mouse interaction
    ImGui::InvisibleButton("##binary_grid_area", ImVec2(avail_w, total_h));
    const bool btn_hovered = ImGui::IsItemHovered();
    const bool btn_clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const float scroll_y = ImGui::GetScrollY();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const bool grid_hovered = btn_hovered && mouse.x >= origin.x + byte_col_w && mouse.x < origin.x + byte_col_w + grid_w;
    const bool grid_clicked = grid_hovered && btn_clicked;

    // Compute which cell the mouse is over (during active drag, always compute even if mouse leaves grid)
    int hover_byte = -1, hover_col = -1;
    const bool compute_hover = grid_hovered || (binary_drag_.active && ImGui::IsMouseDown(ImGuiMouseButton_Left));
    if (compute_hover) {
      float rel_x = mouse.x - origin.x - byte_col_w;
      float rel_y = mouse.y - origin.y + scroll_y;
      hover_col = std::clamp(static_cast<int>(rel_x / cell_w), 0, 7);
      hover_byte = std::clamp(static_cast<int>(rel_y / cell_h), 0, static_cast<int>(dat.size()) - 1);
    }

    // Update drag position from current mouse cell BEFORE computing the preview
    if (binary_drag_.active && hover_byte >= 0 && hover_col >= 0) {
      int drag_logical = hover_byte * 8 + hover_col;
      binary_drag_.current_bit = flipBitPos(drag_logical);
      binary_drag_.current_logical = drag_logical;
    }

    // Compute drag preview highlight using up-to-date current_bit
    // Build a set of occupied logical positions for correct big-endian handling
    std::vector<bool> drag_preview_bits(total_bits, false);
    bool has_drag_preview = false;
    if (binary_drag_.active) {
      auto [start_bit, size_bits, is_little_endian] = currentBinarySelection();
      has_drag_preview = true;
      for (int j = 0; j < size_bits; ++j) {
        int pos = is_little_endian ? flipBitPos(start_bit + j) : flipBitPos(start_bit) + j;
        if (pos >= 0 && pos < total_bits) drag_preview_bits[pos] = true;
      }
    }

    // Determine visible rows
    const int first_row = std::max(0, static_cast<int>(scroll_y / cell_h) - 1);
    const int last_row = std::min(static_cast<int>(dat.size()), static_cast<int>((scroll_y + avail_h) / cell_h) + 2);

    for (int byte = first_row; byte < last_row; ++byte) {
      const float row_y = origin.y + byte * cell_h - scroll_y;
      if (row_y + cell_h < origin.y || row_y > origin.y + avail_h) continue;

      // Byte number label
      char byte_label[16];
      std::snprintf(byte_label, sizeof(byte_label), "%d", byte);
      ImVec2 byte_ts = ImGui::CalcTextSize(byte_label);
      dl->AddText(ImVec2(origin.x + byte_col_w - byte_ts.x - 4.0f, row_y + (cell_h - byte_ts.y) * 0.5f),
                  IM_COL32(140, 140, 140, 200), byte_label);

      // Track whether this byte is beyond the actual payload (invalid/nonexistent)
      const bool byte_invalid = (byte >= payload_size);

      for (int col = 0; col < 8; ++col) {
        const int logical_bit = byte * 8 + col;
        const int signal_bit = flipBitPos(logical_bit);
        auto &ci = cell_info[logical_bit];
        const cabana::Signal *covering = ci.sigs.empty() ? nullptr : ci.sigs.back();

        const float cx = origin.x + byte_col_w + col * cell_w;
        const ImVec2 cell_min(cx, row_y);
        const ImVec2 cell_max(cx + cell_w, row_y + cell_h);

        // Background: signal color with heatmap alpha, or heatmap only
        float alpha = covering ? 0.098f : 0.0f;
        if (static_cast<size_t>(byte) < bit_flips.size()) {
          uint32_t flip_count = bit_flips[byte][col];
          if (flip_count > 0) {
            double normalized = std::log2(1.0 + flip_count * log_factor) * log_scaler;
            double min_alpha = covering ? 25.0 : 10.0;
            alpha = static_cast<float>(std::clamp(normalized, min_alpha, 255.0) / 255.0);
          }
        }
        if (covering) {
          float a = (selected_signal_name_ == covering->name || hovered_signal_name_ == covering->name) ? 0.95f : alpha;
          dl->AddRectFilled(cell_min, cell_max, packedColor(covering->color, a));
        } else if (alpha > 0.0f) {
          dl->AddRectFilled(cell_min, cell_max, IM_COL32(102, 86, 169, static_cast<int>(alpha * 255.0f)));
        }

        // Drag preview highlight
        if (has_drag_preview && logical_bit < static_cast<int>(drag_preview_bits.size()) && drag_preview_bits[logical_bit]) {
          dl->AddRectFilled(cell_min, cell_max, IM_COL32(230, 245, 255, 120));
        }

        // Overlap hatching
        if (ci.sigs.size() > 1) {
          dl->PushClipRect(cell_min, cell_max, true);
          const ImU32 hatch_col = IM_COL32(200, 200, 200, 70);
          for (float off = -(cell_max.y - cell_min.y); off < cell_w; off += 6.0f) {
            dl->AddLine(ImVec2(cell_min.x + off, cell_max.y), ImVec2(cell_min.x + off + cell_h, cell_min.y), hatch_col, 1.0f);
          }
          dl->PopClipRect();
        }

        // Invalid bits hatching — bytes beyond real payload
        if (byte_invalid) {
          dl->PushClipRect(cell_min, cell_max, true);
          const ImU32 invalid_col = IM_COL32(100, 100, 100, 60);
          for (float off = -(cell_max.y - cell_min.y); off < cell_w; off += 5.0f) {
            dl->AddLine(ImVec2(cell_min.x + off, cell_max.y), ImVec2(cell_min.x + off + cell_h, cell_min.y), invalid_col, 1.0f);
          }
          dl->PopClipRect();
        }

        // Signal borders
        if (covering && !binary_drag_.active) {
          CabanaColor edge_color = covering->color.darker(125);
          ImU32 border_col = IM_COL32(edge_color.red(), edge_color.green(), edge_color.blue(), 200);
          if (!neighborHasSig(logical_bit, -1, 0, covering)) dl->AddLine(cell_min, ImVec2(cell_min.x, cell_max.y), border_col, 2.0f);
          if (!neighborHasSig(logical_bit, 1, 0, covering))  dl->AddLine(ImVec2(cell_max.x, cell_min.y), cell_max, border_col, 2.0f);
          if (!neighborHasSig(logical_bit, 0, -1, covering)) dl->AddLine(cell_min, ImVec2(cell_max.x, cell_min.y), border_col, 2.0f);
          if (!neighborHasSig(logical_bit, 0, 1, covering))  dl->AddLine(ImVec2(cell_min.x, cell_max.y), cell_max, border_col, 2.0f);
        }

        // Bit value text - large, centered (skip for invalid/nonexistent bytes)
        if (!byte_invalid) {
          const int bit_value = (dat[byte] >> (7 - col)) & 0x1;
          char bit_label[4];
          std::snprintf(bit_label, sizeof(bit_label), "%d", bit_value);
          ImVec2 bit_ts = ImGui::CalcTextSize(bit_label);
          ImU32 text_col = covering ? IM_COL32(240, 240, 240, 230) : IM_COL32(187, 187, 187, 220);
          dl->AddText(ImVec2(cx + (cell_w - bit_ts.x) * 0.5f, row_y + (cell_h - bit_ts.y) * 0.5f), text_col, bit_label);
        }

        // MSB/LSB marker - small, bottom-right corner
        if (ci.is_msb || ci.is_lsb) {
          const char *marker = (ci.is_msb && ci.is_lsb) ? "ML" : ci.is_msb ? "M" : "L";
          ImVec2 marker_ts = ImGui::CalcTextSize(marker);
          float scale = 0.7f;
          dl->AddText(nullptr, font_size * scale,
                      ImVec2(cell_max.x - marker_ts.x * scale - 2.0f, cell_max.y - marker_ts.y * scale - 1.0f),
                      IM_COL32(255, 255, 255, 160), marker);
        }

        // Hit testing via grid-level mouse math
        if (hover_byte == byte && hover_col == col) {
          binary_hovered = true;
          hovered_signal_name_ = covering ? covering->name : std::string();
          // Highlight cell on hover
          dl->AddRect(cell_min, cell_max, IM_COL32(255, 255, 255, 80), 0.0f, 0, 2.0f);
          if (covering && !binary_drag_.active) {
            ImGui::BeginTooltip();
            ImGui::TextColored(imColor(covering->color), "%s", covering->name.c_str());
            ImGui::Text("start_bit %d  size %d  lsb %d  msb %d", covering->start_bit, covering->size, covering->lsb, covering->msb);
            ImGui::Text("%s  %s", covering->is_little_endian ? "Little-endian" : "Big-endian",
                        covering->is_signed ? "signed" : "unsigned");
            if (!covering->unit.empty()) ImGui::Text("unit: %s", covering->unit.c_str());
            if (auto it = signal_values_.find(covering->name); it != signal_values_.end()) {
              ImGui::Text("value: %s", covering->formatValue(it->second).c_str());
            }
            ImGui::EndTooltip();
          }
          if (grid_clicked) {
            beginBinaryDrag(signal_bit, logical_bit, covering);
          }
        }
      }

      // Hex column
      const float hex_x = origin.x + byte_col_w + 8 * cell_w;
      ImVec2 hex_min(hex_x, row_y);
      ImVec2 hex_max(hex_x + hex_col_w, row_y + cell_h);
      if (static_cast<size_t>(byte) < last.colors.size()) {
        dl->AddRectFilled(hex_min, hex_max, packedColor(last.colors[byte], 0.65f));
      }
      if (!byte_invalid) {
        char hex_label[4];
        std::snprintf(hex_label, sizeof(hex_label), "%02X", dat[byte]);
        ImVec2 hex_ts = ImGui::CalcTextSize(hex_label);
        dl->AddText(ImVec2(hex_x + (hex_col_w - hex_ts.x) * 0.5f, row_y + (cell_h - hex_ts.y) * 0.5f),
                    IM_COL32(187, 187, 187, 200), hex_label);
      }
    }
    ImGui::EndChild();
  }
  if (binary_drag_.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    finishBinaryDrag();
  }
  if (!binary_hovered && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
    hovered_signal_name_.clear();
  }

  if (message_editor_.open) {
    ImGui::OpenPopup("Edit Message");
    message_editor_.open = false;
  }
  { bool em_open = true;
  if (ImGui::BeginPopupModal("Edit Message", &em_open, ImGuiWindowFlags_AlwaysAutoResize)) {
    dismissOnEscape();
    ImGui::InputText("Name", message_editor_.name.data(), message_editor_.name.size());
    // Validate name
    std::string name_str(message_editor_.name.data());
    bool name_valid = true;
    std::string name_error;
    {
      std::string name_lower = name_str;
      for (auto &c : name_lower) c = std::tolower(c);
      if (name_lower == "untitled" || name_str.empty()) {
        name_valid = false;
        name_error = "Name cannot be empty or 'untitled'";
      }
    }
    if (name_valid && !isValidDbcIdentifier(name_str)) {
      name_valid = false;
      name_error = "Only letters, digits, and underscores allowed";
    }
    if (name_valid && has_selected_id_) {
      // Check for duplicate names (different address, same source bus)
      auto *existing = dbc()->msg(selected_id_.source, name_str);
      if (existing && existing->address != selected_id_.address) {
        name_valid = false;
        name_error = "Name already exists";
      }
    }
    ImGui::InputScalar("Size", ImGuiDataType_S32, &message_editor_.size);
    message_editor_.size = std::clamp(message_editor_.size, 1, CAN_MAX_DATA_BYTES);
    ImGui::InputText("Node", message_editor_.node.data(), message_editor_.node.size());
    std::string node_str(message_editor_.node.data());
    bool node_valid = node_str.empty() || isValidDbcIdentifier(node_str);
    if (!node_valid) {
      name_error = "Node: only letters, digits, and underscores allowed";
    }
    if (!name_error.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", name_error.c_str());
    }
    ImGui::InputTextMultiline("Comment", message_editor_.comment.data(), message_editor_.comment.size(), ImVec2(360.0f, 80.0f));
    ImGui::BeginDisabled(!name_valid || !node_valid);
    if (ImGui::Button("Save")) {
      commitMessageEditor();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }}
  binary_panel_hovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  ImGui::EndChild();
}

std::vector<std::string> CabanaImguiApp::startupSummary() const {
  CabanaPersistentState state;
  CabanaWorkspaceState workspace = {
    .has_stream = stream_ != nullptr && !dynamic_cast<DummyStream *>(stream_),
    .live_streaming = stream_ ? stream_->liveStreaming() : (config_.mode != CabanaLaunchConfig::Mode::Replay),
    .route_name = stream_ ? stream_->routeName() : (config_.route.empty() ? "none" : config_.route),
    .car_fingerprint = stream_ ? stream_->carFingerprint() : std::string(),
  };
  if (!config_.dbc_file.empty()) rememberRecentFile(state, config_.dbc_file, 15);

  std::vector<std::string> lines;
  lines.push_back(config_.route.empty() ? "route: none selected" : "route: " + config_.route);
  lines.push_back(config_.dbc_file.empty() ? "dbc: auto or none selected" : "dbc: " + config_.dbc_file);
  lines.push_back("workspace title: " + workspace.videoPanelTitle());
  lines.push_back("fingerprint: " + (stream_ ? stream_->carFingerprint() : std::string("unknown")));
  lines.push_back("recent file slots primed: " + std::to_string(state.recent_files.size()));
  return lines;
}

ReplayStream *CabanaImguiApp::replayStream() const {
  return dynamic_cast<ReplayStream *>(stream_);
}
