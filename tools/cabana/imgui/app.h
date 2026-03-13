#pragma once

#include <array>
#include <chrono>
#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "tools/cabana/core/launch_config.h"
#include "tools/cabana/core/message_list.h"
#include "tools/cabana/dbc/dbc.h"
#include "tools/cabana/imgui/stream.h"

struct GLFWwindow;
struct ImVec2;
struct ImDrawList;

class CabanaImguiApp {
public:
  CabanaImguiApp(CabanaLaunchConfig config, AbstractStream *stream);
  ~CabanaImguiApp();
  int run();
  void setStartupError(const std::string &error) { startup_error_ = error; }

private:
  struct VideoStreamState;
  struct ChartSeriesRef {
    MessageId msg_id;
    std::string signal_name;
    bool operator==(const ChartSeriesRef &o) const { return msg_id == o.msg_id && signal_name == o.signal_name; }
  };
  struct ChartState {
    int id = 0;
    int series_type = 0;
    std::vector<ChartSeriesRef> series;
    std::vector<bool> hidden;
  };
  struct ChartTabState {
    int id = 0;
    std::vector<ChartState> charts;
  };
  struct MessageEditorState {
    bool open = false;
    int size = 8;
    std::array<char, 1024> name = {};
    std::array<char, 1024> node = {};
    std::array<char, 4096> comment = {};
  };
  struct SignalEditorState {
    bool open = false;
    bool editing_existing = false;
    int start_bit = 0;
    int size = 1;
    bool is_little_endian = true;
    bool is_signed = false;
    double factor = 1.0;
    double offset = 0.0;
    double min = 0.0;
    double max = 1.0;
    int type = 0;  // matches Signal::Type: 0=Normal, 1=Multiplexed, 2=Multiplexor
    int multiplex_value = 0;
    std::string original_name;
    std::string error;
    std::array<char, 1024> name = {};
    std::array<char, 1024> unit = {};
    std::array<char, 1024> receiver = {};
    std::array<char, 4096> comment = {};
  };
  struct ValueDescEditorState {
    bool open = false;
    std::string signal_name;
    struct Entry { double value; std::array<char, 1024> desc; };
    std::vector<Entry> entries;
  };
  struct BinaryDragState {
    bool active = false;
    int press_bit = -1;
    int press_logical = -1;
    int anchor_bit = -1;
    int anchor_logical = -1;
    int current_bit = -1;
    int current_logical = -1;
    std::string covering_signal_name;
    std::string resize_signal_name;
  };

  void draw();
  void handleKeyboardShortcuts(GLFWwindow *window);
  void drawStatusBar(const ImVec2 &size);
  void drawUnsavedPrompt();
  bool confirmOrPromptUnsaved(std::function<void()> continuation);
  void drawSettingsDialog();
  void drawValueDescEditor();
  void drawFindSimilarBitsDialog();
  void drawFindSignalDialog();
  void drawSignalSelectorDialog();
  void drawStreamSelectorDialog();
  void drawRouteBrowser();
  void drawFileBrowser();
  void showImguiOpenDialog(const std::string &title, const std::string &dir, const std::string &filter_ext, std::function<void(const std::string &)> callback);
  void showImguiSaveDialog(const std::string &title, const std::string &dir, const std::string &default_name, const std::string &filter_ext, std::function<void(const std::string &)> callback);
  void showImguiDirDialog(const std::string &title, const std::string &dir, std::function<void(const std::string &)> callback);
  void refreshState();
  void drawMessagesPanel(const ImVec2 &size);
  void drawVideoPanel(const ImVec2 &size);
  void drawChartPanel(const ImVec2 &size);
  void drawSignalsPanel(const ImVec2 &size);
  void drawBinaryPanel(const ImVec2 &size);
  void drawLogsPanel(const ImVec2 &size);
  std::vector<std::string> startupSummary() const;
  std::vector<MessageListItem> buildMessageItems() const;
  void maybeCaptureScreenshot(int drawable_width, int drawable_height);
  void shutdown();
  void setStatusMessage(std::string message, int timeout_ms = 2000);
  void showErrorModal(const std::string &title, const std::string &message);
  void openStreamDialog();
  void replaceStream(AbstractStream *stream, const std::string &dbc_file = {});
  void openDbcFileDialog();
  bool loadDbcFile(const std::string &filename, const SourceSet &sources = SOURCE_ALL);
  void loadDbcFromClipboard(const SourceSet &sources = SOURCE_ALL);
  void exportToCsvDialog(std::optional<MessageId> msg_id = std::nullopt);
  void saveDbc(bool save_as = false);
  void copyDbcToClipboard();
  void ensureAutoDbcLoaded();
  void ensureSelectionState();
  void ensureDetailTabs();
  void ensureChartTabs();
  void ensureSignalState();
  void activateMessage(const MessageId &id);
  void pushChartRangeHistory();
  void updateChartRange(double center, double width, bool push_history = true);
  void resetChartRange();
  std::pair<double, double> currentChartDisplayRange();
  double timelineSecFromMouseX(double min_sec, double max_sec, float slider_x, float slider_w, float mouse_x) const;
  void drawTimelineStrip(ImDrawList *draw, const ImVec2 &slider_pos, const ImVec2 &slider_size,
                         double min_sec, double max_sec, double current_sec,
                         std::optional<std::pair<double, double>> highlight_range = std::nullopt,
                         double hover_sec = -1.0, bool show_missing_segments = true) const;
  std::string formatTime(double sec, bool include_milliseconds) const;
  const cabana::Msg *selectedDbcMessage() const;
  const MessageListItem *selectedItem() const;
  const cabana::Signal *selectedSignal() const;
  const cabana::Signal *hoveredSignal() const;
  void showChart(const MessageId &id, const cabana::Signal *sig, bool show, bool merge = false);
  ChartState *findChart(const MessageId &id, const cabana::Signal *sig);
  void removeAllCharts();
  void removeChartSeriesIf(const std::function<bool(const ChartSeriesRef &)> &pred);
  std::vector<ChartState> &currentCharts();
  void loadUiState();
  void saveUiState() const;
  void openMessageEditor();
  void commitMessageEditor();
  void openSignalEditor(bool edit_existing);
  bool commitSignalEditor();
  void beginBinaryDrag(int signal_bit, int logical_bit, const cabana::Signal *covering);
  void finishBinaryDrag();
  std::tuple<int, int, bool> currentBinarySelection() const;
  void toggleSelectedSignalPlot(bool merge = false);
  void removeSelectedSignal();
  void updateSignalEndian(bool little_endian);
  void updateSignalSigned(bool is_signed);
  std::vector<std::array<uint32_t, 8>> bitFlipCounts(size_t msg_size) const;
  DBCFile *singleOpenDbcFile() const;
  class ReplayStream *replayStream() const;
  class Replay *replay() const;

  CabanaLaunchConfig config_;
  GLFWwindow *glfw_window_ = nullptr;
  AbstractStream *stream_ = nullptr;
  std::unique_ptr<VideoStreamState> video_;
  std::vector<MessageListItem> message_items_;
  mutable MessageListItem selected_item_cache_;  // Cached item for selected message (survives filtering)
  std::string msg_panel_title_ = "0 Messages (0 DBC Messages, 0 Signals)";
  MessageListFilter filter_;
  MessageId selected_id_;
  bool has_selected_id_ = false;
  std::vector<MessageId> detail_tabs_;
  std::vector<ChartTabState> chart_tabs_;
  int next_chart_tab_id_ = 1;
  int next_chart_id_ = 1;
  int active_chart_tab_ = 0;
  std::unordered_map<std::string, double> signal_values_;
  struct SparklineData {
    std::vector<float> values;
    float min = 0.0f;
    float max = 0.0f;
    float freq = 0.0f;  // Signal update frequency in Hz (for mux signals, matches Qt)
  };
  std::unordered_map<std::string, SparklineData> sparklines_;
  struct ChartSeriesCache {
    struct Series {
      ChartSeriesRef ref;
      CabanaColor color;
      std::string unit;
      std::vector<double> xs;
      std::vector<double> ys;
      double y_min = 0.0;
      double y_max = 0.0;
      size_t decoded_event_count = 0;  // tracks how many raw events have been decoded for incremental append
      uint64_t last_decoded_mono_time = 0;  // mono_time of last decoded event (for middle-insert detection)
    };
    int chart_id = -1;
    std::vector<ChartSeriesRef> series_refs;
    double x_min = 0.0;
    double x_max = 0.0;
    size_t total_event_count = 0;
    int undo_index = -1;
    std::vector<Series> series;
  };
  std::unordered_map<int, ChartSeriesCache> chart_caches_;
  std::optional<std::pair<double, double>> chart_range_;
  std::pair<double, double> chart_follow_range_ = {0.0, 0.0};  // Persistent follow-mode display range (matches Qt's display_range)
  std::vector<std::optional<std::pair<double, double>>> chart_zoom_history_;
  std::vector<std::optional<std::pair<double, double>>> chart_zoom_redo_;
  std::string last_auto_dbc_fingerprint_;
  int frame_count_ = 0;
  bool exit_requested_ = false;
  int detail_tab_ = 0;
  bool logs_hex_mode_ = true;
  int logs_filter_signal_idx_ = 0;
  int logs_compare_idx_ = 0;
  bool absolute_time_ = false;
  bool suppress_defined_signals_ = false;
  bool heatmap_live_ = true;
  int suppressed_count_ = 0;
  int chart_columns_ = 1;
  bool show_route_info_ = false;
  bool show_help_ = false;
  bool whats_this_mode_ = false;  // Shift+F1 "What's This?" click-to-help mode
  std::string whats_this_panel_title_;
  std::string whats_this_panel_text_;
  bool show_messages_ = true;
  bool show_video_ = true;
  bool show_charts_ = true;
  bool charts_floating_ = false;

  // Error modal (matches Qt's QMessageBox for important failures)
  std::string error_modal_title_;
  std::string error_modal_message_;

  // Layout proportions (for Reset Layout)
  float layout_left_frac_ = 0.30f;
  float layout_center_frac_ = 0.32f;
  float layout_center_top_frac_ = 0.50f;
  float layout_right_top_frac_ = 0.50f;
  bool pending_tab_select_ = false;
  bool video_rendered_this_frame_ = false;
  bool binary_panel_hovered_ = false;
  bool signals_panel_hovered_ = false;
  bool signals_collapse_all_ = false;  // Collapse All button state
  bool signals_auto_open_ = false;     // Auto-open selected signal (one-shot, cleared after first render)
  bool signal_scroll_pending_ = false;  // Scroll to selected signal in signals panel

  // Panel screen rects for help overlay (x, y, w, h)
  struct PanelRect { float x = 0, y = 0, w = 0, h = 0; };
  struct ChartZoomDragState {
    bool active = false;
    int chart_id = -1;
    float plot_min_x = 0.0f;
    float plot_min_y = 0.0f;
    float plot_max_x = 0.0f;
    float plot_max_y = 0.0f;
    float start_x = 0.0f;
  };
  struct TimelineZoomDragState {
    bool active = false;
    float start_x = 0.0f;
    float min_x = 0.0f;
    float max_x = 0.0f;
    double range_min = 0.0;
    double range_max = 0.0;
  };
  PanelRect panel_messages_, panel_binary_, panel_signals_, panel_video_, panel_charts_;
  ChartZoomDragState chart_zoom_drag_;
  TimelineZoomDragState chart_timeline_zoom_drag_;
  bool chart_scrub_was_playing_ = false;
  double chart_hover_sec_ = -1.0;
  double timeline_hover_sec_ = -1.0;  // Timeline scrub hover for full-size thumbnail
  bool multiline_bytes_ = false;
  // Per-column filter buffers for message list
  std::array<char, 64> col_filter_bus_ = {};
  std::array<char, 64> col_filter_addr_ = {};
  std::array<char, 64> col_filter_node_ = {};
  std::array<char, 64> col_filter_freq_ = {};
  std::array<char, 64> col_filter_bytes_ = {};
  std::string hovered_signal_name_;
  std::string selected_signal_name_;
  mutable std::optional<std::vector<std::array<uint32_t, 8>>> range_bit_flips_;
  mutable std::optional<std::pair<double, double>> range_bit_flips_window_;
  mutable std::optional<MessageId> range_bit_flips_id_;
  MessageEditorState message_editor_;
  SignalEditorState signal_editor_;
  ValueDescEditorState val_desc_editor_;
  BinaryDragState binary_drag_;
  std::array<char, 128> text_filter_ = {};
  std::array<char, 128> signal_filter_ = {};
  std::array<char, 64> logs_filter_text_ = {};
  std::string status_message_;
  std::chrono::steady_clock::time_point status_message_until_ = std::chrono::steady_clock::time_point::min();
  std::chrono::steady_clock::time_point last_frame_time_ = std::chrono::steady_clock::now();

  // Download progress (updated from download thread via installDownloadProgressHandler)
  std::atomic<uint64_t> download_cur_{0};
  std::atomic<uint64_t> download_total_{0};
  std::atomic<bool> download_active_{false};

  // Unsaved changes confirmation
  bool show_unsaved_prompt_ = false;
  std::function<void()> unsaved_continuation_;

  // CSV export in-app format choice fallback
  bool show_csv_choice_ = false;
  std::optional<MessageId> csv_export_msg_id_;
  std::string csv_export_path_;

  // Native ImGui settings dialog state
  bool show_settings_ = false;
  int settings_fps_ = 10;
  int settings_cached_minutes_ = 30;
  int settings_chart_height_ = 200;
  int settings_drag_direction_ = 0;
  int settings_theme_ = 0;
  bool settings_log_livestream_ = true;
  bool settings_video_crop_to_fill_ = true;
  std::array<char, 256> settings_log_path_ = {};

  // Native ImGui Find Similar Bits dialog state
  bool show_find_similar_bits_ = false;
  bool fsb_needs_init_ = false;
  int fsb_src_bus_ = 0;
  int fsb_find_bus_ = 0;
  int fsb_msg_idx_ = 0;
  int fsb_byte_idx_ = 0;
  int fsb_bit_idx_ = 0;
  int fsb_equal_ = 0;
  std::array<char, 32> fsb_min_msgs_ = {};
  struct FSBResult { uint32_t address; uint32_t byte_idx; uint32_t bit_idx; uint32_t mismatches; uint32_t total; float perc; };
  std::vector<FSBResult> fsb_results_;

  // Native ImGui Find Signal dialog state
  bool show_find_signal_ = false;
  bool fsd_needs_init_ = false;
  std::array<char, 128> fsd_bus_ = {};
  std::array<char, 128> fsd_address_ = {};
  std::array<char, 32> fsd_first_time_ = {};
  std::array<char, 32> fsd_last_time_ = {};
  int fsd_min_size_ = 8;
  int fsd_max_size_ = 8;
  bool fsd_little_endian_ = true;
  bool fsd_is_signed_ = false;
  std::array<char, 32> fsd_factor_ = {};
  std::array<char, 32> fsd_offset_ = {};
  int fsd_compare_ = 0;
  std::array<char, 32> fsd_value1_ = {};
  std::array<char, 32> fsd_value2_ = {};
  struct FSDSignal { MessageId id; uint64_t mono_time; int start_bit; int size; double value; std::string values_text; };
  std::vector<FSDSignal> fsd_results_;
  std::vector<std::vector<FSDSignal>> fsd_history_;
  bool fsd_properties_locked_ = false;
  std::future<std::vector<FSDSignal>> fsd_future_;
  bool fsd_searching_ = false;

  // Native ImGui Signal Selector (Manage Signals) dialog state
  bool show_signal_selector_ = false;
  int signal_selector_chart_id_ = -1;
  int signal_selector_msg_idx_ = -1;
  int signal_selector_avail_idx_ = -1;  // Highlighted available signal
  int signal_selector_sel_idx_ = -1;    // Highlighted selected signal
  std::array<char, 128> signal_selector_filter_ = {};
  std::vector<ChartSeriesRef> signal_selector_selected_;

  // Native ImGui Stream Selector dialog state
  bool show_stream_selector_ = false;
  std::string startup_error_;
  int ss_tab_ = 0;  // 0=Replay, 1=Panda, 2=SocketCAN, 3=Device
  // Replay tab
  std::array<char, 512> ss_route_ = {};
  bool ss_road_cam_ = true;
  bool ss_driver_cam_ = false;
  bool ss_wide_cam_ = false;
  // Panda tab
  std::vector<std::string> ss_panda_serials_;
  int ss_panda_serial_idx_ = 0;
  bool ss_panda_has_fd_ = false;
  bool ss_panda_needs_probe_ = true;
  struct SSBusConfig { int speed_idx = 6; bool can_fd = false; int data_speed_idx = 8; };
  std::array<SSBusConfig, 3> ss_bus_config_ = {};
  // SocketCAN tab
  std::vector<std::string> ss_can_devices_;
  int ss_can_device_idx_ = 0;
  // Device tab
  int ss_device_mode_ = 1;  // 0=MSGQ, 1=ZMQ
  std::array<char, 64> ss_device_ip_ = {};
  // DBC file
  std::string ss_dbc_file_;
  std::string ss_error_;

  // Thumbnail state for timeline scrub preview
  struct ThumbnailEntry {
    unsigned int tex_id = 0;
    int width = 0;
    int height = 0;
  };
  std::map<uint64_t, ThumbnailEntry> thumbnails_;
  void parseThumbnails();
  void clearThumbnails();

  // Remote route browser state
  bool show_route_browser_ = false;
  bool rb_selection_changed_ = false;  // Auto-fetch routes when devices first load
  bool rb_fetching_devices_ = false;
  bool rb_fetching_routes_ = false;
  struct RemoteDevice { std::string dongle_id; std::string alias; };
  std::vector<RemoteDevice> rb_devices_;
  int rb_device_idx_ = 0;
  int rb_period_idx_ = 0;  // 0=week, 1=2weeks, 2=month, 3=6months, 4=preserved
  struct RemoteRoute { std::string fullname; std::string display; };
  std::vector<RemoteRoute> rb_routes_;
  int rb_route_idx_ = 0;
  std::string rb_error_;
  uint64_t rb_fetch_id_ = 0;        // Monotonic counter to invalidate stale route fetches
  uint64_t rb_pending_fetch_id_ = 0; // fetch_id of the in-flight request
  std::future<std::string> rb_future_;

  // ImGui file browser fallback state
  bool show_file_browser_ = false;
  std::string fb_current_dir_;
  std::string fb_selected_path_;
  std::string fb_title_;
  bool fb_directory_mode_ = false;
  bool fb_save_mode_ = false;
  std::string fb_filter_;
  std::array<char, 256> fb_filename_ = {};
  std::function<void(const std::string &)> fb_callback_;
  struct FBEntry { std::string name; bool is_dir; };
  std::vector<FBEntry> fb_entries_;
};
