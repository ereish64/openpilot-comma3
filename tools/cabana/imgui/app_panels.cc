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

namespace {
constexpr std::array<float, 11> kPlaybackSpeeds = {0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 0.8f, 1.0f, 2.0f, 3.0f, 5.0f};

ImVec2 fitImageSize(const ImVec2 &bounds, float aspect) {
  aspect = std::max(aspect, 1e-3f);
  ImVec2 size(bounds.x, bounds.x / aspect);
  if (size.y > bounds.y) {
    size.y = bounds.y;
    size.x = size.y * aspect;
  }
  return size;
}

void cropFillUvs(float source_aspect, float container_aspect, bool mirror_x, ImVec2 *uv0, ImVec2 *uv1) {
  source_aspect = std::max(source_aspect, 1e-3f);
  container_aspect = std::max(container_aspect, 1e-3f);

  float u0 = 0.0f, u1 = 1.0f, v0 = 0.0f, v1 = 1.0f;
  if (source_aspect > container_aspect) {
    const float visible_u = std::clamp(container_aspect / source_aspect, 0.0f, 1.0f);
    const float inset = (1.0f - visible_u) * 0.5f;
    u0 = inset;
    u1 = 1.0f - inset;
  } else {
    const float visible_v = std::clamp(source_aspect / container_aspect, 0.0f, 1.0f);
    const float inset = (1.0f - visible_v) * 0.5f;
    v0 = inset;
    v1 = 1.0f - inset;
  }

  *uv0 = ImVec2(mirror_x ? u1 : u0, v0);
  *uv1 = ImVec2(mirror_x ? u0 : u1, v1);
}
}  // namespace

double CabanaImguiApp::timelineSecFromMouseX(double min_sec, double max_sec, float slider_x, float slider_w, float mouse_x) const {
  if (slider_w <= 0.0f || max_sec <= min_sec) return min_sec;
  float t = std::clamp((mouse_x - slider_x) / slider_w, 0.0f, 1.0f);
  return min_sec + (max_sec - min_sec) * t;
}

void CabanaImguiApp::drawTimelineStrip(ImDrawList *draw, const ImVec2 &slider_pos, const ImVec2 &slider_size,
                                       double min_sec, double max_sec, double current_sec,
                                       std::optional<std::pair<double, double>> highlight_range,
                                       double hover_sec, bool show_missing_segments) const {
  if (!draw || slider_size.x <= 0.0f || slider_size.y <= 0.0f || max_sec <= min_sec) return;

  const auto secToX = [&](double sec) {
    double t = (sec - min_sec) / std::max(0.001, max_sec - min_sec);
    return slider_pos.x + static_cast<float>(t * slider_size.x);
  };

  draw->AddRectFilled(slider_pos, ImVec2(slider_pos.x + slider_size.x, slider_pos.y + slider_size.y),
                      packedColor(timeline_colors[(int)TimelineType::None]), 0.0f);

  if (Replay *r = replay()) {
    if (const auto timeline = r->getTimeline()) {
      for (const auto &entry : *timeline) {
        float x0 = secToX(std::clamp(entry.start_time, min_sec, max_sec));
        float x1 = secToX(std::clamp(entry.end_time, min_sec, max_sec));
        if (x1 <= slider_pos.x || x0 >= slider_pos.x + slider_size.x || x1 <= x0) continue;
        draw->AddRectFilled(ImVec2(x0, slider_pos.y), ImVec2(x1, slider_pos.y + slider_size.y),
                            packedColor(timeline_colors[(int)entry.type]), 0.0f);
      }
    }

    if (show_missing_segments) {
      if (auto event_data = r->getEventData()) {
        CabanaColor empty_color(40, 40, 42, 180);
        for (const auto &[n, _] : r->route().segments()) {
          if (!event_data->isSegmentLoaded(n)) {
            float x0 = secToX(n * 60.0);
            float x1 = secToX((n + 1) * 60.0);
            if (x1 <= slider_pos.x || x0 >= slider_pos.x + slider_size.x || x1 <= x0) continue;
            draw->AddRectFilled(ImVec2(x0, slider_pos.y), ImVec2(x1, slider_pos.y + slider_size.y), packedColor(empty_color), 0.0f);
          }
        }
      }
    }
  }

  if (highlight_range) {
    const double hi_min = std::clamp(highlight_range->first, min_sec, max_sec);
    const double hi_max = std::clamp(highlight_range->second, min_sec, max_sec);
    if (hi_max > hi_min) {
      const float x0 = secToX(hi_min);
      const float x1 = secToX(hi_max);
      draw->AddRectFilled(ImVec2(x0, slider_pos.y), ImVec2(x1, slider_pos.y + slider_size.y), IM_COL32(255, 255, 255, 28), 0.0f);
      draw->AddLine(ImVec2(x0, slider_pos.y - 1.0f), ImVec2(x0, slider_pos.y + slider_size.y + 1.0f), IM_COL32(225, 225, 225, 190), 1.5f);
      draw->AddLine(ImVec2(x1, slider_pos.y - 1.0f), ImVec2(x1, slider_pos.y + slider_size.y + 1.0f), IM_COL32(225, 225, 225, 190), 1.5f);
    }
  }

  if (hover_sec >= min_sec && hover_sec <= max_sec) {
    float hover_x = secToX(hover_sec);
    draw->AddLine(ImVec2(hover_x, slider_pos.y), ImVec2(hover_x, slider_pos.y + slider_size.y), IM_COL32(255, 200, 50, 180), 1.5f);
  }

  float cursor_x = secToX(std::clamp(current_sec, min_sec, max_sec));
  draw->AddLine(ImVec2(cursor_x, slider_pos.y - 1.0f), ImVec2(cursor_x, slider_pos.y + slider_size.y + 1.0f), IM_COL32(255, 255, 255, 220), 2.0f);
}

void CabanaImguiApp::drawMessagesPanel(const ImVec2 &size) {
  ImGui::BeginChild("MessagesPanel", size, ImGuiChildFlags_Borders);
  // Title reflects all active filters (updated after column filter application below)
  ImGui::TextUnformatted(msg_panel_title_.c_str());

  ImGui::BeginDisabled(stream_ == nullptr);
  if (ImGui::Button("Suppress Highlighted")) {
    suppressed_count_ = static_cast<int>(stream_->suppressHighlighted());
  }
  ImGui::SameLine();
  if (ImGui::Button(suppressed_count_ > 0 ? ("Clear (" + std::to_string(suppressed_count_) + ")").c_str() : "Clear")) {
    stream_->clearSuppressed();
    suppressed_count_ = 0;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  {
    bool prev_suppress = suppress_defined_signals_;
    ImGui::Checkbox("Suppress Signals", &suppress_defined_signals_);
    if (suppress_defined_signals_ != prev_suppress && stream_) {
      stream_->suppressDefinedSignals(suppress_defined_signals_);
    }
  }

  ImGui::Checkbox("Show inactive", &filter_.show_inactive_messages);
  ImGui::SameLine();
  ImGui::Checkbox("Multi-line", &multiline_bytes_);
  ImGui::Separator();

  if (!stream_) {
    ImGui::TextDisabled("No stream loaded.");
    ImGui::Spacing();
    for (const auto &line : startupSummary()) {
      ImGui::BulletText("%s", line.c_str());
    }
    ImGui::EndChild();
    return;
  }

  ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_ScrollX | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable |
                          ImGuiTableFlags_Hideable | ImGuiTableFlags_Reorderable;
  if (ImGui::BeginTable("MessagesTable", 6, flags, ImVec2(0, 0))) {
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoHide);
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    ImGui::TableSetupColumn("Node", ImGuiTableColumnFlags_WidthFixed, 86.0f);
    ImGui::TableSetupColumn("Freq", ImGuiTableColumnFlags_WidthFixed, 60.0f);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_NoSort);
    ImGui::TableSetupScrollFreeze(0, 2);
    ImGui::TableHeadersRow();

    // Per-column filter row
    ImGui::TableNextRow();
    ImGui::TableSetColumnIndex(0);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##f_name", "Filter", text_filter_.data(), text_filter_.size());
    if (text_filter_[0] == '\0') filter_.filters.erase(0);
    else filter_.filters[0] = text_filter_.data();
    ImGui::TableSetColumnIndex(1);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##f_bus", "Bus", col_filter_bus_.data(), col_filter_bus_.size());
    ImGui::TableSetColumnIndex(2);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##f_addr", "Addr", col_filter_addr_.data(), col_filter_addr_.size());
    ImGui::TableSetColumnIndex(3);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##f_node", "Node", col_filter_node_.data(), col_filter_node_.size());
    ImGui::TableSetColumnIndex(4);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##f_freq", "Hz", col_filter_freq_.data(), col_filter_freq_.size());
    ImGui::TableSetColumnIndex(5);
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##f_bytes", "Bytes", col_filter_bytes_.data(), col_filter_bytes_.size());

    if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs(); sort_specs && sort_specs->SpecsCount > 0) {
      if (sort_specs->SpecsDirty) {
        const ImGuiTableColumnSortSpecs &sort = sort_specs->Specs[0];
        filter_.sort_column = sort.ColumnIndex;
        filter_.descending = sort.SortDirection == ImGuiSortDirection_Descending;
        refreshState();
        sort_specs->SpecsDirty = false;
      }
    }

    // Build filtered indices for per-column filters
    const std::string f_bus(col_filter_bus_.data());
    const std::string f_addr(col_filter_addr_.data());
    const std::string f_node(col_filter_node_.data());
    const std::string f_freq(col_filter_freq_.data());
    const std::string f_bytes(col_filter_bytes_.data());
    const bool has_col_filters = !f_bus.empty() || !f_addr.empty() || !f_node.empty() || !f_freq.empty() || !f_bytes.empty();

    auto match_range = [](const std::string &filter, double value) -> bool {
      if (filter.empty()) return true;
      auto dash = filter.find('-');
      if (dash != std::string::npos) {
        // Ensure at most one dash (reject invalid like "1-2-3")
        if (filter.find('-', dash + 1) != std::string::npos) return false;
        std::string first = filter.substr(0, dash);
        std::string second = filter.substr(dash + 1);
        double lo = 0.0, hi = 1e18;
        char *end = nullptr;
        if (!first.empty()) { lo = std::strtod(first.c_str(), &end); if (end == first.c_str()) return false; }
        if (!second.empty()) { hi = std::strtod(second.c_str(), &end); if (end == second.c_str()) return false; }
        return value >= lo && value <= hi;
      }
      char *end = nullptr;
      double target = std::strtod(filter.c_str(), &end);
      if (end == filter.c_str()) return false;  // not a valid number
      return static_cast<int>(value) == static_cast<int>(target);
    };

    auto icontains = [](const std::string &haystack, const std::string &needle) -> bool {
      if (needle.empty()) return true;
      return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
                         [](char a, char b) { return std::tolower(a) == std::tolower(b); }) != haystack.end();
    };

    auto match_hex = [](const std::string &filter, uint32_t address) -> bool {
      if (filter.empty()) return true;
      // Support hex range syntax: "100-1FF" or "0x100-0x1FF"
      auto dash = filter.find('-');
      if (dash != std::string::npos && dash > 0 && dash < filter.size() - 1) {
        std::string lo_str = filter.substr(0, dash);
        std::string hi_str = filter.substr(dash + 1);
        char *end_lo = nullptr, *end_hi = nullptr;
        unsigned long lo = std::strtoul(lo_str.c_str(), &end_lo, 16);
        unsigned long hi = std::strtoul(hi_str.c_str(), &end_hi, 16);
        if (end_lo != lo_str.c_str() && end_hi != hi_str.c_str()) {
          return address >= lo && address <= hi;
        }
      }
      char addr_hex[16];
      std::snprintf(addr_hex, sizeof(addr_hex), "0x%X", address);
      std::string addr_str(addr_hex);
      // Case-insensitive contains
      std::string f_lower = filter;
      std::transform(f_lower.begin(), f_lower.end(), f_lower.begin(), ::tolower);
      std::string a_lower = addr_str;
      std::transform(a_lower.begin(), a_lower.end(), a_lower.begin(), ::tolower);
      return a_lower.find(f_lower) != std::string::npos;
    };

    std::vector<int> filtered_indices;
    if (has_col_filters) {
      filtered_indices.reserve(message_items_.size());
      for (int i = 0; i < static_cast<int>(message_items_.size()); ++i) {
        const auto &item = message_items_[i];
        if (!f_bus.empty() && !match_range(f_bus, item.id.source)) continue;
        if (!f_addr.empty() && !match_hex(f_addr, item.id.address)) continue;
        if (!f_node.empty() && !icontains(item.node, f_node)) continue;
        if (!f_freq.empty() && !match_range(f_freq, item.freq)) continue;
        if (!f_bytes.empty() && !icontains(item.data_hex, f_bytes)) continue;
        filtered_indices.push_back(i);
      }
    }

    const int row_count = has_col_filters ? static_cast<int>(filtered_indices.size()) : static_cast<int>(message_items_.size());

    // Update title counts reflecting all active filters (matching Qt's updateTitle)
    {
      size_t dbc_count = 0, signal_count = 0;
      for (int i = 0; i < row_count; ++i) {
        const int idx = has_col_filters ? filtered_indices[i] : i;
        if (const auto *msg = dbc()->msg(message_items_[idx].id)) {
          ++dbc_count;
          signal_count += msg->sigs.size();
        }
      }
      char buf[128];
      std::snprintf(buf, sizeof(buf), "%d Messages (%zu DBC Messages, %zu Signals)", row_count, dbc_count, signal_count);
      msg_panel_title_ = buf;
    }

    ImGuiListClipper clipper;
    clipper.Begin(row_count);
    while (clipper.Step()) {
      for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
        const int idx = has_col_filters ? filtered_indices[row] : row;
        const auto &item = message_items_[idx];
        ImGui::TableNextRow();
        ImGui::PushID(item.id.toString().c_str());
        const bool is_selected = has_selected_id_ && selected_id_ == item.id;
        // Color the whole row for inactive items (matches Qt's disabled text palette)
        if (!item.active) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.60f, 0.60f, 0.60f, 1.0f));

        ImGui::TableSetColumnIndex(0);
        // No hover highlight — only show selected/pressed state (matching Qt cabana)
        if (!is_selected) {
          ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
        }
        if (ImGui::Selectable(item.name.c_str(), is_selected, ImGuiSelectableFlags_SpanAllColumns)) {
          activateMessage(item.id);
        }
        if (!is_selected) {
          ImGui::PopStyleColor();
        }
        if (ImGui::BeginPopupContextItem(("msg_ctx_" + item.id.toString()).c_str())) {
          activateMessage(item.id);
          if (ImGui::MenuItem("Edit Message")) openMessageEditor();
          if (ImGui::MenuItem("Export CSV...")) exportToCsvDialog(item.id);
          const auto *m = dbc()->msg(item.id);
          if (m && !m->getSignals().empty()) {
            if (ImGui::MenuItem("Plot All Signals")) {
              currentCharts().clear();
              for (const auto *sig : m->getSignals()) {
                showChart(item.id, sig, true, false);
              }
            }
            if (ImGui::MenuItem("Plot All Signals (Merged)")) {
              for (const auto *sig : m->getSignals()) {
                showChart(item.id, sig, true, true);
              }
            }
          }
          ImGui::EndPopup();
        }

        const bool is_dbc_only = item.id.source == INVALID_SOURCE;
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(is_dbc_only ? "N/A" : std::to_string(item.id.source).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::Text("0x%X", item.id.address);
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(item.node.empty() ? "-" : item.node.c_str());
        ImGui::TableSetColumnIndex(4);
        if (is_dbc_only) {
          ImGui::TextUnformatted("N/A");
        } else if (item.freq >= 0.95) {
          ImGui::Text("%.0f", item.freq);
        } else if (item.freq > 0.0) {
          ImGui::Text("%.2f", item.freq);
        } else {
          ImGui::TextUnformatted("-");
        }
        ImGui::TableSetColumnIndex(5);
        if (is_dbc_only) {
          ImGui::TextUnformatted("N/A");
        } else {
          // Color-coded hex bytes matching Qt Cabana (monospace so columns don't shift)
          ImGui::PushFont(cabanaMonoFont());
          const auto &last = stream_ ? stream_->lastMessage(item.id) : CanData{};
          if (last.dat.empty()) {
            ImGui::TextUnformatted(item.data_hex.empty() ? "-" : item.data_hex.c_str());
          } else if (multiline_bytes_) {
            // Multi-line: 8 bytes per line
            for (size_t b = 0; b < last.dat.size(); ++b) {
              if (b > 0 && b % 8 == 0) { /* new line automatically */ }
              else if (b > 0) { ImGui::SameLine(0, 0); ImGui::TextUnformatted(" "); ImGui::SameLine(0, 0); }
              char hex[4];
              std::snprintf(hex, sizeof(hex), "%02X", last.dat[b]);
              if (b < last.colors.size() && last.colors[b].isValid() && last.colors[b].alpha() > 0) {
                ImGui::TextColored(imColor(last.colors[b]), "%s", hex);
              } else {
                ImGui::TextUnformatted(hex);
              }
            }
          } else {
            for (size_t b = 0; b < last.dat.size(); ++b) {
              if (b > 0) { ImGui::SameLine(0, 0); ImGui::TextUnformatted(" "); ImGui::SameLine(0, 0); }
              char hex[4];
              std::snprintf(hex, sizeof(hex), "%02X", last.dat[b]);
              if (b < last.colors.size() && last.colors[b].isValid() && last.colors[b].alpha() > 0) {
                ImGui::TextColored(imColor(last.colors[b]), "%s", hex);
              } else {
                ImGui::TextUnformatted(hex);
              }
            }
          }
          ImGui::PopFont();
        }
        if (!item.active) ImGui::PopStyleColor();
        ImGui::PopID();
      }
    }
    ImGui::EndTable();
  }
  ImGui::EndChild();
}

void CabanaImguiApp::drawVideoPanel(const ImVec2 &size) {
  ImGui::BeginChild("VideoPanel", size, ImGuiChildFlags_Borders);
  const std::string title = stream_
                                ? CabanaWorkspaceState{
                                    .has_stream = true,
                                    .live_streaming = stream_->liveStreaming(),
                                    .route_name = stream_->routeName(),
                                    .car_fingerprint = stream_->carFingerprint(),
                                  }.videoPanelTitle()
                                : std::string("Video");
  ImGui::TextUnformatted(title.c_str());
  const bool is_dummy = !stream_ || dynamic_cast<DummyStream *>(stream_);
  const bool paused = stream_ && stream_->isPaused();
  auto drawPausedOverlay = [&](const ImVec2 &image_min, const ImVec2 &image_max) {
    if (!paused) return;

    ImDrawList *overlay_draw = ImGui::GetWindowDrawList();
    const char *paused_text = "PAUSED";
    ImFont *font = cabanaBoldFont();
    const float font_size = font ? font->LegacySize : std::round(std::max(ImGui::GetFontSize(), 16.0f * cabanaUiScale()));
    const ImVec2 text_size = font ? font->CalcTextSizeA(font_size, 10000.0f, 0.0f, paused_text)
                                  : ImGui::CalcTextSize(paused_text);
    const float x = std::round((image_min.x + image_max.x - text_size.x) * 0.5f);
    const float y = std::round((image_min.y + image_max.y - text_size.y) * 0.5f);
    const ImU32 text_col = IM_COL32(200, 200, 200, static_cast<int>(255.0f * 0.7f));
    overlay_draw->AddText(font, font_size, ImVec2(x, y), text_col, paused_text);
    if (font == ImGui::GetIO().FontDefault) {
      overlay_draw->AddText(font, font_size, ImVec2(x + 1.0f, y), text_col, paused_text);
    }
  };
  if (!is_dummy) {
    ImGui::SameLine();
    ImGui::TextDisabled("%s", stream_->liveStreaming() ? "live" : "replay");
  }

  if (video_) {
    const auto streams = video_->streams();
    if (!streams.empty()) {
      if (ImGui::BeginTabBar("camera_tabs", ImGuiTabBarFlags_FittingPolicyResizeDown)) {
        for (auto type : streams) {
          ImGuiTabItemFlags flags = video_->requestedStream() == type ? ImGuiTabItemFlags_SetSelected : 0;
          if (ImGui::BeginTabItem(streamLabel(type).c_str(), nullptr, flags)) {
            video_->setRequestedStream(type);
            ImGui::EndTabItem();
          }
        }
        ImGui::EndTabBar();
      }
    }
  }
  ImGui::Separator();

  if (!stream_) {
    ImGui::TextDisabled("No stream loaded.");
    ImGui::Spacing();
    for (const auto &line : startupSummary()) {
      ImGui::BulletText("%s", line.c_str());
    }
    ImGui::EndChild();
    return;
  }

  const float avail_width = ImGui::GetContentRegionAvail().x;
  const float controls_h = stream_ && !stream_->liveStreaming() ? 104.0f : 72.0f;
  const float avail_height = std::max(120.0f, ImGui::GetContentRegionAvail().y - controls_h);
  const ImVec2 video_bounds(avail_width, avail_height);
  const bool crop_video_to_fill = settings.video_crop_to_fill;

  ImDrawList *draw = ImGui::GetWindowDrawList();
  if (video_) {
    const GLuint texture = video_->texture();
    const ImVec2 tex_size = video_->textureSize();
    const float aspect = texture != 0 && tex_size.x > 0.0f && tex_size.y > 0.0f ? tex_size.x / tex_size.y : 16.0f / 9.0f;
    const ImVec2 image_size = crop_video_to_fill ? video_bounds : fitImageSize(video_bounds, aspect);
    if (!crop_video_to_fill) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_width - image_size.x) * 0.5f);
    }
    if (texture != 0) {
      const bool is_driver = video_->activeStream() == VISION_STREAM_DRIVER;
      ImVec2 uv0 = is_driver ? ImVec2(1, 0) : ImVec2(0, 0);
      ImVec2 uv1 = is_driver ? ImVec2(0, 1) : ImVec2(1, 1);
      if (crop_video_to_fill) {
        cropFillUvs(aspect, video_bounds.x / video_bounds.y, is_driver, &uv0, &uv1);
      }
      ImGui::Image((ImTextureID)(intptr_t)texture, image_size, uv0, uv1);
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        stream_->pause(!paused);
      }
      video_rendered_this_frame_ = true;
    } else {
      ImGui::Dummy(image_size);
    }
    const ImVec2 image_min = ImGui::GetItemRectMin();
    const ImVec2 image_max = ImGui::GetItemRectMax();
    if (texture == 0) {
      draw->AddRectFilled(image_min, image_max, IM_COL32(40, 40, 42, 255), 0.0f);
      const char *waiting_text = "Waiting for camera frames...";
      ImVec2 text_size = ImGui::CalcTextSize(waiting_text);
      draw->AddText(ImVec2((image_min.x + image_max.x - text_size.x) * 0.5f, (image_min.y + image_max.y - text_size.y) * 0.5f),
                    IM_COL32(187, 187, 187, 255), waiting_text);
    }
    if (Replay *r = replay()) {
      if (auto alert = r->findAlertAtTime(stream_->currentSec())) {
        drawAlertOverlay(draw, *alert, image_min.x + 8.0f, image_min.y + 8.0f, image_size.x - 16.0f);
      }
    }
    drawPausedOverlay(image_min, image_max);
    {
      double hover_sec = timeline_hover_sec_ >= 0 ? timeline_hover_sec_ : chart_hover_sec_;
      if (hover_sec >= 0 && !thumbnails_.empty() && stream_) {
        uint64_t hover_mono = stream_->toMonoTime(hover_sec);
        auto it = thumbnails_.upper_bound(hover_mono);
        if (it != thumbnails_.begin()) {
          --it;
          auto &thumb = it->second;
          if (thumb.tex_id) {
            const bool is_paused = stream_->isPaused();
            float thumb_aspect = static_cast<float>(thumb.width) / std::max(1, thumb.height);
            if (is_paused) {
              ImVec2 fill_size = crop_video_to_fill ? image_size : fitImageSize(image_size, thumb_aspect);
              ImVec2 fill_pos((image_min.x + image_max.x - fill_size.x) * 0.5f,
                              (image_min.y + image_max.y - fill_size.y) * 0.5f);
              ImVec2 thumb_uv0(0, 0);
              ImVec2 thumb_uv1(1, 1);
              if (crop_video_to_fill) {
                cropFillUvs(thumb_aspect, image_size.x / image_size.y, false, &thumb_uv0, &thumb_uv1);
              }
              draw->AddRectFilled(image_min, image_max, IM_COL32(0, 0, 0, 200), 0.0f);
              draw->AddImage((ImTextureID)(intptr_t)thumb.tex_id, fill_pos,
                             ImVec2(fill_pos.x + fill_size.x, fill_pos.y + fill_size.y), thumb_uv0, thumb_uv1);
              std::string time_label = formatTime(hover_sec, false);
              ImVec2 label_size = ImGui::CalcTextSize(time_label.c_str());
              draw->AddRectFilled(ImVec2(fill_pos.x, fill_pos.y + fill_size.y - label_size.y - 8),
                                  ImVec2(fill_pos.x + label_size.x + 12, fill_pos.y + fill_size.y),
                                  IM_COL32(0, 0, 0, 160), 3.0f);
              draw->AddText(ImVec2(fill_pos.x + 6, fill_pos.y + fill_size.y - label_size.y - 4),
                            IM_COL32(255, 255, 255, 220), time_label.c_str());
              if (Replay *r = replay()) {
                if (auto alert = r->findAlertAtTime(hover_sec)) {
                  drawAlertOverlay(draw, *alert, fill_pos.x + 8.0f, fill_pos.y + 8.0f, fill_size.x - 16.0f);
                }
              }
            } else {
              float tw = static_cast<float>(thumb.width);
              float th = static_cast<float>(thumb.height);
              auto [min_sec, max_sec] = std::make_pair(stream_->minSeconds(), stream_->maxSeconds());
              float pos_frac = (max_sec > min_sec) ? static_cast<float>((hover_sec - min_sec) / (max_sec - min_sec)) : 0.5f;
              float pos_x = image_min.x + pos_frac * image_size.x;
              float tx = std::clamp(pos_x - tw * 0.5f, image_min.x + 4.0f, image_max.x - tw - 4.0f);
              float ty = image_max.y - th - 8.0f;
              draw->AddRect(ImVec2(tx - 1, ty - 1), ImVec2(tx + tw + 1, ty + th + 1), IM_COL32(255, 255, 255, 255));
              draw->AddImage((ImTextureID)(intptr_t)thumb.tex_id, ImVec2(tx, ty), ImVec2(tx + tw, ty + th));
              std::string time_label = formatTime(hover_sec, false);
              ImVec2 label_size = ImGui::CalcTextSize(time_label.c_str());
              draw->AddRectFilled(ImVec2(tx, ty + th - label_size.y - 6),
                                  ImVec2(tx + label_size.x + 10, ty + th),
                                  IM_COL32(0, 0, 0, 160), 3.0f);
              draw->AddText(ImVec2(tx + 5, ty + th - label_size.y - 3),
                            IM_COL32(255, 255, 255, 220), time_label.c_str());
            }
          }
        }
      }
    }
  } else if (Replay *r = replay()) {
    const ImVec2 image_size = crop_video_to_fill ? video_bounds : fitImageSize(video_bounds, 16.0f / 9.0f);
    if (!crop_video_to_fill) {
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail_width - image_size.x) * 0.5f);
    }
    ImGui::Dummy(image_size);
    const ImVec2 image_min = ImGui::GetItemRectMin();
    const ImVec2 image_max = ImGui::GetItemRectMax();
    draw->AddRectFilled(image_min, image_max, IM_COL32(40, 40, 42, 255), 0.0f);
    if (auto alert = r->findAlertAtTime(stream_->currentSec())) {
      drawAlertOverlay(draw, *alert, image_min.x + 8.0f, image_min.y + 8.0f, image_size.x - 16.0f);
    } else {
      const char *no_video = "No video (--no-vipc)";
      ImVec2 text_size = ImGui::CalcTextSize(no_video);
      draw->AddText(ImVec2((image_min.x + image_max.x - text_size.x) * 0.5f, (image_min.y + image_max.y - text_size.y) * 0.5f),
                    IM_COL32(187, 187, 187, 255), no_video);
    }
    drawPausedOverlay(image_min, image_max);
  } // end if (video_) / else replay
  ImGui::Spacing();
  if (!is_dummy) {
    const bool paused_ctrl = stream_ && stream_->isPaused();
    if (iconButton(paused_ctrl ? "##play" : "##pause", paused_ctrl ? BootstrapIcon::Play : BootstrapIcon::Pause)) {
      stream_->pause(!paused_ctrl);
    }
    ImGui::SameLine();
    if (iconButton("##rewind", BootstrapIcon::Rewind)) stream_->seekTo(stream_->currentSec() - 1.0);
    ImGui::SameLine();
    if (iconButton("##ffwd", BootstrapIcon::FastForward)) stream_->seekTo(stream_->currentSec() + 1.0);
    if (stream_ && !stream_->liveStreaming()) {
      ImGui::SameLine();
      if (iconButton("##route_info", BootstrapIcon::InfoCircle, "Route Info")) show_route_info_ = true;
    }

    if (Replay *r = replay()) {
      ImGui::SameLine();
      if (iconButton("##loop", BootstrapIcon::Repeat, r->loop() ? "Loop: On" : "Loop: Off")) r->setLoop(!r->loop());
    }

    if (stream_->liveStreaming()) {
      ImGui::SameLine();
      bool has_time_range = stream_->timeRange().has_value();
      ImGui::BeginDisabled(has_time_range);
      if (iconButton("##live", BootstrapIcon::SkipEndFill)) {
        stream_->setSpeed(1.0f);
        stream_->pause(false);
        stream_->seekTo(stream_->maxSeconds() + 1);
      }
      ImGui::EndDisabled();
    }

    ImGui::SameLine();
    if (ImGui::Checkbox("Abs", &absolute_time_)) {
      settings.absolute_time = absolute_time_;
    }

    ImGui::SameLine();
    int current_speed = 0;
    for (int i = 0; i < static_cast<int>(kPlaybackSpeeds.size()); ++i) {
      if (std::abs(stream_->getSpeed() - kPlaybackSpeeds[i]) < 1e-4f) current_speed = i;
    }
    ImGui::SetNextItemWidth(92.0f);
    char speed_preview[32];
    snprintf(speed_preview, sizeof(speed_preview), "%gx", kPlaybackSpeeds[current_speed]);
    if (ImGui::BeginCombo("##speed", speed_preview)) {
      for (int i = 0; i < static_cast<int>(kPlaybackSpeeds.size()); ++i) {
        const bool selected = i == current_speed;
        char label_buf[32];
        snprintf(label_buf, sizeof(label_buf), "%gx", kPlaybackSpeeds[i]);
        std::string label = label_buf;
        if (ImGui::Selectable(label.c_str(), selected)) stream_->setSpeed(kPlaybackSpeeds[i]);
        if (selected) ImGui::SetItemDefaultFocus();
      }
      ImGui::EndCombo();
    }
  } else {
    ImGui::TextDisabled("No stream active");
  }

  const double min_sec = stream_->minSeconds();
  const double max_sec = stream_->maxSeconds();
  const double current = stream_->currentSec();
  std::optional<std::pair<double, double>> chart_window;
  if (chart_range_ || (show_charts_ && !currentCharts().empty())) {
    chart_window = currentChartDisplayRange();
  }
  ImGui::TextDisabled("%s / %s", formatTime(current, false).c_str(), formatTime(max_sec, false).c_str());

  if (replay()) {
    const ImVec2 slider_pos = ImGui::GetCursorScreenPos();
    const ImVec2 slider_size(ImGui::GetContentRegionAvail().x, 16.0f);
    drawTimelineStrip(draw, slider_pos, slider_size, min_sec, max_sec, current, chart_window, chart_hover_sec_);

    // Parse thumbnails from loaded qlog segments
    parseThumbnails();

    ImGui::InvisibleButton("timeline_bg", slider_size);
    timeline_hover_sec_ = -1.0;
    if (ImGui::IsItemActive() || ImGui::IsItemHovered()) {
      double hovered_sec = timelineSecFromMouseX(min_sec, max_sec, slider_pos.x, slider_size.x, ImGui::GetIO().MousePos.x);
      if (ImGui::IsItemHovered()) {
        timeline_hover_sec_ = hovered_sec;
      }
      if (ImGui::IsItemActive()) {
        if (chart_range_ && chart_window && (hovered_sec < chart_window->first || hovered_sec > chart_window->second)) {
          if (ImGui::IsItemActivated()) pushChartRangeHistory();
          updateChartRange(hovered_sec, chart_window->second - chart_window->first, false);
        }
        stream_->seekTo(hovered_sec);
      }
    }
  }

  ImGui::EndChild();
}

void CabanaImguiApp::drawSignalsPanel(const ImVec2 &size) {
  ImGui::BeginChild("SignalsPanel", size, ImGuiChildFlags_Borders);
  const MessageListItem *selected = selectedItem();
  const cabana::Msg *msg = selectedDbcMessage();
  if (!selected) {
    // Welcome view matching Qt's CenterWidget (CABANA title + shortcuts)
    float avail_h = ImGui::GetContentRegionAvail().y;
    float avail_w = ImGui::GetContentRegionAvail().x;
    ImGui::Dummy(ImVec2(0, avail_h * 0.25f));
    {
      const char *title = "CABANA";
      ImGui::PushFont(nullptr);  // default font
      float tw = ImGui::CalcTextSize(title).x;
      ImGui::SetCursorPosX((avail_w - tw) * 0.5f);
      ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "%s", title);
      ImGui::PopFont();
    }
    ImGui::Spacing();
    {
      const char *hint = "<- Select a message to view details";
      float hw = ImGui::CalcTextSize(hint).x;
      ImGui::SetCursorPosX((avail_w - hw) * 0.5f);
      ImGui::TextDisabled("%s", hint);
    }
    ImGui::Spacing();
    auto shortcutRow = [avail_w](const char *label, const char *key) {
      float lw = ImGui::CalcTextSize(label).x + ImGui::CalcTextSize(key).x + 20;
      ImGui::SetCursorPosX((avail_w - lw) * 0.5f);
      ImGui::TextDisabled("%s", label);
      ImGui::SameLine();
      ImGui::TextUnformatted(key);
    };
    shortcutRow("Pause:", "Space");
    shortcutRow("Help:", "F1");
    shortcutRow("What's This?:", "Shift+F1");
    // Show startup error if present (matching Qt's error dialog on route load failure)
    if (!startup_error_.empty()) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
      float ew = ImGui::CalcTextSize(startup_error_.c_str()).x;
      if (ew < avail_w) ImGui::SetCursorPosX((avail_w - ew) * 0.5f);
      ImGui::TextWrapped("%s", startup_error_.c_str());
      ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    return;
  }

  pushQtTabBarStyle();
  if (ImGui::BeginTabBar("detail_tabs")) {
    if (ImGui::BeginTabItem("Msg")) {
      detail_tab_ = 0;
      if (!msg) {
        ImGui::TextDisabled("No DBC loaded for this message.");
        for (const auto &line : startupSummary()) ImGui::BulletText("%s", line.c_str());
        ImGui::EndTabItem();
      } else {
      ImGui::Text("Signals");
      ImGui::SameLine();
      ImGui::TextDisabled("%s", selected->name.c_str());
      ImGui::Separator();
      ImGui::TextDisabled("Message %s", selected->id.toString().c_str());
      ImGui::TextDisabled("Payload %s", selected->data_hex.c_str());
      // Size mismatch warning
      {
        const auto &last = stream_->lastMessage(selected->id);
        if (msg->size > 0 && !last.dat.empty() && msg->size != last.dat.size()) {
          ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.0f, 1.0f), "Warning: DBC size %d != actual payload size %zu",
                             static_cast<int>(msg->size), last.dat.size());
        }
      }
      const bool can_edit_signals = dbc()->findDBCFile(selected->id) != nullptr;
      ImGui::Spacing();
      if (!can_edit_signals) {
        ImGui::TextDisabled("Open a DBC to add or edit signals.");
      }
      // Toolbar: Add signal + collapse all + sparkline range + filter
      if (ImGui::Button("Add Signal")) openSignalEditor(false);
      ImGui::SameLine();
      if (ImGui::Button("Collapse All")) { signals_collapse_all_ = true; signals_auto_open_ = false; }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Collapse all signal nodes");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.0f);
      ImGui::SliderInt("##sparkline_range", &settings.sparkline_range, 1, 30, "%ds");
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Sparkline time range");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      ImGui::InputTextWithHint("##signal_filter", "Filter signals...", signal_filter_.data(), signal_filter_.size());
      const std::string sig_filter_str = signal_filter_.data();

      // Expandable signal tree (matches Qt's tree view pattern)
      bool signal_hovered = false;
      ImGui::BeginChild("SignalTree", ImVec2(0, 0), ImGuiChildFlags_None);
      int sig_idx = 0;
      for (const auto *sig : msg->getSignals()) {
        // Signal filter
        if (!sig_filter_str.empty()) {
          std::string lower_name = sig->name;
          std::string lower_filter = sig_filter_str;
          std::transform(lower_name.begin(), lower_name.end(), lower_name.begin(), ::tolower);
          std::transform(lower_filter.begin(), lower_filter.end(), lower_filter.begin(), ::tolower);
          if (lower_name.find(lower_filter) == std::string::npos) continue;
        }
        ImGui::PushID(sig->name.c_str());
        const bool is_selected = selected_signal_name_ == sig->name;
        const bool is_hovered_from_binary = !is_selected && hovered_signal_name_ == sig->name;
        const bool is_highlighted = is_selected || is_hovered_from_binary;
        bool plotted = has_selected_id_ && findChart(selected_id_, sig) != nullptr;

        // Collapse All: force all nodes closed this frame
        if (signals_collapse_all_) {
          ImGui::SetNextItemOpen(false, ImGuiCond_Always);
        } else if (sig->name == selected_signal_name_ && signals_auto_open_) {
          // Auto-open the selected signal's tree node (set on selection, cleared by Collapse All)
          ImGui::SetNextItemOpen(true, ImGuiCond_Always);
          signals_auto_open_ = false;  // One-shot: only auto-open once per selection
        }

        // Tree node flags: clicking anywhere on the row toggles open/closed (matches Qt's rowClicked toggle behavior)
        ImGuiTreeNodeFlags node_flags = ImGuiTreeNodeFlags_AllowOverlap | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (is_selected) node_flags |= ImGuiTreeNodeFlags_Selected;

        // Match Qt: selection gets a row background, hover only changes the signal's own accents.
        if (is_selected) {
          ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.28f, 0.40f, 0.85f));
          ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.18f, 0.28f, 0.40f, 0.85f));
          ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.18f, 0.28f, 0.40f, 0.85f));
        } else {
          ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0, 0, 0, 0));
          ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0, 0, 0, 0));
        }

        // Build header label: "SignalName ■ [M/m] [unit]  value"
        std::string header_label = sig->name;
        bool tree_open = ImGui::TreeNodeEx(("##sig_" + sig->name).c_str(), node_flags);
        if (sig->name == selected_signal_name_ && signal_scroll_pending_) {
          ImGui::SetScrollHereY(0.5f);
          signal_scroll_pending_ = false;
        }

        // Click selects signal (toggle open/close is handled by TreeNodeEx without OpenOnArrow)
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
          selected_signal_name_ = sig->name;
        }
        if (ImGui::IsItemHovered()) {
          hovered_signal_name_ = sig->name;
          signal_hovered = true;
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            openSignalEditor(true);
          }
        }
        // Right-click context menu
        if (ImGui::BeginPopupContextItem(("signal_ctx_" + sig->name).c_str())) {
          selected_signal_name_ = sig->name;
          if (ImGui::MenuItem("Edit Signal", nullptr, false, can_edit_signals)) openSignalEditor(true);
          if (ImGui::MenuItem(plotted ? "Hide Plot" : "Show Plot")) toggleSelectedSignalPlot(false);
          if (ImGui::MenuItem(sig->is_little_endian ? "Set Big Endian" : "Set Little Endian")) updateSignalEndian(!sig->is_little_endian);
          if (ImGui::MenuItem(sig->is_signed ? "Set Unsigned" : "Set Signed")) updateSignalSigned(!sig->is_signed);
          if (ImGui::MenuItem("Value Descriptions...")) {
            val_desc_editor_.signal_name = sig->name;
            val_desc_editor_.entries.clear();
            for (const auto &[val, desc] : sig->val_desc) {
              ValueDescEditorState::Entry e;
              e.value = val;
              std::snprintf(e.desc.data(), e.desc.size(), "%s", desc.c_str());
              val_desc_editor_.entries.push_back(e);
            }
            val_desc_editor_.open = true;
          }
          if (ImGui::MenuItem("Remove Signal")) removeSelectedSignal();
          ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);

        // Same-line items after the tree arrow: numbered color badge (matching Qt), mux indicator, name, value, sparkline, plot/remove buttons
        ImGui::SameLine();
        {
          // Draw numbered color badge matching Qt's rounded-rect with row number
          ImVec4 badge_col = imColor(sig->color);
          if (is_selected) {
            // Darken for highlight (Qt: .darker(125))
            badge_col.x *= 0.8f; badge_col.y *= 0.8f; badge_col.z *= 0.8f;
          }
          ImVec2 badge_pos = ImGui::GetCursorScreenPos();
          float badge_h = ImGui::GetTextLineHeight();
          char num_buf[16];
          std::snprintf(num_buf, sizeof(num_buf), "%d", sig_idx + 1);
          ImVec2 text_size = ImGui::CalcTextSize(num_buf);
          float badge_w = std::max(badge_h, text_size.x + 6.0f);
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(badge_pos, ImVec2(badge_pos.x + badge_w, badge_pos.y + badge_h),
                            ImGui::ColorConvertFloat4ToU32(badge_col), 3.0f);
          // Center text in badge
          ImVec2 text_pos(badge_pos.x + (badge_w - text_size.x) * 0.5f, badge_pos.y);
          dl->AddText(text_pos, is_highlighted ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255), num_buf);
          ImGui::Dummy(ImVec2(badge_w, badge_h));
        }
        ImGui::SameLine();
        ImGui::TextUnformatted(sig->name.c_str());
        if (sig->type == cabana::Signal::Type::Multiplexor) {
          ImGui::SameLine();
          ImVec2 mux_pos = ImGui::GetCursorScreenPos();
          const char *mux_text = " M ";
          ImVec2 mux_size = ImGui::CalcTextSize(mux_text);
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(mux_pos, ImVec2(mux_pos.x + mux_size.x, mux_pos.y + mux_size.y),
                            IM_COL32(128, 128, 128, 255), 3.0f);
          dl->AddText(mux_pos, IM_COL32(255, 255, 255, 255), mux_text);
          ImGui::Dummy(mux_size);
        } else if (sig->type == cabana::Signal::Type::Multiplexed) {
          ImGui::SameLine();
          char mux_buf[16];
          std::snprintf(mux_buf, sizeof(mux_buf), " m%d ", sig->multiplex_value);
          ImVec2 mux_pos = ImGui::GetCursorScreenPos();
          ImVec2 mux_size = ImGui::CalcTextSize(mux_buf);
          ImDrawList *dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(mux_pos, ImVec2(mux_pos.x + mux_size.x, mux_pos.y + mux_size.y),
                            IM_COL32(128, 128, 128, 255), 3.0f);
          dl->AddText(mux_pos, IM_COL32(255, 255, 255, 255), mux_buf);
          ImGui::Dummy(mux_size);
        }
        if (!sig->unit.empty()) {
          ImGui::SameLine();
          ImGui::TextDisabled("[%s]", sig->unit.c_str());
        }
        // Value + sparkline (with min/max for selected/hovered, freq for multiplexed — matches Qt SignalItemDelegate)
        ImGui::SameLine(ImGui::GetContentRegionAvail().x * 0.55f + ImGui::GetCursorPosX() * 0.0f);
        if (auto it = signal_values_.find(sig->name); it != signal_values_.end()) {
          ImGui::TextUnformatted(sig->formatValue(it->second).c_str());
          ImGui::SameLine();
          if (auto sp_it = sparklines_.find(sig->name); sp_it != sparklines_.end()) {
            const auto &sp = sp_it->second;
            ImGui::PushStyleColor(ImGuiCol_PlotLines, imColor(sig->color));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0, 0, 0, 0));
            ImGui::PlotLines(("##sp_" + sig->name).c_str(), sp.values.data(), static_cast<int>(sp.values.size()), 0, nullptr, sp.min, sp.max, ImVec2(60.0f, 14.0f));
            ImGui::PopStyleColor(2);
            // Fixed stats area keeps rows stable while matching Qt's hover behavior.
            ImGui::SameLine();
            const float stats_w = 68.0f;
            const float stats_h = ImGui::GetFrameHeight();
            const ImVec2 stats_pos = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(stats_w, stats_h));
            ImDrawList *stats_dl = ImGui::GetWindowDrawList();
            ImFont *stats_font = ImGui::GetFont();
            const float stats_font_size = std::max(11.0f, ImGui::GetFontSize() * 0.78f);
            const ImU32 stats_col = IM_COL32(170, 170, 170, 255);
            if (!sp.values.empty() && is_highlighted) {
              char max_buf[32], min_buf[32];
              std::snprintf(max_buf, sizeof(max_buf), "%.3g", sp.max);
              std::snprintf(min_buf, sizeof(min_buf), "%.3g", sp.min);
              const ImVec2 min_size = stats_font->CalcTextSizeA(stats_font_size, 10000.0f, 0.0f, min_buf);
              const float line_x = stats_pos.x + 1.0f;
              stats_dl->AddLine(ImVec2(line_x, stats_pos.y + 2.0f), ImVec2(line_x, stats_pos.y + stats_h - 2.0f), stats_col, 1.0f);
              stats_dl->AddText(stats_font, stats_font_size, ImVec2(stats_pos.x + 6.0f, stats_pos.y + 1.0f), stats_col, max_buf);
              stats_dl->AddText(stats_font, stats_font_size,
                                ImVec2(stats_pos.x + 6.0f, stats_pos.y + stats_h - min_size.y - 1.0f),
                                stats_col, min_buf);
            } else if (sig->type == cabana::Signal::Type::Multiplexed && sp.freq > 0) {
              char freq_buf[32];
              std::snprintf(freq_buf, sizeof(freq_buf), "%.2g hz", sp.freq);
              const ImVec2 freq_size = stats_font->CalcTextSizeA(stats_font_size, 10000.0f, 0.0f, freq_buf);
              stats_dl->AddText(stats_font, stats_font_size,
                                ImVec2(stats_pos.x + 5.0f, stats_pos.y + (stats_h - freq_size.y) * 0.5f),
                                stats_col, freq_buf);
            }
          }
        } else {
          ImGui::TextDisabled("-");
        }
        // Plot toggle and remove button at right edge
        float right_x = ImGui::GetWindowContentRegionMax().x;
        ImGui::SameLine(right_x - 52.0f);
        if (smallIconButton(("##plot_" + sig->name).c_str(), BootstrapIcon::GraphUp, plotted ? "Hide Plot (Shift+click to merge)" : "Show Plot (Shift+click to merge)")) {
          if (has_selected_id_) showChart(selected_id_, sig, !plotted, ImGui::GetIO().KeyShift);
        }
        ImGui::SameLine(right_x - 22.0f);
        ImGui::BeginDisabled(!can_edit_signals);
        if (smallIconButton(("##rm_" + sig->name).c_str(), BootstrapIcon::X)) {
          selected_signal_name_ = sig->name;
          removeSelectedSignal();
        }
        ImGui::EndDisabled();

        // Expanded: show signal properties inline (matches Qt tree expand)
        if (tree_open) {
          ImGui::Indent(20.0f);
          // Inline-editable signal properties (matching Qt's editable tree model)
          // Helper: label on left, editable widget on right. Commits via EditSignalCommand on deactivation.
          const float label_w = 120.0f;
          const float input_w = -1.0f;
          auto editableRow = [&](const char *label, auto drawWidget) {
            ImGui::TextDisabled("%s:", label);
            ImGui::SameLine(label_w);
            ImGui::SetNextItemWidth(input_w);
            drawWidget();
          };
          if (can_edit_signals) {
            // Inline-editable signal properties using IsItemDeactivatedAfterEdit() pattern.
            // Buffers are always initialized from the model before the widget call.
            // When a widget is active, ImGui uses its own internal editing state (keyed by
            // widget ID, unique per signal via PushID), so our buffer init is ignored.
            // On deactivation, ImGui writes the final edit back to the buffer for us to read.

            auto textField = [&](const char *label, const char *id, char *buf, size_t buf_size,
                                 const std::string &current, auto commitFn) {
              editableRow(label, [&]() {
                std::snprintf(buf, buf_size, "%s", current.c_str());
                ImGui::InputText(id, buf, buf_size);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                  commitFn(std::string(buf));
                }
              });
            };
            auto doubleField = [&](const char *label, const char *id, double *val, double current, auto commitFn) {
              editableRow(label, [&]() {
                *val = current;
                ImGui::InputDouble(id, val, 0, 0, "%.6g");
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                  commitFn(*val);
                }
              });
            };
            auto intField = [&](const char *label, const char *id, int *val, int current, auto commitFn) {
              editableRow(label, [&]() {
                *val = current;
                ImGui::InputInt(id, val, 0, 0);
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                  commitFn(*val);
                }
              });
            };

            // Per-iteration edit buffers — safe to share across signals because
            // each widget call initializes from model first, and ImGui tracks
            // active editing state internally per widget ID (unique via PushID).
            std::array<char, 256> name_buf = {};
            int size_val = 0;
            double factor_val = 0, offset_val = 0, min_val = 0, max_val = 0;
            std::array<char, 256> unit_buf = {}, recv_buf = {};
            std::array<char, 1024> comment_buf = {};
            int mux_val = 0;

            // Name (with autocomplete, duplicate-name check, and identifier normalization — matches Qt's QCompleter + NameValidator)
            editableRow("Name", [&]() {
              std::snprintf(name_buf.data(), name_buf.size(), "%s", sig->name.c_str());
              ImGui::InputText("##name", name_buf.data(), name_buf.size());
              bool is_editing = ImGui::IsItemActive();
              bool committed = ImGui::IsItemDeactivatedAfterEdit();
              // Autocomplete popup (case-insensitive contains matching, like Qt's QCompleter with MatchContains)
              if (is_editing && name_buf[0] != '\0') {
                std::string input_lower = name_buf.data();
                for (auto &c : input_lower) c = std::tolower(static_cast<unsigned char>(c));
                auto all_names = dbc()->signalNames();
                std::vector<std::string> matches;
                for (const auto &n : all_names) {
                  std::string lower_n = n;
                  for (auto &c : lower_n) c = std::tolower(static_cast<unsigned char>(c));
                  if (lower_n.find(input_lower) != std::string::npos && n != sig->name) {
                    matches.push_back(n);
                    if (matches.size() >= 8) break;
                  }
                }
                if (!matches.empty()) {
                  ImGui::SetNextWindowPos(ImVec2(ImGui::GetItemRectMin().x, ImGui::GetItemRectMax().y));
                  ImGui::SetNextWindowSize(ImVec2(ImGui::GetItemRectSize().x, 0));
                  if (ImGui::Begin("##name_autocomplete", nullptr,
                                   ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                   ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing |
                                   ImGuiWindowFlags_Tooltip)) {
                    for (const auto &m : matches) {
                      if (ImGui::Selectable(m.c_str())) {
                        std::snprintf(name_buf.data(), name_buf.size(), "%s", m.c_str());
                        committed = true;
                      }
                    }
                  }
                  ImGui::End();
                }
              }
              if (committed) {
                std::string v(name_buf.data());
                std::string normalized = v;
                for (auto &c : normalized) if (c == ' ') c = '_';
                if (normalized != sig->name) {
                  if (auto *msg_obj2 = dbc()->msg(selected_id_); msg_obj2) {
                    auto *existing = msg_obj2->sig(normalized);
                    if (existing && existing != sig) {
                      setStatusMessage("Signal name '" + normalized + "' already exists", 3000);
                      return;
                    }
                  }
                  cabana::Signal s = *sig; s.name = normalized;
                  UndoStack::push(new EditSignalCommand(selected_id_, sig, s));
                }
              }
            });
            // Size
            intField("Size", "##size", &size_val, sig->size,
              [&](int v) {
                v = std::clamp(v, 1, CAN_MAX_DATA_BYTES);
                if (v != sig->size) { cabana::Signal s = *sig; s.size = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); }
              });
            // Endian (checkbox)
            {
              bool le = sig->is_little_endian;
              editableRow("Little Endian", [&]() {
                if (ImGui::Checkbox("##endian", &le)) {
                  selected_signal_name_ = sig->name;
                  updateSignalEndian(le);
                }
              });
            }
            // Signed (checkbox)
            {
              bool is_signed = sig->is_signed;
              editableRow("Signed", [&]() {
                if (ImGui::Checkbox("##signed", &is_signed)) {
                  selected_signal_name_ = sig->name;
                  updateSignalSigned(is_signed);
                }
              });
            }
            // Factor
            doubleField("Factor", "##factor", &factor_val, sig->factor,
              [&](double v) { if (v != sig->factor) { cabana::Signal s = *sig; s.factor = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); } });
            // Offset
            doubleField("Offset", "##offset", &offset_val, sig->offset,
              [&](double v) { if (v != sig->offset) { cabana::Signal s = *sig; s.offset = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); } });
            // Min
            doubleField("Min", "##min", &min_val, sig->min,
              [&](double v) { if (v != sig->min) { cabana::Signal s = *sig; s.min = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); } });
            // Max
            doubleField("Max", "##max", &max_val, sig->max,
              [&](double v) { if (v != sig->max) { cabana::Signal s = *sig; s.max = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); } });
            // Unit
            textField("Unit", "##unit", unit_buf.data(), unit_buf.size(), sig->unit,
              [&](const std::string &v) {
                if (v != sig->unit) { cabana::Signal s = *sig; s.unit = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); }
              });
            // Receiver (validated: word chars separated by commas, matching Qt's QRegExpValidator("^\w+(,\w+)*$"))
            textField("Receiver", "##receiver", recv_buf.data(), recv_buf.size(), sig->receiver_name,
              [&](const std::string &v) {
                if (v == sig->receiver_name) return;
                // Validate: must be empty or match ^\w+(,\w+)*$
                if (!v.empty()) {
                  bool valid = true;
                  bool need_word = true;  // Expecting a word character
                  for (char c : v) {
                    if (need_word) {
                      if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) { valid = false; break; }
                      need_word = false;
                    } else {
                      if (c == ',') { need_word = true; }
                      else if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) { valid = false; break; }
                    }
                  }
                  if (need_word) valid = false;  // Trailing comma
                  if (!valid) {
                    setStatusMessage("Invalid receiver: use word characters separated by commas", 3000);
                    return;
                  }
                }
                cabana::Signal s = *sig; s.receiver_name = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s));
              });
            // Comment
            textField("Comment", "##comment", comment_buf.data(), comment_buf.size(), sig->comment,
              [&](const std::string &v) {
                if (v != sig->comment) { cabana::Signal s = *sig; s.comment = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); }
              });
            // Signal Type (combo — with multiplexor restriction matching Qt)
            {
              // Qt only shows Multiplexor if message has no multiplexor yet;
              // only shows Multiplexed if message already has a multiplexor (and this isn't it)
              bool has_multiplexor = false;
              if (const auto *msg_obj2 = dbc()->msg(selected_id_)) {
                has_multiplexor = msg_obj2->multiplexor != nullptr;
              }
              int type_idx = static_cast<int>(sig->type);
              editableRow("Type", [&]() {
                if (ImGui::BeginCombo("##sigtype", sig->type == cabana::Signal::Type::Normal ? "Normal" :
                    sig->type == cabana::Signal::Type::Multiplexor ? "Multiplexor" : "Multiplexed")) {
                  if (ImGui::Selectable("Normal", type_idx == 0)) type_idx = 0;
                  // Enum: Normal=0, Multiplexed=1, Multiplexor=2
                  // Only offer Multiplexor if no other multiplexor exists in this message
                  if (!has_multiplexor || sig->type == cabana::Signal::Type::Multiplexor) {
                    if (ImGui::Selectable("Multiplexor", type_idx == 2)) type_idx = 2;
                  }
                  // Only offer Multiplexed if message already has a multiplexor (and this signal isn't it)
                  if (has_multiplexor && sig->type != cabana::Signal::Type::Multiplexor) {
                    if (ImGui::Selectable("Multiplexed", type_idx == 1)) type_idx = 1;
                  }
                  ImGui::EndCombo();
                }
                if (type_idx != static_cast<int>(sig->type)) {
                  cabana::Signal s = *sig;
                  s.type = static_cast<cabana::Signal::Type>(type_idx);
                  UndoStack::push(new EditSignalCommand(selected_id_, sig, s));
                }
              });
            }
            if (sig->type == cabana::Signal::Type::Multiplexed) {
              intField("Mux Value", "##muxval", &mux_val, sig->multiplex_value,
                [&](int v) {
                  if (v != sig->multiplex_value) { cabana::Signal s = *sig; s.multiplex_value = v; UndoStack::push(new EditSignalCommand(selected_id_, sig, s)); }
                });
            }
            // Value descriptions button
            if (ImGui::SmallButton("Value Descriptions...")) {
              selected_signal_name_ = sig->name;
              val_desc_editor_.signal_name = sig->name;
              val_desc_editor_.entries.clear();
              for (const auto &[val, desc] : sig->val_desc) {
                ValueDescEditorState::Entry e;
                e.value = val;
                std::snprintf(e.desc.data(), e.desc.size(), "%s", desc.c_str());
                val_desc_editor_.entries.push_back(e);
              }
              val_desc_editor_.open = true;
            }
            if (!sig->val_desc.empty()) {
              ImGui::SameLine();
              ImGui::TextDisabled("(%zu entries)", sig->val_desc.size());
            }
          } else {
            // Read-only view when DBC is not editable
            auto detailRow = [label_w](const char *label, const char *value) {
              ImGui::TextDisabled("%s:", label);
              ImGui::SameLine(label_w);
              ImGui::TextUnformatted(value);
            };
            char buf[64];
            detailRow("Name", sig->name.c_str());
            std::snprintf(buf, sizeof(buf), "%d", sig->size);
            detailRow("Size", buf);
            detailRow("Endian", sig->is_little_endian ? "Little-endian" : "Big-endian");
            detailRow("Signed", sig->is_signed ? "Yes" : "No");
            std::snprintf(buf, sizeof(buf), "%.6g", sig->factor);
            detailRow("Factor", buf);
            std::snprintf(buf, sizeof(buf), "%.6g", sig->offset);
            detailRow("Offset", buf);
            std::snprintf(buf, sizeof(buf), "%.2f..%.2f", sig->min, sig->max);
            detailRow("Range", buf);
            if (!sig->unit.empty()) detailRow("Unit", sig->unit.c_str());
            if (!sig->receiver_name.empty()) detailRow("Receiver", sig->receiver_name.c_str());
            if (!sig->comment.empty()) detailRow("Comment", sig->comment.c_str());
            if (!sig->val_desc.empty()) {
              ImGui::TextDisabled("Values:");
              ImGui::SameLine(label_w);
              for (const auto &[val, desc] : sig->val_desc) {
                ImGui::Text("%.0f: %s", val, desc.c_str());
                ImGui::Dummy(ImVec2(0, 0)); ImGui::SameLine(label_w);
              }
              ImGui::NewLine();
            }
          }
          ImGui::Unindent(20.0f);
          ImGui::Spacing();
          ImGui::TreePop();
        }
        ImGui::PopID();
        ++sig_idx;
      }
      if (!signal_hovered && ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows)) {
        hovered_signal_name_.clear();
      }
      signals_collapse_all_ = false;
      ImGui::EndChild();

      // Message metadata footer
      ImGui::Separator();
      ImGui::TextDisabled("Node: %s  |  Signals: %zu  |  DBC: %s",
        msg->transmitter.empty() ? "-" : msg->transmitter.c_str(),
        msg->sigs.size(),
        dbc()->findDBCFile(selected->id) ? dbc()->findDBCFile(selected->id)->name().c_str() : "Loaded");
      ImGui::EndTabItem();
      } // end else (msg != null)
    }

    if (ImGui::BeginTabItem("Logs")) {
      detail_tab_ = 1;
      drawLogsPanel(ImGui::GetContentRegionAvail());
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  popQtTabBarStyle();

  if (signal_editor_.open) {
    ImGui::OpenPopup(signal_editor_.editing_existing ? "Edit Signal" : "Add Signal");
    signal_editor_.open = false;
  }

  const char *signal_popup = signal_editor_.editing_existing ? "Edit Signal" : "Add Signal";
  { bool sig_open = true;
  if (ImGui::BeginPopupModal(signal_popup, &sig_open, ImGuiWindowFlags_AlwaysAutoResize)) {
    dismissOnEscape();
    if (!signal_editor_.error.empty()) {
      ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", signal_editor_.error.c_str());
    }
    ImGui::InputText("Name", signal_editor_.name.data(), signal_editor_.name.size());
    ImGui::InputScalar("Start Bit", ImGuiDataType_S32, &signal_editor_.start_bit);
    ImGui::InputScalar("Size", ImGuiDataType_S32, &signal_editor_.size);
    signal_editor_.size = std::clamp(signal_editor_.size, 1, CAN_MAX_DATA_BYTES);

    int endian_idx = signal_editor_.is_little_endian ? 0 : 1;
    if (ImGui::Combo("Endian", &endian_idx, "Little-endian\0Big-endian\0")) {
      const bool little_endian = endian_idx == 0;
      if (little_endian != signal_editor_.is_little_endian) {
        signal_editor_.start_bit = flipBitPos(signal_editor_.start_bit);
        signal_editor_.is_little_endian = little_endian;
      }
    }
    ImGui::Checkbox("Signed", &signal_editor_.is_signed);
    ImGui::InputDouble("Factor", &signal_editor_.factor, 0.1, 1.0, "%.6g");
    ImGui::InputDouble("Offset", &signal_editor_.offset, 0.1, 1.0, "%.6g");
    ImGui::InputDouble("Min", &signal_editor_.min, 0.1, 1.0, "%.6g");
    ImGui::InputDouble("Max", &signal_editor_.max, 0.1, 1.0, "%.6g");
    ImGui::InputText("Unit", signal_editor_.unit.data(), signal_editor_.unit.size());
    ImGui::InputText("Receiver", signal_editor_.receiver.data(), signal_editor_.receiver.size());

    // Multiplexing type
    {
      const auto *cur_msg = selectedDbcMessage();
      // Signal::Type: Normal=0, Multiplexed=1, Multiplexor=2
      const char *type_items[] = {"Normal", "Multiplexed", "Multiplexor"};
      int type_count = 3;
      // Only allow one multiplexor per message
      if (cur_msg && cur_msg->multiplexor && signal_editor_.type != 2) {
        // Already has a multiplexor, don't offer it
        const char *limited_items[] = {"Normal", "Multiplexed"};
        ImGui::Combo("Type", &signal_editor_.type, limited_items, 2);
      } else {
        ImGui::Combo("Type", &signal_editor_.type, type_items, type_count);
      }
      if (signal_editor_.type == 1) {  // Multiplexed
        ImGui::InputScalar("Multiplex Value", ImGuiDataType_S32, &signal_editor_.multiplex_value);
      }
    }

    ImGui::InputTextMultiline("Comment", signal_editor_.comment.data(), signal_editor_.comment.size(), ImVec2(380.0f, 96.0f));

    cabana::Signal preview = {};
    preview.start_bit = signal_editor_.start_bit;
    preview.size = signal_editor_.size;
    preview.is_little_endian = signal_editor_.is_little_endian;
    preview.is_signed = signal_editor_.is_signed;
    updateMsbLsb(preview);
    ImGui::Spacing();
    ImGui::TextDisabled("Preview: lsb %d  msb %d", preview.lsb, preview.msb);

    if (ImGui::Button("Save")) {
      if (commitSignalEditor()) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }}
  drawValueDescEditor();
  signals_panel_hovered_ = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
  ImGui::EndChild();
}
