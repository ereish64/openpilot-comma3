#include "tools/cabana/imgui/app.h"
#include "tools/cabana/imgui/app_util.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>

#include <arpa/inet.h>

#include "imgui.h"
#include "implot.h"
#include "third_party/json11/json11.hpp"
#include "tools/cabana/core/persistent_state.h"
#include "tools/cabana/core/workspace_state.h"
#include "tools/cabana/imgui/commands.h"
#include "tools/cabana/imgui/dbcmanager.h"
#include "tools/cabana/imgui/export.h"
#include "tools/cabana/imgui/settings.h"
#include "tools/cabana/imgui/stream.h"
#include "tools/cabana/imgui/replaystream.h"
#include "tools/cabana/imgui/util.h"
#include "tools/cabana/panda.h"
#ifndef __APPLE__
#include "tools/cabana/imgui/socketcanstream.h"
#endif
#include "tools/cabana/imgui/devicestream.h"
#include "tools/cabana/imgui/pandastream.h"
#include "tools/replay/replay.h"
#include "tools/replay/timeline.h"

void CabanaImguiApp::drawRouteBrowser() {
  if (!show_route_browser_) return;

  // Poll async results
  if (rb_future_.valid() && rb_future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
    std::string result = rb_future_.get();
    std::string err;
    auto j = json11::Json::parse(result, err);
    if (!err.empty()) {
      rb_error_ = "Failed to parse API response";
    } else if (j.is_object() && j["error"].is_string()) {
      rb_error_ = j["error"].string_value();
    } else if (rb_fetching_devices_) {
      bool was_empty = rb_devices_.empty();
      rb_devices_.clear();
      if (j.is_array()) {
        for (const auto &dev : j.array_items()) {
          RemoteDevice d;
          d.dongle_id = dev["dongle_id"].string_value();
          d.alias = dev["alias"].is_string() ? dev["alias"].string_value() : d.dongle_id;
          if (d.alias.empty()) d.alias = d.dongle_id;
          rb_devices_.push_back(std::move(d));
        }
      }
      if (was_empty && !rb_devices_.empty()) rb_selection_changed_ = true;
      if (rb_devices_.empty()) rb_error_ = "No devices found. Check authentication (~/.comma/auth.json).";
    } else if (rb_fetching_routes_) {
      // Drop stale results from a previous device/period selection
      if (rb_pending_fetch_id_ != rb_fetch_id_) { rb_fetching_routes_ = false; return; }
      rb_routes_.clear();
      if (j.is_array()) {
        for (const auto &r : j.array_items()) {
          RemoteRoute route;
          route.fullname = r["fullname"].string_value();
          // Parse start/end time for display
          double start_ms = r["start_time_utc_millis"].is_number() ? r["start_time_utc_millis"].number_value() : 0;
          double end_ms = r["end_time_utc_millis"].is_number() ? r["end_time_utc_millis"].number_value() : 0;
          if (start_ms > 0) {
            time_t t = static_cast<time_t>(start_ms / 1000.0);
            struct tm tm_buf;
            localtime_r(&t, &tm_buf);
            char time_str[64];
            strftime(time_str, sizeof(time_str), "%Y-%m-%d %H:%M:%S", &tm_buf);
            int duration_min = (end_ms > start_ms) ? static_cast<int>((end_ms - start_ms) / 60000.0) : 0;
            route.display = std::string(time_str) + "    " + std::to_string(duration_min) + "min";
          } else {
            // Preserved routes may use ISO format
            route.display = r["start_time"].is_string() ? r["start_time"].string_value() : route.fullname;
          }
          rb_routes_.push_back(std::move(route));
        }
      }
      if (rb_routes_.empty()) rb_error_ = "No routes found for this device/period.";
    }
    rb_fetching_devices_ = false;
    rb_fetching_routes_ = false;
  }

  ImGui::SetNextWindowSize(ImVec2(680, 400), ImGuiCond_FirstUseEver);
  const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  if (ImGui::Begin("Remote Route Browser", &show_route_browser_)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_route_browser_ = false;
    }
    if (rb_devices_.empty() && rb_fetching_devices_) {
      ImGui::TextUnformatted("Fetching devices...");
    } else if (!rb_devices_.empty()) {
      // Device selector
      ImGui::Text("Device:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(200);
      bool rb_selection_changed = false;
      if (ImGui::BeginCombo("##rb_device", rb_devices_[rb_device_idx_].alias.c_str())) {
        for (int i = 0; i < static_cast<int>(rb_devices_.size()); ++i) {
          if (ImGui::Selectable(rb_devices_[i].alias.c_str(), i == rb_device_idx_)) {
            rb_device_idx_ = i;
            rb_routes_.clear();
            rb_route_idx_ = 0;
            rb_error_.clear();
            rb_selection_changed = true;
          }
        }
        ImGui::EndCombo();
      }

      // Time period selector
      ImGui::SameLine();
      ImGui::Text("Period:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(140);
      const char *periods[] = {"Last week", "Last 2 weeks", "Last month", "Last 6 months", "Preserved"};
      if (ImGui::BeginCombo("##rb_period", periods[rb_period_idx_])) {
        for (int i = 0; i < 5; ++i) {
          if (ImGui::Selectable(periods[i], i == rb_period_idx_)) {
            rb_period_idx_ = i;
            rb_routes_.clear();
            rb_route_idx_ = 0;
            rb_error_.clear();
            rb_selection_changed = true;
          }
        }
        ImGui::EndCombo();
      }

      // Fetch routes (auto-fetch when device or period changes, matching Qt behavior)
      ImGui::SameLine();
      const bool want_fetch = !rb_fetching_routes_ && (rb_selection_changed_ || rb_selection_changed);
      ImGui::BeginDisabled(rb_fetching_routes_);
      if (want_fetch || ImGui::Button(rb_fetching_routes_ ? "Loading..." : "Fetch")) {
        rb_selection_changed_ = false;
        rb_fetching_routes_ = true;
        rb_error_.clear();
        rb_routes_.clear();
        rb_pending_fetch_id_ = ++rb_fetch_id_;
        const std::string dongle_id = rb_devices_[rb_device_idx_].dongle_id;
        const int period = rb_period_idx_;
        rb_future_ = std::async(std::launch::async, [dongle_id, period]() -> std::string {
          std::string cmd = "python3 -m openpilot.tools.lib.file_downloader device-routes " + dongle_id;
          if (period < 4) {
            const int days[] = {7, 14, 30, 180};
            auto now = std::chrono::system_clock::now();
            auto end_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
            auto start_ms = end_ms - static_cast<int64_t>(days[period]) * 86400000LL;
            cmd += " --start " + std::to_string(start_ms) + " --end " + std::to_string(end_ms);
          } else {
            cmd += " --preserved";
          }
          cmd += " 2>/dev/null";
          FILE *p = popen(cmd.c_str(), "r");
          if (!p) return "{\"error\":\"failed to run python\"}";
          std::string out;
          char buf[4096];
          while (fgets(buf, sizeof(buf), p)) out += buf;
          pclose(p);
          return out;
        });
      }
      ImGui::EndDisabled();

      // Route list (always show the list area to avoid flashing on device switch)
      ImGui::Separator();
      if (rb_fetching_routes_) {
        ImGui::Text("Loading routes...");
      } else if (!rb_routes_.empty()) {
        ImGui::Text("%d routes:", static_cast<int>(rb_routes_.size()));
      } else {
        ImGui::TextDisabled("No routes loaded");
      }
      ImVec2 avail = ImGui::GetContentRegionAvail();
      if (ImGui::BeginListBox("##rb_routes", ImVec2(avail.x, avail.y - 35))) {
        for (int i = 0; i < static_cast<int>(rb_routes_.size()); ++i) {
          if (ImGui::Selectable(rb_routes_[i].display.c_str(), i == rb_route_idx_, ImGuiSelectableFlags_AllowDoubleClick)) {
            rb_route_idx_ = i;
            if (ImGui::IsMouseDoubleClicked(0)) {
              std::strncpy(ss_route_.data(), rb_routes_[rb_route_idx_].fullname.c_str(), ss_route_.size() - 1);
              show_route_browser_ = false;
            }
          }
        }
        ImGui::EndListBox();
      }
      if (!rb_routes_.empty()) {
        if (ImGui::Button("Select")) {
          std::strncpy(ss_route_.data(), rb_routes_[rb_route_idx_].fullname.c_str(), ss_route_.size() - 1);
          show_route_browser_ = false;
        }
        ImGui::SameLine();
      }
    }

    if (!rb_error_.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", rb_error_.c_str());
    }
    if (ImGui::Button("Cancel")) {
      show_route_browser_ = false;
    }
  }
  ImGui::End();
}

void CabanaImguiApp::showImguiOpenDialog(const std::string &title, const std::string &dir,
                                         const std::string &filter_ext,
                                         std::function<void(const std::string &)> callback) {
  fb_title_ = title;
  fb_current_dir_ = dir;
  fb_directory_mode_ = false;
  fb_save_mode_ = false;
  fb_filter_ = filter_ext;
  fb_filename_ = {};
  fb_callback_ = std::move(callback);
  fb_entries_.clear();
  fb_selected_path_.clear();
  show_file_browser_ = true;
}

void CabanaImguiApp::showImguiSaveDialog(const std::string &title, const std::string &dir,
                                         const std::string &default_name, const std::string &filter_ext,
                                         std::function<void(const std::string &)> callback) {
  fb_title_ = title;
  fb_current_dir_ = dir;
  fb_directory_mode_ = false;
  fb_save_mode_ = true;
  fb_filter_ = filter_ext;
  fb_filename_ = {};
  std::snprintf(fb_filename_.data(), fb_filename_.size(), "%s", default_name.c_str());
  fb_callback_ = std::move(callback);
  fb_entries_.clear();
  fb_selected_path_.clear();
  show_file_browser_ = true;
}

void CabanaImguiApp::showImguiDirDialog(const std::string &title, const std::string &dir,
                                        std::function<void(const std::string &)> callback) {
  fb_title_ = title;
  fb_current_dir_ = dir;
  fb_directory_mode_ = true;
  fb_save_mode_ = false;
  fb_filter_.clear();
  fb_filename_ = {};
  fb_callback_ = std::move(callback);
  fb_entries_.clear();
  fb_selected_path_.clear();
  show_file_browser_ = true;
}

void CabanaImguiApp::drawFileBrowser() {
  if (!show_file_browser_) return;

  ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
  { const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); }
  bool was_open = show_file_browser_;
  bool saved = false;
  if (ImGui::Begin(fb_title_.c_str(), &show_file_browser_)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_file_browser_ = false;
    }
    // Directory path
    ImGui::TextDisabled("%s", fb_current_dir_.c_str());
    ImGui::Separator();

    // Refresh entries
    if (fb_entries_.empty()) {
      try {
        for (const auto &entry : std::filesystem::directory_iterator(fb_current_dir_)) {
          FBEntry e;
          e.name = entry.path().filename().string();
          e.is_dir = entry.is_directory();
          if (e.name.empty() || e.name[0] == '.') continue;
          if (!e.is_dir && !fb_filter_.empty()) {
            // Check extension filter
            auto dot = e.name.rfind('.');
            if (dot == std::string::npos) continue;
            std::string ext = e.name.substr(dot);
            if (fb_filter_.find(ext) == std::string::npos) continue;
          }
          fb_entries_.push_back(std::move(e));
        }
      } catch (...) {}
      // Sort: directories first, then alphabetical
      std::sort(fb_entries_.begin(), fb_entries_.end(), [](const FBEntry &a, const FBEntry &b) {
        if (a.is_dir != b.is_dir) return a.is_dir > b.is_dir;
        return a.name < b.name;
      });
    }

    // Parent directory button
    if (ImGui::Button("..")) {
      fb_current_dir_ = pathDirname(fb_current_dir_);
      fb_entries_.clear();
      fb_selected_path_.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(parent directory)");

    // File list
    ImVec2 avail = ImGui::GetContentRegionAvail();
    if (ImGui::BeginListBox("##fb_list", ImVec2(avail.x, avail.y - 35))) {
      for (const auto &entry : fb_entries_) {
        std::string label = entry.is_dir ? "[D] " + entry.name : entry.name;
        bool selected = fb_selected_path_ == entry.name;
        if (ImGui::Selectable(label.c_str(), selected, ImGuiSelectableFlags_AllowDoubleClick)) {
          if (entry.is_dir && ImGui::IsMouseDoubleClicked(0)) {
            fb_current_dir_ += "/" + entry.name;
            fb_entries_.clear();
            fb_selected_path_.clear();
          } else if (entry.is_dir && fb_directory_mode_) {
            fb_selected_path_ = entry.name;
          } else if (!entry.is_dir && !fb_directory_mode_) {
            fb_selected_path_ = entry.name;
            if (fb_save_mode_) {
              std::snprintf(fb_filename_.data(), fb_filename_.size(), "%s", entry.name.c_str());
            }
            if (ImGui::IsMouseDoubleClicked(0)) {
              std::string full = fb_current_dir_ + "/" + fb_selected_path_;
              show_file_browser_ = false;
              if (fb_callback_) fb_callback_(full);
            }
          } else if (entry.is_dir) {
            // Double-click to enter, single click to navigate
            fb_selected_path_ = entry.name;
          }
        }
      }
      ImGui::EndListBox();
    }

    // In save mode, show a filename text input
    if (fb_save_mode_) {
      ImGui::Text("Filename:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
      ImGui::InputText("##fb_filename", fb_filename_.data(), fb_filename_.size());
    }

    const bool can_confirm = fb_save_mode_ ? fb_filename_[0] != '\0'
                           : fb_directory_mode_ ? true
                           : !fb_selected_path_.empty();
    ImGui::BeginDisabled(!can_confirm);
    if (ImGui::Button(fb_save_mode_ ? "Save" : "Select")) {
      std::string full;
      if (fb_save_mode_) {
        full = fb_current_dir_ + "/" + std::string(fb_filename_.data());
      } else if (fb_directory_mode_ && fb_selected_path_.empty()) {
        full = fb_current_dir_;
      } else {
        full = fb_current_dir_ + "/" + fb_selected_path_;
      }
      show_file_browser_ = false;
      saved = true;
      if (fb_callback_) fb_callback_(full);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      show_file_browser_ = false;
    }
  }
  ImGui::End();
  // If the browser was closed without a successful save (Cancel or X button),
  // re-show the unsaved-changes prompt so the user can choose again
  if (was_open && !show_file_browser_ && !saved) {
    if (unsaved_continuation_) {
      show_unsaved_prompt_ = true;
    }
  }
}

void CabanaImguiApp::drawStatusBar(const ImVec2 &size) {
  ImGui::BeginChild("StatusBar", size, ImGuiChildFlags_Borders, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
  const bool has_status = !status_message_.empty() && std::chrono::steady_clock::now() < status_message_until_;
  if (has_status) {
    ImGui::TextUnformatted(status_message_.c_str());
    ImGui::SameLine();
    ImGui::Separator();
  }
  const bool is_dummy = !stream_ || dynamic_cast<DummyStream *>(stream_);
  if (!is_dummy) {
    ImGui::TextDisabled("%s", stream_->liveStreaming() ? "Live stream" : "Replay");
  }
  if (stream_ && !is_dummy) {
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::TextDisabled("%zu messages", stream_->lastMessages().size());
    if (has_selected_id_) {
      ImGui::SameLine();
      ImGui::Separator();
      ImGui::TextDisabled("Selected %s", selected_id_.toString().c_str());
    }
  }
  if (Replay *r = replay()) {
    if (auto event_data = r->getEventData()) {
      int total_segs = static_cast<int>(r->route().segments().size());
      int loaded = 0;
      for (const auto &[n, _] : r->route().segments()) {
        loaded += event_data->isSegmentLoaded(n) ? 1 : 0;
      }
      if (loaded < total_segs) {
        ImGui::SameLine();
        ImGui::Separator();
        ImGui::TextDisabled("Loading %d/%d", loaded, total_segs);
      }
    }
  }
  if (download_active_.load(std::memory_order_acquire)) {
    uint64_t cur = download_cur_.load(std::memory_order_relaxed);
    uint64_t total = download_total_.load(std::memory_order_relaxed);
    float frac = total > 0 ? static_cast<float>(static_cast<double>(cur) / total) : 0.0f;
    ImGui::SameLine();
    ImGui::Separator();
    ImGui::SameLine();
    char overlay[64];
    if (total > (1024 * 1024)) {
      std::snprintf(overlay, sizeof(overlay), "Downloading %d%% (%.1f MB)", static_cast<int>(frac * 100), total / (1024.0 * 1024.0));
    } else {
      std::snprintf(overlay, sizeof(overlay), "Downloading %d%%", static_cast<int>(frac * 100));
    }
    ImGui::SetNextItemWidth(300.0f);
    ImGui::ProgressBar(frac, ImVec2(0, 16), overlay);
  }
  // Persistent FPS / Cached Minutes readout (matches Qt status bar)
  ImGui::SameLine(ImGui::GetContentRegionAvail().x - 180.0f);
  ImGui::TextDisabled("FPS: %d | Cached: %d min", settings.fps, settings.max_cached_minutes);
  ImGui::EndChild();
}

bool CabanaImguiApp::confirmOrPromptUnsaved(std::function<void()> continuation) {
  if (UndoStack::instance()->isClean()) return true;
  unsaved_continuation_ = std::move(continuation);
  show_unsaved_prompt_ = true;
  return false;
}

void CabanaImguiApp::drawUnsavedPrompt() {
  if (show_unsaved_prompt_) {
    ImGui::OpenPopup("Unsaved Changes");
    show_unsaved_prompt_ = false;
  }
  { bool unsaved_open = true;
  if (ImGui::BeginPopupModal("Unsaved Changes", &unsaved_open, ImGuiWindowFlags_AlwaysAutoResize)) {
    dismissOnEscape();
    ImGui::Text("You have unsaved changes to the DBC file.");
    ImGui::Text("Do you want to save before continuing?");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(100, 0))) {
      saveDbc(false);
      ImGui::CloseCurrentPopup();
      // For native dialogs, saveDbc() completes synchronously and sets isClean().
      // For ImGui file browser, saveDbc() chains the save_next callbacks which will
      // invoke unsaved_continuation_ on completion.
      if (UndoStack::instance()->isClean()) {
        if (unsaved_continuation_) { unsaved_continuation_(); unsaved_continuation_ = nullptr; }
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Don't Save", ImVec2(100, 0))) {
      UndoStack::instance()->clear();
      ImGui::CloseCurrentPopup();
      if (unsaved_continuation_) { unsaved_continuation_(); unsaved_continuation_ = nullptr; }
    }
    ImGui::EndPopup();
  } else if (!unsaved_open) {
    // Modal was dismissed via Escape or X button — clear stale continuation
    unsaved_continuation_ = nullptr;
  }}
}

void CabanaImguiApp::drawValueDescEditor() {
  if (val_desc_editor_.open) {
    ImGui::OpenPopup("Value Descriptions");
    val_desc_editor_.open = false;
  }
  { bool vd_open = true;
  if (ImGui::BeginPopupModal("Value Descriptions", &vd_open, ImGuiWindowFlags_AlwaysAutoResize)) {
    dismissOnEscape();
    ImGui::TextDisabled("Signal: %s", val_desc_editor_.signal_name.c_str());
    ImGui::Separator();

    if (ImGui::Button("Add Row")) {
      ValueDescEditorState::Entry e;
      e.value = 0.0;
      e.desc = {};
      val_desc_editor_.entries.push_back(e);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(%d entries)", static_cast<int>(val_desc_editor_.entries.size()));

    if (ImGui::BeginTable("ValDescTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
                          ImVec2(420.0f, std::min(300.0f, 28.0f * (val_desc_editor_.entries.size() + 1))))) {
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 100.0f);
      ImGui::TableSetupColumn("Description", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 28.0f);
      ImGui::TableHeadersRow();

      int remove_idx = -1;
      for (int i = 0; i < static_cast<int>(val_desc_editor_.entries.size()); ++i) {
        ImGui::TableNextRow();
        ImGui::PushID(i);
        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputDouble("##val", &val_desc_editor_.entries[i].value, 0, 0, "%.6g");
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputText("##desc", val_desc_editor_.entries[i].desc.data(), val_desc_editor_.entries[i].desc.size());
        ImGui::TableSetColumnIndex(2);
        if (ImGui::SmallButton("X")) remove_idx = i;
        ImGui::PopID();
      }
      if (remove_idx >= 0) {
        val_desc_editor_.entries.erase(val_desc_editor_.entries.begin() + remove_idx);
      }
      ImGui::EndTable();
    }

    if (ImGui::Button("Save")) {
      const auto *msg = selectedDbcMessage();
      const auto *sig = msg ? msg->sig(val_desc_editor_.signal_name) : nullptr;
      if (sig) {
        cabana::Signal updated = *sig;
        updated.val_desc.clear();
        for (const auto &e : val_desc_editor_.entries) {
          std::string desc = e.desc.data();
          if (!desc.empty()) {
            updated.val_desc.push_back({e.value, desc});
          }
        }
        UndoStack::push(new EditSignalCommand(selected_id_, sig, updated));
        setStatusMessage("Updated value descriptions for " + val_desc_editor_.signal_name);
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }}
}

void CabanaImguiApp::drawSettingsDialog() {
  if (!show_settings_) return;
  ImGui::OpenPopup("Settings");
  show_settings_ = false;
  settings_fps_ = settings.fps;
  settings_cached_minutes_ = settings.max_cached_minutes;
  settings_chart_height_ = settings.chart_height;
  settings_drag_direction_ = static_cast<int>(settings.drag_direction);
  settings_theme_ = settings.theme;
  settings_log_livestream_ = settings.log_livestream;
  settings_video_crop_to_fill_ = settings.video_crop_to_fill;
  std::strncpy(settings_log_path_.data(), settings.log_path.c_str(), settings_log_path_.size() - 1);
}

void CabanaImguiApp::drawFindSimilarBitsDialog() {
  if (!show_find_similar_bits_) return;
  if (fsb_needs_init_) {
    fsb_needs_init_ = false;
    fsb_results_.clear();
    std::strncpy(fsb_min_msgs_.data(), "100", fsb_min_msgs_.size());
    fsb_src_bus_ = 0;
    fsb_find_bus_ = 0;
    fsb_msg_idx_ = 0;
    fsb_byte_idx_ = 0;
    fsb_bit_idx_ = 0;
    fsb_equal_ = 0;
  }
}

void CabanaImguiApp::drawFindSignalDialog() {
  if (!show_find_signal_) return;
  if (fsd_needs_init_) {
    fsd_needs_init_ = false;
    if (fsd_future_.valid()) fsd_future_.wait();
    fsd_searching_ = false;
    fsd_results_.clear();
    fsd_history_.clear();
    fsd_properties_locked_ = false;
    fsd_bus_[0] = '\0';
    fsd_address_[0] = '\0';
    std::strncpy(fsd_first_time_.data(), "0", fsd_first_time_.size());
    std::strncpy(fsd_last_time_.data(), "MAX", fsd_last_time_.size());
    fsd_min_size_ = 8;
    fsd_max_size_ = 8;
    fsd_little_endian_ = true;
    fsd_is_signed_ = false;
    std::strncpy(fsd_factor_.data(), "1.0", fsd_factor_.size());
    std::strncpy(fsd_offset_.data(), "0.0", fsd_offset_.size());
    fsd_compare_ = 0;
    fsd_value1_[0] = '\0';
    fsd_value2_[0] = '\0';
  }
}

void CabanaImguiApp::drawSignalSelectorDialog() {
  if (!show_signal_selector_) return;
  ImGui::OpenPopup("Manage Signals");
  show_signal_selector_ = false;
}

void CabanaImguiApp::drawStreamSelectorDialog() {
  if (!show_stream_selector_) return;
  ImGui::OpenPopup("Open Stream");
  show_stream_selector_ = false;
}

Replay *CabanaImguiApp::replay() const {
  auto *rs = replayStream();
  return rs ? rs->getReplay() : nullptr;
}

void CabanaImguiApp::openDbcFileDialog() {
  const std::string last_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
  if (hasNativeFileDialogs()) {
    const std::string fn = nativeOpenFileDialog("Open DBC File", last_dir, "DBC", "*.dbc");
    if (!fn.empty()) loadDbcFile(fn);
  } else {
    showImguiOpenDialog("Open DBC File", last_dir, ".dbc", [this](const std::string &path) { loadDbcFile(path); });
  }
}

bool CabanaImguiApp::loadDbcFile(const std::string &filename, const SourceSet &sources) {
  if (filename.empty()) return false;

  // Match Qt: closeAll() when operating on SOURCE_ALL to clear bus-specific DBCs
  if (sources == SOURCE_ALL) dbc()->closeAll(); else dbc()->close(sources);
  std::string error;
  if (!dbc()->open(sources, filename, &error)) {
    showErrorModal("DBC Load Failed", "Failed to load DBC file:\n" + error);
    return false;
  }

  namespace fs = std::filesystem;
  fs::path fpath(filename);
  auto state = readPersistentState();
  state.last_dir = fpath.parent_path().string();
  rememberRecentFile(state, filename, 15);
  writePersistentState(state);
  setStatusMessage("DBC loaded: " + fpath.filename().string());
  loadUiState();
  return true;
}

void CabanaImguiApp::loadDbcFromClipboard(const SourceSet &sources) {
  if (sources == SOURCE_ALL) dbc()->closeAll(); else dbc()->close(sources);
  const char *clip = ImGui::GetClipboardText();
  const std::string clip_str = clip ? clip : "";
  std::string error;
  if (dbc()->open(sources, std::string(), clip_str, &error) && dbc()->nonEmptyDBCCount() > 0) {
    setStatusMessage("DBC loaded from clipboard");
    loadUiState();
    return;
  }

  showErrorModal("Clipboard Load Failed", "Failed to load DBC from clipboard:\n" + error);
}

void CabanaImguiApp::exportToCsvDialog(std::optional<MessageId> msg_id) {
  // For single-message export with signals, offer choice
  bool export_signals = false;
  bool needs_popup = false;
  if (msg_id.has_value()) {
    const auto *msg = dbc()->msg(*msg_id);
    if (msg && !msg->sigs.empty()) {
      // Use zenity/kdialog for a quick question dialog, fallback to in-app popup
      if (hasCommand("zenity")) {
        std::string cmd = "zenity --question --title='Export CSV' "
                          "--text='Export signal values instead of hex bytes?' "
                          "--ok-label='Signals' --cancel-label='Hex' 2>/dev/null";
        export_signals = (system(cmd.c_str()) == 0);
      } else if (hasCommand("kdialog")) {
        std::string cmd = "kdialog --yesno 'Export signal values instead of hex bytes?' --title 'Export CSV' 2>/dev/null";
        export_signals = (system(cmd.c_str()) == 0);
      } else {
        needs_popup = true;
      }
    }
  }

  const std::string base_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
  const std::string route_name = stream_ ? stream_->routeName() : "cabana";
  const std::string default_name = base_dir + "/" + (route_name.empty() ? "cabana" : route_name) + ".csv";

  // When no native dialog tools are available for the signal/hex choice, defer to in-app popup
  // after getting the file path first.
  if (needs_popup) {
    csv_export_msg_id_ = msg_id;
    auto deferred_popup = [this](const std::string &fn) {
      csv_export_path_ = fn;
      show_csv_choice_ = true;
    };
    if (hasNativeFileDialogs()) {
      const std::string fn = nativeSaveFileDialog("Export stream to CSV file", default_name, "CSV", "*.csv");
      if (!fn.empty()) deferred_popup(fn);
    } else {
      const std::string base = (route_name.empty() ? "cabana" : route_name) + ".csv";
      showImguiSaveDialog("Export stream to CSV file", base_dir, base, ".csv", deferred_popup);
    }
    return;
  }

  auto do_export = [this, export_signals, msg_id](const std::string &fn) {
    if (export_signals && msg_id.has_value()) {
      utils::exportSignalsToCSV(fn, *msg_id);
    } else {
      utils::exportToCSV(fn, msg_id);
    }
    settings.last_dir = pathDirname(fn);
    setStatusMessage("CSV exported: " + pathBasename(fn));
  };
  if (hasNativeFileDialogs()) {
    const std::string fn = nativeSaveFileDialog("Export stream to CSV file", default_name, "CSV", "*.csv");
    if (!fn.empty()) do_export(fn);
  } else {
    const std::string base = (route_name.empty() ? "cabana" : route_name) + ".csv";
    showImguiSaveDialog("Export stream to CSV file", base_dir, base, ".csv", do_export);
  }
}

DBCFile *CabanaImguiApp::singleOpenDbcFile() const {
  DBCFile *single = nullptr;
  for (auto *dbc_file : dbc()->allDBCFiles()) {
    if (dbc_file->isEmpty()) continue;
    if (single) return nullptr;
    single = dbc_file;
  }
  return single;
}

void CabanaImguiApp::saveDbc(bool save_as) {
  std::vector<DBCFile *> dbc_files;
  for (auto *dbc_file : dbc()->allDBCFiles()) {
    if (!dbc_file->isEmpty()) dbc_files.push_back(dbc_file);
  }
  if (dbc_files.empty()) return;

  // Helper that saves one file and handles persistent state bookkeeping
  auto save_one = [this](DBCFile *f, const std::string &fn) {
    f->saveAs(fn);
    auto state = readPersistentState();
    state.last_dir = pathDirname(fn);
    rememberRecentFile(state, fn, 15);
    writePersistentState(state);
    setStatusMessage("DBC saved: " + pathBasename(fn));
  };

  // Process files that need a Save-As dialog, chaining async callbacks for the ImGui browser.
  // We build the list of "needs dialog" files first, then process them.  Files with existing
  // filenames (and !save_as) are saved synchronously up front.
  std::vector<DBCFile *> needs_dialog;
  for (auto *dbc_file : dbc_files) {
    if (save_as || dbc_file->filename.empty()) {
      needs_dialog.push_back(dbc_file);
    } else {
      dbc_file->save();
    }
  }

  if (needs_dialog.empty()) {
    UndoStack::instance()->setClean();
    setStatusMessage(dbc_files.size() > 1 ? "DBCs saved" : "DBC saved");
    return;
  }

  // For native dialogs, process all synchronously
  if (hasNativeFileDialogs()) {
    for (auto *dbc_file : needs_dialog) {
      const std::string default_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
      const std::string default_name = default_dir + "/untitled.dbc";
      const std::string title = "Save File (bus: " + toString(dbc()->sources(dbc_file)) + ")";
      const std::string fn = nativeSaveFileDialog(title, default_name, "DBC", "*.dbc");
      if (fn.empty()) {
        // User canceled — re-show unsaved prompt if in unsaved flow (matching Qt's loop)
        if (unsaved_continuation_) show_unsaved_prompt_ = true;
        return;
      }
      save_one(dbc_file, fn);
    }
    UndoStack::instance()->setClean();
    setStatusMessage(dbc_files.size() > 1 ? "DBCs saved" : "DBC saved");
    return;
  }

  // ImGui file browser: chain saves through async callbacks so multi-DBC works.
  // Use shared_ptr to allow the lambda to capture itself for recursive calls.
  auto remaining = std::make_shared<std::vector<DBCFile *>>(needs_dialog);
  auto total = dbc_files.size();
  auto save_next = std::make_shared<std::function<void(size_t)>>();
  *save_next = [this, remaining, total, save_one, save_next](size_t idx) {
    if (idx >= remaining->size()) {
      UndoStack::instance()->setClean();
      setStatusMessage(total > 1 ? "DBCs saved" : "DBC saved");
      // Resume any pending unsaved-change continuation now that all saves are done
      if (unsaved_continuation_) { unsaved_continuation_(); unsaved_continuation_ = nullptr; }
      return;
    }
    DBCFile *dbc_file = (*remaining)[idx];
    const std::string default_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
    const std::string title = "Save File (bus: " + toString(dbc()->sources(dbc_file)) + ")";
    showImguiSaveDialog(title, default_dir, "untitled.dbc", ".dbc",
      [save_one, dbc_file, save_next, idx](const std::string &fn) {
        save_one(dbc_file, fn);
        (*save_next)(idx + 1);
      });
  };
  (*save_next)(0);
}

void CabanaImguiApp::copyDbcToClipboard() {
  if (DBCFile *dbc_file = singleOpenDbcFile()) {
    ImGui::SetClipboardText(dbc_file->generateDBC().c_str());
    setStatusMessage("DBC copied to clipboard");
  }
}

void CabanaImguiApp::activateMessage(const MessageId &id) {
  selected_id_ = id;
  has_selected_id_ = true;
  pending_tab_select_ = true;
  if (std::find(detail_tabs_.begin(), detail_tabs_.end(), id) == detail_tabs_.end()) {
    detail_tabs_.push_back(id);
  }
  selected_signal_name_.clear();
  hovered_signal_name_.clear();
  sparklines_.clear();  // Force immediate rebuild for new message
  // Clear signal and log filters on message change (matches Qt: SignalView clears filter,
  // HistoryLog resets filter state when model changes messages)
  signal_filter_ = {};
  logs_filter_text_ = {};
  logs_filter_signal_idx_ = 0;
  logs_compare_idx_ = 0;
  ensureSelectionState();
  ensureDetailTabs();
  ensureSignalState();
  setStatusMessage("Selected " + id.toString());
}

void CabanaImguiApp::pushChartRangeHistory() {
  if (!chart_zoom_history_.empty() && chart_zoom_history_.back() == chart_range_) return;
  chart_zoom_history_.push_back(chart_range_);
  chart_zoom_redo_.clear();
  if (chart_zoom_history_.size() > 50) chart_zoom_history_.erase(chart_zoom_history_.begin());
}

std::pair<double, double> CabanaImguiApp::currentChartDisplayRange() {
  if (!stream_) return {0.0, 1.0};
  if (chart_range_) return *chart_range_;

  const double range_sec = std::clamp(static_cast<double>(settings.chart_range), 1.0,
                                      std::max(1.0, stream_->maxSeconds() - stream_->minSeconds()));
  const double cur_t = stream_->currentSec();
  double display_min = chart_follow_range_.first;
  double pos = (cur_t - display_min) / std::max(1.0, range_sec);
  if (pos < 0 || pos > 0.8) {
    display_min = std::max(stream_->minSeconds(), cur_t - range_sec * 0.1);
  }
  double display_max = std::min(display_min + range_sec, stream_->maxSeconds());
  display_min = std::max(stream_->minSeconds(), display_max - range_sec);
  display_max = display_min + range_sec;
  chart_follow_range_ = {display_min, display_max};
  return chart_follow_range_;
}

void CabanaImguiApp::updateChartRange(double center, double width, bool push_history) {
  if (!stream_) return;
  if (push_history) pushChartRangeHistory();
  width = std::clamp(width, 0.05, stream_->maxSeconds() - stream_->minSeconds());
  double min = std::max(stream_->minSeconds(), center - width * 0.5);
  double max = std::min(stream_->maxSeconds(), min + width);
  min = std::max(stream_->minSeconds(), max - width);
  chart_range_ = std::make_pair(min, max);
  heatmap_live_ = false;  // Sync heatmap to range mode (matches Qt)
  stream_->setTimeRange(chart_range_);
}

void CabanaImguiApp::resetChartRange() {
  chart_range_.reset();
  heatmap_live_ = true;  // Back to live mode when range is cleared
  if (stream_) stream_->setTimeRange(std::nullopt);
}

void CabanaImguiApp::ensureAutoDbcLoaded() {
  if (!stream_ || stream_->liveStreaming() || dbc()->nonEmptyDBCCount() > 0) return;

  const std::string fingerprint = stream_->carFingerprint();
  if (fingerprint.empty() || fingerprint == last_auto_dbc_fingerprint_) return;

  last_auto_dbc_fingerprint_ = fingerprint;
  const std::string dbc_name = autoDbcForFingerprint(fingerprint);
  if (dbc_name.empty()) return;

  std::string error;
  const std::string dbc_path = std::string(OPENDBC_FILE_PATH) + "/" + dbc_name;
  dbc()->open(SOURCE_ALL, dbc_path, &error);
  if (error.empty()) {
    loadUiState();
  }
}

void CabanaImguiApp::ensureSelectionState() {
  // Validate current selection still exists in the full message universe (stream + DBC),
  // NOT just the filtered table.  Qt keeps the detail view open even when the selected
  // message is hidden by a filter — only clear selection if the message is truly gone.
  if (has_selected_id_) {
    bool exists = false;
    if (stream_) {
      // Check stream messages
      const auto &last = stream_->lastMessages();
      if (last.count(selected_id_)) exists = true;
    }
    if (!exists) {
      // Check DBC-only messages
      if (dbc()->msg(selected_id_)) exists = true;
    }
    if (!exists) has_selected_id_ = false;
  }

  // Qt does not auto-select a message on startup; users must click one manually.
}

void CabanaImguiApp::ensureDetailTabs() {
  if (!has_selected_id_) {
    detail_tabs_.clear();
    return;
  }

  // Validate tabs against the full message universe (stream + DBC), not just filtered items.
  // Qt keeps detail tabs open even when the message is hidden by a filter.
  auto valid = [&](const MessageId &id) {
    if (stream_ && stream_->lastMessages().count(id)) return true;
    if (dbc()->msg(id)) return true;
    return false;
  };
  detail_tabs_.erase(std::remove_if(detail_tabs_.begin(), detail_tabs_.end(), [&](const MessageId &id) { return !valid(id); }), detail_tabs_.end());
  if (std::find(detail_tabs_.begin(), detail_tabs_.end(), selected_id_) == detail_tabs_.end()) {
    detail_tabs_.push_back(selected_id_);
  }
}

std::vector<CabanaImguiApp::ChartState> &CabanaImguiApp::currentCharts() {
  ensureChartTabs();
  return chart_tabs_[active_chart_tab_].charts;
}

CabanaImguiApp::ChartState *CabanaImguiApp::findChart(const MessageId &id, const cabana::Signal *sig) {
  if (!sig) return nullptr;
  // Search across ALL tabs (matching Qt's global chart list behavior)
  for (auto &tab : chart_tabs_) {
    for (auto &chart : tab.charts) {
      for (const auto &ref : chart.series) {
        if (ref.msg_id == id && ref.signal_name == sig->name) return &chart;
      }
    }
  }
  return nullptr;
}

void CabanaImguiApp::showChart(const MessageId &id, const cabana::Signal *sig, bool show, bool merge) {
  if (!sig) return;
  ChartSeriesRef ref{id, sig->name};

  if (show) {
    if (findChart(id, sig)) return;  // Already plotted (in any tab)
    auto &charts = currentCharts();
    if (merge && !charts.empty()) {
      charts.front().series.push_back(ref);
    } else {
      ChartState cs;
      cs.id = next_chart_id_++;
      cs.series_type = settings.chart_series_type;
      cs.series.push_back(ref);
      charts.insert(charts.begin(), std::move(cs));
    }
  } else {
    // Remove from ALL tabs (matching Qt's global chart semantics)
    for (auto &tab : chart_tabs_) {
      for (auto chart_it = tab.charts.begin(); chart_it != tab.charts.end(); ) {
        auto &s = chart_it->series;
        auto &h = chart_it->hidden;
        // Remove matching series and their corresponding hidden entries
        for (auto it = s.begin(); it != s.end(); ) {
          if (*it == ref) {
            size_t idx = std::distance(s.begin(), it);
            it = s.erase(it);
            if (idx < h.size()) h.erase(h.begin() + idx);
          } else {
            ++it;
          }
        }
        if (s.empty()) {
          chart_caches_.erase(chart_it->id);
          chart_it = tab.charts.erase(chart_it);
        } else {
          ++chart_it;
        }
      }
    }
  }
}

void CabanaImguiApp::removeAllCharts() {
  chart_tabs_.clear();
  chart_tabs_.push_back(ChartTabState{.id = next_chart_tab_id_++});
  active_chart_tab_ = 0;
  chart_caches_.clear();
  chart_zoom_history_.clear();
  chart_zoom_redo_.clear();
  resetChartRange();
}

void CabanaImguiApp::removeChartSeriesIf(const std::function<bool(const ChartSeriesRef &)> &pred) {
  for (auto &tab : chart_tabs_) {
    for (auto it = tab.charts.begin(); it != tab.charts.end();) {
      auto &series = it->series;
      auto &hidden = it->hidden;
      for (int si = static_cast<int>(series.size()) - 1; si >= 0; --si) {
        if (pred(series[si])) {
          chart_caches_.erase(it->id);
          series.erase(series.begin() + si);
          if (si < static_cast<int>(hidden.size())) hidden.erase(hidden.begin() + si);
        }
      }
      if (series.empty()) {
        it = tab.charts.erase(it);
      } else {
        ++it;
      }
    }
  }
}

void CabanaImguiApp::ensureChartTabs() {
  if (chart_tabs_.empty()) {
    chart_tabs_.push_back(ChartTabState{.id = next_chart_tab_id_++});
    active_chart_tab_ = 0;
  }
  active_chart_tab_ = std::clamp(active_chart_tab_, 0, static_cast<int>(chart_tabs_.size()) - 1);
}

void CabanaImguiApp::ensureSignalState() {
  const auto *msg = selectedDbcMessage();
  if (!msg || msg->getSignals().empty()) {
    selected_signal_name_.clear();
    hovered_signal_name_.clear();
    return;
  }

  // Qt does not auto-select a signal row — only clear if the current selection is invalid
  if (!selected_signal_name_.empty() && msg->sig(selected_signal_name_) == nullptr) {
    selected_signal_name_.clear();
  }
  if (!hovered_signal_name_.empty() && msg->sig(hovered_signal_name_) == nullptr) {
    hovered_signal_name_.clear();
  }
}

static std::string imguiStateFilePath() {
  return homeDir() + "/.cabana_state.json";
}

void CabanaImguiApp::loadUiState() {
  std::ifstream ifs(imguiStateFilePath());
  json11::Json j;
  bool has_sidecar = false;
  if (ifs) {
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::string err;
    j = json11::Json::parse(content, err);
    has_sidecar = err.empty();
  }

  // Always restore layout/visibility regardless of DBC state (matches Qt restoring geometry unconditionally)
  if (has_sidecar) {
    show_video_ = j["show_video"].is_null() ? true : j["show_video"].bool_value();
    show_charts_ = j["show_charts"].is_null() ? true : j["show_charts"].bool_value();
    show_messages_ = j["show_messages"].is_null() ? true : j["show_messages"].bool_value();
    charts_floating_ = j["charts_floating"].bool_value();
    if (!j["layout_left"].is_null()) layout_left_frac_ = static_cast<float>(j["layout_left"].number_value());
    if (!j["layout_center"].is_null()) layout_center_frac_ = static_cast<float>(j["layout_center"].number_value());
    if (!j["layout_center_top"].is_null()) layout_center_top_frac_ = static_cast<float>(j["layout_center_top"].number_value());
    if (!j["layout_right_top"].is_null()) layout_right_top_frac_ = static_cast<float>(j["layout_right_top"].number_value());
  }

  if (!has_sidecar || dbc()->nonEmptyDBCCount() == 0) return;
  const std::string saved_dbc = j["recent_dbc_file"].string_value();
  std::string current_dbc;
  for (auto *f : dbc()->allDBCFiles()) {
    if (!f->isEmpty() && !f->filename.empty()) { current_dbc = f->filename; break; }
  }
  const bool dbc_matches = !saved_dbc.empty() && !current_dbc.empty() && saved_dbc == current_dbc;

  if (dbc_matches) {
    if (j["active_msg_id"].is_string() && !j["active_msg_id"].string_value().empty()) {
      selected_id_ = MessageId::fromString(j["active_msg_id"].string_value());
      has_selected_id_ = true;
    }
    detail_tabs_.clear();
    for (const auto &id_j : j["selected_msg_ids"].array_items()) {
      if (id_j.is_string()) detail_tabs_.push_back(MessageId::fromString(id_j.string_value()));
    }
  }

  chart_tabs_.clear();
  if (dbc_matches) {
    for (const auto &tab_j : j["chart_tabs"].array_items()) {
      ChartTabState tab;
      tab.id = tab_j["id"].int_value();
      for (const auto &chart_j : tab_j["charts"].array_items()) {
        ChartState cs;
        cs.id = chart_j["id"].int_value();
        cs.series_type = chart_j["series_type"].int_value();
        for (const auto &ref_j : chart_j["series"].array_items()) {
          const std::string &s = ref_j.string_value();
          auto sep = s.find('|');
          if (sep != std::string::npos) {
            MessageId msg_id = MessageId::fromString(s.substr(0, sep));
            std::string sig_name = s.substr(sep + 1);
            // Validate that the signal still exists in the current DBC.
            if (auto *m = dbc()->msg(msg_id)) {
              if (m->sig(sig_name)) {
                cs.series.push_back({msg_id, sig_name});
              }
            }
          }
        }
        if (!cs.series.empty()) {
          tab.charts.push_back(std::move(cs));
          next_chart_id_ = std::max(next_chart_id_, tab.charts.back().id + 1);
        }
      }
      chart_tabs_.push_back(std::move(tab));
      next_chart_tab_id_ = std::max(next_chart_tab_id_, chart_tabs_.back().id + 1);
    }
    active_chart_tab_ = j["active_chart_tab"].int_value();
  }
}

void CabanaImguiApp::saveUiState() const {
  settings.absolute_time = absolute_time_;
  settings.suppress_defined_signals = suppress_defined_signals_;
  settings.multiple_lines_hex = multiline_bytes_;
  settings.chart_column_count = chart_columns_;

  std::vector<json11::Json> tabs_j;
  for (const auto &tab : chart_tabs_) {
    std::vector<json11::Json> charts_j;
    for (const auto &chart : tab.charts) {
      std::vector<json11::Json> series_j;
      for (const auto &ref : chart.series) {
        series_j.emplace_back(ref.msg_id.toString() + "|" + ref.signal_name);
      }
      charts_j.push_back(json11::Json::object{
        {"id", chart.id},
        {"series_type", chart.series_type},
        {"series", series_j},
      });
    }
    tabs_j.push_back(json11::Json::object{
      {"id", tab.id},
      {"charts", charts_j},
    });
  }
  json11::Json::array selected_msg_ids_j;
  for (const auto &id : detail_tabs_) selected_msg_ids_j.push_back(id.toString());
  json11::Json state = json11::Json::object{
    {"chart_tabs", tabs_j},
    {"active_chart_tab", active_chart_tab_},
    {"show_video", show_video_},
    {"show_charts", show_charts_},
    {"show_messages", show_messages_},
    {"charts_floating", charts_floating_},
    {"layout_left", static_cast<double>(layout_left_frac_)},
    {"layout_center", static_cast<double>(layout_center_frac_)},
    {"layout_center_top", static_cast<double>(layout_center_top_frac_)},
    {"layout_right_top", static_cast<double>(layout_right_top_frac_)},
    {"active_msg_id", has_selected_id_ ? json11::Json(selected_id_.toString()) : json11::Json("")},
    {"selected_msg_ids", selected_msg_ids_j},
    {"recent_dbc_file", []() -> std::string {
      for (auto *f : dbc()->allDBCFiles()) {
        if (!f->isEmpty() && !f->filename.empty()) return f->filename;
      }
      return {};
    }()},
  };
  std::ofstream ofs(imguiStateFilePath());
  if (ofs) ofs << state.dump();
}

void CabanaImguiApp::openMessageEditor() {
  const auto *msg = selectedDbcMessage();
  const auto *item = selectedItem();
  if (!item) return;

  std::string name = msg ? msg->name : item->name;
  std::string node = msg ? msg->transmitter : item->node;
  std::string comment = msg ? msg->comment : std::string();
  std::snprintf(message_editor_.name.data(), message_editor_.name.size(), "%s", name.c_str());
  std::snprintf(message_editor_.node.data(), message_editor_.node.size(), "%s", node.c_str());
  std::snprintf(message_editor_.comment.data(), message_editor_.comment.size(), "%s", comment.c_str());
  message_editor_.size = msg ? msg->size : static_cast<int>(stream_->lastMessage(selected_id_).dat.size());
  message_editor_.open = true;
}

void CabanaImguiApp::commitMessageEditor() {
  // Normalize spaces to underscores (matching Qt's NameValidator)
  normalizeDbcIdentifier(message_editor_.name.data());
  normalizeDbcIdentifier(message_editor_.node.data());
  const std::string name = message_editor_.name.data();
  const std::string node = message_editor_.node.data();
  if (!isValidDbcIdentifier(name)) {
    setStatusMessage("Invalid message name: only letters, digits, and underscores allowed.");
    return;
  }
  if (!node.empty() && !isValidDbcIdentifier(node)) {
    setStatusMessage("Invalid node name: only letters, digits, and underscores allowed.");
    return;
  }
  UndoStack::push(new EditMsgCommand(selected_id_, name, message_editor_.size, node, message_editor_.comment.data()));
}

void CabanaImguiApp::openSignalEditor(bool edit_existing) {
  const auto *item = selectedItem();
  const auto *msg = selectedDbcMessage();
  if (!item || dbc()->findDBCFile(selected_id_) == nullptr) {
    setStatusMessage("Open a DBC before editing signals");
    return;
  }

  const auto *sig = selectedSignal();
  if (edit_existing && !sig) {
    setStatusMessage("Select a signal to edit");
    return;
  }

  signal_editor_ = {};
  signal_editor_.editing_existing = edit_existing;
  const int msg_size = msg ? static_cast<int>(msg->size) : static_cast<int>(stream_->lastMessage(selected_id_).dat.size());

  if (edit_existing) {
    signal_editor_.original_name = sig->name;
    std::snprintf(signal_editor_.name.data(), signal_editor_.name.size(), "%s", sig->name.c_str());
    std::snprintf(signal_editor_.unit.data(), signal_editor_.unit.size(), "%s", sig->unit.c_str());
    std::snprintf(signal_editor_.receiver.data(), signal_editor_.receiver.size(), "%s", sig->receiver_name.c_str());
    std::snprintf(signal_editor_.comment.data(), signal_editor_.comment.size(), "%s", sig->comment.c_str());
    signal_editor_.start_bit = sig->start_bit;
    signal_editor_.size = sig->size;
    signal_editor_.is_little_endian = sig->is_little_endian;
    signal_editor_.is_signed = sig->is_signed;
    signal_editor_.factor = sig->factor;
    signal_editor_.offset = sig->offset;
    signal_editor_.min = sig->min;
    signal_editor_.max = sig->max;
    signal_editor_.type = static_cast<int>(sig->type);
    signal_editor_.multiplex_value = sig->multiplex_value;
  } else {
    const std::string default_name = msg ? dbc()->newSignalName(selected_id_) : "NEW_SIGNAL_1";
    std::snprintf(signal_editor_.name.data(), signal_editor_.name.size(), "%s", default_name.c_str());
    std::snprintf(signal_editor_.receiver.data(), signal_editor_.receiver.size(), "%s", DEFAULT_NODE_NAME.c_str());
    signal_editor_.start_bit = nextAvailableSignalBit(msg, msg_size);
    signal_editor_.size = 1;
    signal_editor_.is_little_endian = true;
    signal_editor_.is_signed = false;
    signal_editor_.factor = 1.0;
    signal_editor_.offset = 0.0;
    signal_editor_.min = 0.0;
    signal_editor_.max = 1.0;
  }
  signal_editor_.open = true;
}

bool CabanaImguiApp::commitSignalEditor() {
  const auto *msg = selectedDbcMessage();
  const auto *sig = selectedSignal();
  if (dbc()->findDBCFile(selected_id_) == nullptr) {
    signal_editor_.error = "Open a DBC before editing signals.";
    return false;
  }
  if (signal_editor_.editing_existing && !sig) {
    signal_editor_.error = "Select a signal to edit.";
    return false;
  }

  const int msg_size = msg ? static_cast<int>(msg->size) : static_cast<int>(stream_->lastMessage(selected_id_).dat.size());
  // Normalize spaces to underscores (matching Qt's NameValidator)
  normalizeDbcIdentifier(signal_editor_.name.data());
  cabana::Signal updated = signal_editor_.editing_existing ? *sig : cabana::Signal{};
  updated.name = signal_editor_.name.data();
  updated.start_bit = signal_editor_.start_bit;
  updated.size = std::clamp(signal_editor_.size, 1, CAN_MAX_DATA_BYTES);
  updated.is_little_endian = signal_editor_.is_little_endian;
  updated.is_signed = signal_editor_.is_signed;
  updated.factor = signal_editor_.factor;
  updated.offset = signal_editor_.offset;
  updated.min = signal_editor_.min;
  updated.max = signal_editor_.max;
  updated.unit = signal_editor_.unit.data();
  updated.receiver_name = signal_editor_.receiver.data();
  updated.comment = signal_editor_.comment.data();
  updated.type = static_cast<cabana::Signal::Type>(signal_editor_.type);
  updated.multiplex_value = signal_editor_.type == 1 ? signal_editor_.multiplex_value : 0;
  updateMsbLsb(updated);

  if (updated.name.empty()) {
    signal_editor_.error = "Signal name is required.";
    return false;
  }
  if (!isValidDbcIdentifier(updated.name)) {
    signal_editor_.error = "Signal name must contain only letters, digits, and underscores.";
    return false;
  }
  // Qt allows factor=0, so we do too (matches Qt signal editing behavior)
  // Validate receiver name: must be word chars optionally comma-separated (matches Qt regex ^\w+(,\w+)*$)
  if (!updated.receiver_name.empty()) {
    bool valid_receiver = true;
    for (size_t i = 0; i < updated.receiver_name.size(); ++i) {
      char c = updated.receiver_name[i];
      if (c == ',') {
        // comma not at start/end, not consecutive
        if (i == 0 || i == updated.receiver_name.size() - 1 || updated.receiver_name[i - 1] == ',') {
          valid_receiver = false;
          break;
        }
      } else if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') {
        valid_receiver = false;
        break;
      }
    }
    if (!valid_receiver) {
      signal_editor_.error = "Receiver must contain only letters, digits, underscores, and commas.";
      return false;
    }
  }
  if (!signalFitsInMessage(updated, msg_size)) {
    signal_editor_.error = "Signal bits do not fit in the current message size.";
    return false;
  }
  if (msg) {
    if (const auto *duplicate = msg->sig(updated.name);
        duplicate && (!signal_editor_.editing_existing || updated.name != signal_editor_.original_name)) {
      signal_editor_.error = "There is already a signal with that name.";
      return false;
    }
  }

  signal_editor_.error.clear();
  if (signal_editor_.editing_existing) {
    UndoStack::push(new EditSignalCommand(selected_id_, sig, updated));
    setStatusMessage("Updated signal " + updated.name);
  } else {
    UndoStack::push(new AddSigCommand(selected_id_, updated));
    setStatusMessage("Added signal " + updated.name);
  }
  selected_signal_name_ = updated.name;
  hovered_signal_name_ = updated.name;
  return true;
}

void CabanaImguiApp::beginBinaryDrag(int signal_bit, int logical_bit, const cabana::Signal *covering) {
  binary_drag_ = {};
  binary_drag_.active = true;
  binary_drag_.press_bit = signal_bit;
  binary_drag_.press_logical = logical_bit;
  binary_drag_.current_bit = signal_bit;
  binary_drag_.current_logical = logical_bit;
  binary_drag_.covering_signal_name = covering ? covering->name : std::string();

  if (covering && (signal_bit == covering->lsb || signal_bit == covering->msb)) {
    binary_drag_.resize_signal_name = covering->name;
    binary_drag_.anchor_bit = signal_bit == covering->lsb ? covering->msb : covering->lsb;
    binary_drag_.anchor_logical = flipBitPos(binary_drag_.anchor_bit);
  } else {
    binary_drag_.anchor_bit = signal_bit;
    binary_drag_.anchor_logical = logical_bit;
  }
}

std::tuple<int, int, bool> CabanaImguiApp::currentBinarySelection() const {
  const auto *msg = selectedDbcMessage();
  const auto *resize_sig = msg && !binary_drag_.resize_signal_name.empty() ? msg->sig(binary_drag_.resize_signal_name) : nullptr;
  bool is_little_endian = true;
  if (resize_sig) {
    is_little_endian = resize_sig->is_little_endian;
  } else if (settings.drag_direction == Settings::DragDirection::MsbFirst) {
    is_little_endian = binary_drag_.current_logical < binary_drag_.anchor_logical;
  } else if (settings.drag_direction == Settings::DragDirection::LsbFirst) {
    is_little_endian = !(binary_drag_.current_logical < binary_drag_.anchor_logical);
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysLE) {
    is_little_endian = true;
  } else if (settings.drag_direction == Settings::DragDirection::AlwaysBE) {
    is_little_endian = false;
  }

  const int cur_bit = binary_drag_.current_bit;
  const int anchor_bit = binary_drag_.anchor_bit;
  const int start_bit = is_little_endian ? std::min(cur_bit, anchor_bit)
                                         : (binary_drag_.current_logical < binary_drag_.anchor_logical ? cur_bit : anchor_bit);
  const int size = is_little_endian ? std::abs(cur_bit - anchor_bit) + 1
                                    : std::abs(flipBitPos(cur_bit) - flipBitPos(anchor_bit)) + 1;
  return {start_bit, size, is_little_endian};
}

void CabanaImguiApp::finishBinaryDrag() {
  if (!binary_drag_.active) return;

  const auto *msg = selectedDbcMessage();
  const auto *resize_sig = msg && !binary_drag_.resize_signal_name.empty() ? msg->sig(binary_drag_.resize_signal_name) : nullptr;
  const bool moved = binary_drag_.current_logical != binary_drag_.press_logical;
  const std::string clicked_signal_name = binary_drag_.covering_signal_name;
  const auto [start_bit, size_bits, is_little_endian] = currentBinarySelection();
  binary_drag_.active = false;

  if (!moved) {
    if (!clicked_signal_name.empty()) {
      selected_signal_name_ = clicked_signal_name;
      hovered_signal_name_ = clicked_signal_name;
      signal_scroll_pending_ = true;
      signals_auto_open_ = true;
    }
    return;
  }

  if (dbc()->findDBCFile(selected_id_) == nullptr) {
    setStatusMessage("Open a DBC before editing signals");
    return;
  }

  const int msg_size = msg ? static_cast<int>(msg->size) : static_cast<int>(stream_->lastMessage(selected_id_).dat.size());
  cabana::Signal updated = resize_sig ? *resize_sig : cabana::Signal{};
  updated.start_bit = start_bit;
  updated.size = size_bits;
  updated.is_little_endian = is_little_endian;
  updateMsbLsb(updated);
  if (!signalFitsInMessage(updated, msg_size)) {
    setStatusMessage("Signal bits do not fit in the current message size", 3000);
    return;
  }

  if (resize_sig) {
    UndoStack::push(new EditSignalCommand(selected_id_, resize_sig, updated));
    selected_signal_name_ = updated.name;
    hovered_signal_name_ = updated.name;
    setStatusMessage("Resized signal " + updated.name);
  } else {
    updated.name = dbc()->newSignalName(selected_id_);
    UndoStack::push(new AddSigCommand(selected_id_, updated));
    selected_signal_name_ = updated.name;
    hovered_signal_name_ = updated.name;
    setStatusMessage("Added signal " + updated.name);
  }
}

void CabanaImguiApp::toggleSelectedSignalPlot(bool merge) {
  auto *sig = selectedSignal();
  if (!sig || !has_selected_id_) return;
  bool plotted = findChart(selected_id_, sig) != nullptr;
  showChart(selected_id_, sig, !plotted, merge || ImGui::GetIO().KeyShift);
}

void CabanaImguiApp::removeSelectedSignal() {
  if (const auto *sig = selectedSignal()) {
    UndoStack::push(new RemoveSigCommand(selected_id_, sig));
    selected_signal_name_.clear();
    hovered_signal_name_.clear();
  }
}

void CabanaImguiApp::updateSignalEndian(bool little_endian) {
  if (const auto *sig = selectedSignal()) {
    cabana::Signal s = *sig;
    s.is_little_endian = little_endian;
    s.start_bit = flipBitPos(s.start_bit);
    UndoStack::push(new EditSignalCommand(selected_id_, sig, s));
  }
}

void CabanaImguiApp::updateSignalSigned(bool is_signed) {
  if (const auto *sig = selectedSignal()) {
    cabana::Signal s = *sig;
    s.is_signed = is_signed;
    UndoStack::push(new EditSignalCommand(selected_id_, sig, s));
  }
}

std::vector<std::array<uint32_t, 8>> CabanaImguiApp::bitFlipCounts(size_t msg_size) const {
  const auto time_range = stream_->timeRange();
  if (range_bit_flips_ && range_bit_flips_window_ == time_range && range_bit_flips_id_ == selected_id_) {
    return *range_bit_flips_;
  }

  std::vector<std::array<uint32_t, 8>> flips(msg_size, std::array<uint32_t, 8>{});
  auto [first, last] = stream_->eventsInRange(selected_id_, time_range);
  if (std::distance(first, last) > 1) {
    std::vector<uint8_t> prev((*first)->dat, (*first)->dat + std::min<size_t>(msg_size, (*first)->size));
    for (auto it = std::next(first); it != last; ++it) {
      const CanEvent *event = *it;
      int size = std::min<int>(msg_size, event->size);
      if (prev.size() < static_cast<size_t>(size)) prev.resize(size);
      for (int i = 0; i < size; ++i) {
        uint8_t diff = event->dat[i] ^ prev[i];
        if (!diff) continue;
        for (int bit = 0; bit < 8; ++bit) {
          if (diff & (1u << bit)) ++flips[i][7 - bit];
        }
        prev[i] = event->dat[i];
      }
    }
  }
  range_bit_flips_window_ = time_range;
  range_bit_flips_id_ = selected_id_;
  range_bit_flips_ = flips;
  return flips;
}

void CabanaImguiApp::refreshState() {
  ensureAutoDbcLoaded();
  message_items_ = buildMessageItems();
  ensureSelectionState();
  ensureDetailTabs();
  ensureChartTabs();
  ensureSignalState();

  signal_values_.clear();
  if (const auto *msg = selectedDbcMessage()) {
    const auto &last = stream_->lastMessage(selected_id_);
    for (const auto *sig : msg->getSignals()) {
      double value = 0.0;
      if (sig->getValue(last.dat.data(), last.dat.size(), &value)) {
        signal_values_[sig->name] = value;
      }
    }
    // Build sparklines from events within sparkline_range seconds, throttled to every 10 frames
    const auto &events = stream_->events(selected_id_);
    if (!events.empty() && (frame_count_ % 10 == 0 || sparklines_.empty())) {
      sparklines_.clear();
      constexpr int kMaxSamples = 48;
      // Use sparkline_range setting to limit time window (like Qt)
      double cur_sec = stream_->currentSec();
      double range_sec = std::clamp(settings.sparkline_range, 1, 30);
      uint64_t range_start = can->toMonoTime(std::max(0.0, cur_sec - range_sec));
      auto first_it = std::lower_bound(events.cbegin(), events.cend(), range_start, CompareCanEvent());
      int range_count = static_cast<int>(events.cend() - first_it);
      int step = std::max(1, range_count / kMaxSamples);
      for (const auto *sig : msg->getSignals()) {
        SparklineData sp;
        sp.min = FLT_MAX;
        sp.max = -FLT_MAX;
        sp.values.reserve(kMaxSamples);
        int value_count = 0;
        for (int idx = 0; idx < range_count; idx += step) {
          const CanEvent *ev = *(first_it + idx);
          double val = 0.0;
          if (sig->getValue(ev->dat, ev->size, &val)) {
            float fv = static_cast<float>(val);
            sp.values.push_back(fv);
            sp.min = std::min(sp.min, fv);
            sp.max = std::max(sp.max, fv);
            ++value_count;
          }
        }
        // Compute frequency for multiplexed signals (matches Qt sparkline freq display)
        if (range_sec > 0 && value_count > 0) {
          sp.freq = static_cast<float>(value_count * step) / static_cast<float>(range_sec);
        }
        if (sp.values.size() > 1) {
          if (std::abs(sp.max - sp.min) < 1e-6f) { sp.min -= 1.0f; sp.max += 1.0f; }
          sparklines_[sig->name] = std::move(sp);
        }
      }
    }
  }
}

std::string CabanaImguiApp::formatTime(double sec, bool include_milliseconds) const {
  if (!stream_) return "0";
  if (absolute_time_) {
    sec = stream_->beginDateTimeSecs() + sec;
  }
  return utils::formatSeconds(sec, include_milliseconds, absolute_time_);
}

const cabana::Msg *CabanaImguiApp::selectedDbcMessage() const {
  return has_selected_id_ ? dbc()->msg(selected_id_) : nullptr;
}

const MessageListItem *CabanaImguiApp::selectedItem() const {
  if (!has_selected_id_) return nullptr;
  // First check the filtered list
  auto it = std::find_if(message_items_.begin(), message_items_.end(), [&](const auto &item) { return item.id == selected_id_; });
  if (it != message_items_.end()) return &(*it);
  // Message is filtered out of the table but still valid (Qt keeps detail view open
  // independently of filters).  Build a cached item from DBC/stream data.
  const auto *msg = dbc()->msg(selected_id_);
  selected_item_cache_ = {
    .id = selected_id_,
    .name = msg ? msg->name : msgName(selected_id_),
    .node = msg ? msg->transmitter : std::string(),
    .data_hex = {},
    .freq = 0.0,
    .count = 0,
    .active = false,
  };
  if (stream_) {
    const auto &data = stream_->lastMessage(selected_id_);
    selected_item_cache_.data_hex = utils::toHex(data.dat);
    selected_item_cache_.freq = data.freq;
    selected_item_cache_.count = data.count;
    selected_item_cache_.active = stream_->isMessageActive(selected_id_);
  }
  if (msg) {
    for (const auto *sig : msg->sigs) selected_item_cache_.signal_names.push_back(sig->name);
  }
  return &selected_item_cache_;
}

const cabana::Signal *CabanaImguiApp::selectedSignal() const {
  const auto *msg = selectedDbcMessage();
  return msg && !selected_signal_name_.empty() ? msg->sig(selected_signal_name_) : nullptr;
}

const cabana::Signal *CabanaImguiApp::hoveredSignal() const {
  const auto *msg = selectedDbcMessage();
  return msg && !hovered_signal_name_.empty() ? msg->sig(hovered_signal_name_) : nullptr;
}


std::vector<MessageListItem> CabanaImguiApp::buildMessageItems() const {
  if (!stream_) return {};

  std::vector<MessageId> can_ids;
  can_ids.reserve(stream_->lastMessages().size());
  for (const auto &[id, _] : stream_->lastMessages()) can_ids.push_back(id);

  std::set<MessageId> dbc_ids;
  for (const auto &[_, msg] : dbc()->getMessages(-1)) {
    dbc_ids.insert(MessageId{.source = INVALID_SOURCE, .address = msg.address});
  }

  std::vector<MessageListItem> items;
  const auto merged_ids = mergeMessageIds(can_ids, dbc_ids);
  items.reserve(merged_ids.size());
  for (const auto &id : merged_ids) {
    const auto *msg = dbc()->msg(id);
    const auto &data = stream_->lastMessage(id);
    MessageListItem item = {
      .id = id,
      .name = msg ? msg->name : UNTITLED,
      .node = msg ? msg->transmitter : std::string(),
      .data_hex = utils::toHex(data.dat),
      .freq = data.freq,
      .count = data.count,
      .active = stream_->isMessageActive(id),
    };
    if (msg) {
      for (const auto *sig : msg->sigs) item.signal_names.push_back(sig->name);
    }
    items.push_back(std::move(item));
  }
  return filterAndSortMessageList(items, filter_);
}

void CabanaImguiApp::maybeCaptureScreenshot(int drawable_width, int drawable_height) {
  ++frame_count_;
  const char *screenshot_path = std::getenv("CABANA_IMGUI_SCREENSHOT");
  if (!screenshot_path) return;
  if (frame_count_ < 120) return;
  if (stream_ && message_items_.empty()) return;
  if (!chart_tabs_.empty() && !chart_tabs_.front().charts.empty() && signal_values_.empty()) return;
  if (video_ && !video_rendered_this_frame_) return;

  saveScreenshot(screenshot_path, drawable_width, drawable_height);
  if (const char *exit_after = std::getenv("CABANA_IMGUI_EXIT_AFTER_SCREENSHOT"); exit_after && std::string(exit_after) == "1") {
    exit_requested_ = true;
  }
}
