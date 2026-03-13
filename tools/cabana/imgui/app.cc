#include "tools/cabana/imgui/app.h"
#include "tools/cabana/imgui/app_util.h"
#include "tools/cabana/imgui/app_video_state.h"
#include "tools/cabana/imgui/icons.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <arpa/inet.h>
#include <jpeglib.h>
#include "third_party/json11/json11.hpp"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "msgq/visionipc/visionipc_client.h"
#include "tools/cabana/imgui/bootstrap.h"
#include "tools/cabana/imgui/commands.h"
#include "tools/cabana/core/persistent_state.h"
#include "tools/cabana/core/workspace_state.h"
#include "tools/cabana/imgui/dbcmanager.h"
#include "tools/cabana/imgui/settings.h"
#include "tools/cabana/panda.h"
#include "tools/cabana/imgui/devicestream.h"
#include "tools/cabana/imgui/pandastream.h"
#ifndef __APPLE__
#include "tools/cabana/imgui/socketcanstream.h"
#endif
#include "tools/cabana/imgui/stream.h"
#include "tools/cabana/imgui/replaystream.h"
#include "tools/cabana/imgui/export.h"
#include "tools/cabana/imgui/util.h"
#include "common/util.h"
#include "common/version.h"
#include "tools/replay/py_downloader.h"
#include "tools/replay/replay.h"
#include "tools/replay/timeline.h"

namespace {

constexpr int kWindowWidth = 1600;
constexpr int kWindowHeight = 980;
constexpr int kStartupSortColumn = 0;  // Name column (matches Qt's default ascending)
constexpr char kWindowTitle[] = "cabana";


}  // namespace

CabanaImguiApp::CabanaImguiApp(CabanaLaunchConfig config, AbstractStream *stream) : config_(std::move(config)), stream_(stream) {
  // Register stream callbacks now that the object is fully constructed
  // (virtual dispatch doesn't work in constructors, so this is deferred from AbstractStream())
  if (stream_) stream_->registerCallbacks();
  filter_.show_inactive_messages = true;
  filter_.sort_column = kStartupSortColumn;
  filter_.descending = false;  // Qt defaults to ascending
  absolute_time_ = settings.absolute_time;
  suppress_defined_signals_ = settings.suppress_defined_signals;
  multiline_bytes_ = settings.multiple_lines_hex;
  chart_columns_ = std::clamp(settings.chart_column_count, 1, 4);
  installDownloadProgressHandler([this](uint64_t cur, uint64_t total, bool success) {
    download_cur_.store(cur, std::memory_order_relaxed);
    download_total_.store(total, std::memory_order_relaxed);
    download_active_.store(success && cur < total, std::memory_order_release);
  });
  dbc()->onDBCFileChanged([this]() {
    UndoStack::instance()->clear();
    // Clear chart caches to force re-decode with new DBC definitions
    chart_caches_.clear();
    // Qt connects DBCFileChanged to ChartsWidget::removeAll() — clear charts on DBC swap
    removeAllCharts();
  });
  // Match Qt: remove chart series when a signal is removed (callback fires before deletion)
  dbc()->onSignalRemoved([this](MessageId id, const cabana::Signal *sig) {
    removeChartSeriesIf([&](const ChartSeriesRef &ref) {
      return ref.msg_id == id && ref.signal_name == sig->name;
    });
  });
  // Match Qt: remove chart series when a message is removed
  dbc()->onMsgRemoved([this](MessageId id) {
    removeChartSeriesIf([id](const ChartSeriesRef &ref) { return ref.msg_id == id; });
  });
  // Match Qt: update chart series refs on signal rename, invalidate caches on any signal update
  dbc()->onSignalUpdated([this](MessageId id, const std::string &old_name, const cabana::Signal *sig) {
    if (old_name != sig->name) {
      // Signal was renamed — update chart series refs to keep charts alive
      for (auto &tab : chart_tabs_) {
        for (auto &chart : tab.charts) {
          for (auto &ref : chart.series) {
            if (ref.msg_id == id && ref.signal_name == old_name) {
              ref.signal_name = sig->name;
            }
          }
        }
      }
    }
    chart_caches_.clear();
  });
  // Ensure a blank DBC exists so edit operations don't crash (matches Qt startStream())
  if (dbc()->dbcCount() == 0) {
    dbc()->open(SOURCE_ALL, std::string(""), std::string(""));
  }
  loadUiState();
  if (replayStream() && (config_.replay_flags & REPLAY_FLAG_NO_VIPC) == 0) {
    video_ = std::make_unique<VideoStreamState>("camerad", VISION_STREAM_ROAD);
  }
  refreshState();
  // Match Qt: auto-open stream selector when no stream was provided on startup
  if (dynamic_cast<DummyStream *>(stream_)) {
    openStreamDialog();
  }
}

CabanaImguiApp::~CabanaImguiApp() {
  installDownloadProgressHandler(nullptr);
  saveUiState();
  // Drain any in-flight async work to prevent blocking on future destructor
  if (fsd_future_.valid()) fsd_future_.wait();
  if (rb_future_.valid()) rb_future_.wait();
}

void CabanaImguiApp::setStatusMessage(std::string message, int timeout_ms) {
  status_message_ = std::move(message);
  status_message_until_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
}

void CabanaImguiApp::showErrorModal(const std::string &title, const std::string &message) {
  error_modal_title_ = title;
  error_modal_message_ = message;
  // Don't call ImGui::OpenPopup() here — callers may not be in a valid frame context.
  // The draw loop opens the popup when error_modal_title_ is non-empty.
}

void CabanaImguiApp::openStreamDialog() {
  show_stream_selector_ = true;
  ss_tab_ = 0;
  ss_route_ = {};
  ss_road_cam_ = true;
  ss_driver_cam_ = false;
  ss_wide_cam_ = false;
  ss_panda_serials_ = Panda::list();
  ss_panda_serial_idx_ = 0;
  ss_panda_needs_probe_ = true;
  ss_bus_config_ = {};
  ss_can_devices_.clear();
  ss_can_device_idx_ = 0;
  ss_device_mode_ = 1;
  ss_device_ip_ = {};
  ss_dbc_file_.clear();
  ss_error_.clear();
#ifndef __APPLE__
  // Scan for SocketCAN devices
  {
    namespace fs = std::filesystem;
    const fs::path net_dir("/sys/class/net");
    if (fs::is_directory(net_dir)) {
      for (const auto &entry : fs::directory_iterator(net_dir)) {
        if (!entry.is_directory()) continue;
        std::ifstream type_file(entry.path() / "type");
        if (type_file) {
          int type = 0;
          type_file >> type;
          if (type == 280) ss_can_devices_.push_back(entry.path().filename().string());
        }
      }
    }
  }
#endif
}

void CabanaImguiApp::replaceStream(AbstractStream *stream, const std::string &dbc_file) {
  // Wait for any async Find Signal search before destroying the stream it references
  if (fsd_future_.valid()) fsd_future_.wait();
  fsd_searching_ = false;

  resetChartRange();
  video_.reset();
  clearThumbnails();

  // Clear callbacks from the old stream before deleting it to prevent accumulation
  dbc()->clearCallbacks();
  settings.on_changed_.clear();

  if (stream_ && stream_ != stream) {
    delete stream_;
  }

  stream_ = stream;
  can = stream_;
  if (stream_) {
    // Re-register stream-level callbacks (DBC masks, settings) that were wiped by clearCallbacks
    stream_->registerCallbacks();
    stream_->start();
  }

  // Re-register app-level callbacks after clearing (must match constructor registrations)
  dbc()->onDBCFileChanged([this]() {
    UndoStack::instance()->clear();
    chart_caches_.clear();
    removeAllCharts();
  });
  dbc()->onSignalRemoved([this](MessageId id, const cabana::Signal *sig) {
    removeChartSeriesIf([&](const ChartSeriesRef &ref) {
      return ref.msg_id == id && ref.signal_name == sig->name;
    });
  });
  dbc()->onMsgRemoved([this](MessageId id) {
    removeChartSeriesIf([id](const ChartSeriesRef &ref) { return ref.msg_id == id; });
  });
  dbc()->onSignalUpdated([this](MessageId id, const std::string &old_name, const cabana::Signal *sig) {
    if (old_name != sig->name) {
      for (auto &tab : chart_tabs_) {
        for (auto &chart : tab.charts) {
          for (auto &ref : chart.series) {
            if (ref.msg_id == id && ref.signal_name == old_name) {
              ref.signal_name = sig->name;
            }
          }
        }
      }
    }
    chart_caches_.clear();
  });

  config_.route = stream_ ? stream_->routeName() : std::string();
  if (!dbc_file.empty()) {
    config_.dbc_file = dbc_file;
  }

  last_auto_dbc_fingerprint_.clear();
  frame_count_ = 0;
  suppressed_count_ = 0;
  has_selected_id_ = false;
  selected_id_ = {};
  detail_tabs_.clear();
  chart_tabs_.clear();
  chart_caches_.clear();
  selected_signal_name_.clear();
  hovered_signal_name_.clear();
  signal_values_.clear();
  range_bit_flips_.reset();
  range_bit_flips_window_.reset();
  range_bit_flips_id_.reset();
  message_items_.clear();
  UndoStack::instance()->clear();

  // Only create video for replay streams (matches Qt: VideoWidget only for replay)
  if (replayStream()) {
    bool no_vipc = replayStream()->getReplay()->hasFlag(REPLAY_FLAG_NO_VIPC);
    if (!no_vipc) {
      video_ = std::make_unique<VideoStreamState>("camerad", VISION_STREAM_ROAD);
    }
  }

  if (!dbc_file.empty()) {
    loadDbcFile(dbc_file);
  }
  // Ensure a blank DBC exists so edit operations don't crash (matches Qt behavior)
  if (!dbc()->nonEmptyDBCCount() && dbc()->dbcCount() == 0) {
    dbc()->open(SOURCE_ALL, std::string(""), std::string(""));
  }

  refreshState();
  if (dynamic_cast<DummyStream *>(stream_)) {
    setStatusMessage("Stream closed");
  } else {
    const std::string route = stream_ && !stream_->routeName().empty() ? stream_->routeName() : "No stream";
    setStatusMessage("Stream opened: " + route);
  }
}

static void glfwErrorCallback(int error, const char *description) {
  fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

int CabanaImguiApp::run() {
  clearCabanaShutdownRequested();
  glfwSetErrorCallback(glfwErrorCallback);
  if (!glfwInit()) {
    fprintf(stderr, "glfwInit failed\n");
    return 1;
  }

  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#ifdef __APPLE__
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

  int win_w = (settings.window_width > 0) ? settings.window_width : kWindowWidth;
  int win_h = (settings.window_height > 0) ? settings.window_height : kWindowHeight;
  GLFWwindow *window = glfwCreateWindow(win_w, win_h, kWindowTitle, nullptr, nullptr);
  if (!window) {
    fprintf(stderr, "glfwCreateWindow failed\n");
    glfwTerminate();
    return 1;
  }
  glfw_window_ = window;

  // Restore window position
  if (settings.window_x >= 0 && settings.window_y >= 0) {
    glfwSetWindowPos(window, settings.window_x, settings.window_y);
  }
  // Restore maximized state (matches Qt's geometry/window_state restore)
  if (settings.window_maximized) {
    glfwMaximizeWindow(window);
  }

  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();

  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  static std::string ini_path = homeDir() + "/.cabana.ini";
  io.IniFilename = ini_path.c_str();

  loadCabanaFonts();
  applyCabanaTheme(settings.theme, cabanaUiScale());
  initBootstrapIcons((getExeDir() + "/../../third_party/bootstrap/bootstrap-icons.svg").c_str());

  ImGui_ImplGlfw_InitForOpenGL(window, true);
  if (!ImGui_ImplOpenGL3_Init("#version 330")) {
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 1;
  }

  // Set up mouse button callback chain for back-button chart zoom undo
  glfwSetWindowUserPointer(window, this);

  while (!exit_requested_ && !glfwWindowShouldClose(window)) {
    if (stream_) stream_->pollUpdates();
    if (cabanaShutdownRequested()) {
      exit_requested_ = true;
    }

    glfwPollEvents();

    // Handle global keyboard shortcuts via ImGui key state (GLFW backend feeds keys automatically)
    handleKeyboardShortcuts(window);

    // Mouse back button → chart zoom undo (matches Qt's BackButton mapping)
    if (ImGui::IsMouseClicked(3)) {  // button 3 = X1/Back
      if (!chart_zoom_history_.empty()) {
        chart_zoom_redo_.push_back(chart_range_);
        chart_range_ = chart_zoom_history_.back();
        chart_zoom_history_.pop_back();
        if (chart_range_) stream_->setTimeRange(chart_range_);
        else stream_->setTimeRange(std::nullopt);
      }
    }

    refreshState();

    int window_width = 0, window_height = 0;
    int drawable_width = 0, drawable_height = 0;
    glfwGetWindowSize(window, &window_width, &window_height);
    glfwGetFramebufferSize(window, &drawable_width, &drawable_height);

    // Update window title with all open DBC files and dirty state (matches Qt's DBCFileChanged)
    {
      std::string title = "Cabana";
      std::string dbc_names;
      for (auto *f : dbc()->allDBCFiles()) {
        if (!f->isEmpty()) {
          if (!dbc_names.empty()) dbc_names += " | ";
          dbc_names += "(" + toString(dbc()->sources(f)) + ") " + f->name();
        }
      }
      if (!dbc_names.empty()) title += " - " + dbc_names;
      if (!UndoStack::instance()->isClean()) title += " *";
      glfwSetWindowTitle(window, title.c_str());
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    draw();
    ImGui::Render();

    glViewport(0, 0, drawable_width, drawable_height);
    glClearColor(0.18f, 0.18f, 0.18f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    maybeCaptureScreenshot(drawable_width, drawable_height);
    glfwSwapBuffers(window);
  }

  // Save window geometry for restart persistence
  {
    int wx, wy, ww, wh;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);
    settings.window_maximized = glfwGetWindowAttrib(window, GLFW_MAXIMIZED) != 0;
    // Only save position/size if not maximized (restore to normal geometry when un-maximizing)
    if (!settings.window_maximized) {
      settings.window_x = wx;
      settings.window_y = wy;
      settings.window_width = ww;
      settings.window_height = wh;
    }
  }
  // Destroy GL resources before tearing down the GL context
  destroyBootstrapIcons();
  clearThumbnails();
  video_.reset();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImPlot::DestroyContext();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfw_window_ = nullptr;
  glfwTerminate();
  shutdown();
  return 0;
}

void CabanaImguiApp::clearThumbnails() {
  for (auto &[_, t] : thumbnails_) {
    if (t.tex_id) { GLuint id = t.tex_id; glDeleteTextures(1, &id); }
  }
  thumbnails_.clear();
}

void CabanaImguiApp::handleKeyboardShortcuts(GLFWwindow *window) {
  ImGuiIO &io = ImGui::GetIO();

  // Check each shortcut via ImGui's key state (GLFW backend feeds keys automatically)
  auto keyPressed = [&](ImGuiKey k) { return ImGui::IsKeyPressed(k, false); };

  // Global shortcuts: work even when text input is focused (matching Qt's QAction behavior)
  bool handled = false;
  if (io.KeyCtrl && !io.KeyShift && keyPressed(ImGuiKey_N)) {
    auto action = [this] { dbc()->closeAll(); dbc()->open(SOURCE_ALL, std::string(""), std::string("")); setStatusMessage("New DBC file created"); };
    if (confirmOrPromptUnsaved(action)) action();
    handled = true;
  } else if (io.KeyCtrl && !io.KeyShift && keyPressed(ImGuiKey_O)) {
    auto action = [this] { openDbcFileDialog(); };
    if (confirmOrPromptUnsaved(action)) action();
    handled = true;
  } else if (io.KeyCtrl && !io.KeyShift && keyPressed(ImGuiKey_S)) {
    saveDbc(false);
    handled = true;
  } else if (io.KeyCtrl && io.KeyShift && keyPressed(ImGuiKey_S)) {
    saveDbc(true);
    handled = true;
  } else if (io.KeyCtrl && keyPressed(ImGuiKey_Q)) {
    if (confirmOrPromptUnsaved([this] { exit_requested_ = true; })) exit_requested_ = true;
    handled = true;
  } else if (io.KeyShift && keyPressed(ImGuiKey_F1)) {
    whats_this_mode_ = !whats_this_mode_;
    show_help_ = false;
    handled = true;
  } else if (keyPressed(ImGuiKey_F1)) {
    show_help_ = !show_help_;
    whats_this_mode_ = false;
    handled = true;
  } else if (keyPressed(ImGuiKey_F11)) {
    GLFWmonitor *monitor = glfwGetWindowMonitor(window);
    if (monitor) {
      // Currently fullscreen → restore to windowed
      glfwSetWindowMonitor(window, nullptr,
                           settings.window_x >= 0 ? settings.window_x : 100,
                           settings.window_y >= 0 ? settings.window_y : 100,
                           settings.window_width > 0 ? settings.window_width : kWindowWidth,
                           settings.window_height > 0 ? settings.window_height : kWindowHeight, 0);
    } else {
      // Currently windowed → go fullscreen on primary monitor
      GLFWmonitor *primary = glfwGetPrimaryMonitor();
      const GLFWvidmode *mode = glfwGetVideoMode(primary);
      // Save current geometry before entering fullscreen
      glfwGetWindowPos(window, &settings.window_x, &settings.window_y);
      glfwGetWindowSize(window, &settings.window_width, &settings.window_height);
      glfwSetWindowMonitor(window, primary, 0, 0, mode->width, mode->height, mode->refreshRate);
    }
    handled = true;
  }

  // Context shortcuts: only when not in text input (to avoid eating typed characters)
  if (!handled && !io.WantTextInput) {
    if (io.KeyCtrl && !io.KeyShift && keyPressed(ImGuiKey_Z)) {
      UndoStack::instance()->undo();
    } else if (io.KeyCtrl && io.KeyShift && keyPressed(ImGuiKey_Z)) {
      UndoStack::instance()->redo();
    } else if (keyPressed(ImGuiKey_Space)) {
      if (stream_) stream_->pause(!stream_->isPaused());
    } else if (keyPressed(ImGuiKey_LeftArrow)) {
      if (stream_) stream_->seekTo(stream_->currentSec() - 1.0);
    } else if (keyPressed(ImGuiKey_RightArrow)) {
      if (stream_) stream_->seekTo(stream_->currentSec() + 1.0);
    } else if (binary_panel_hovered_ && keyPressed(ImGuiKey_A)) {
      openSignalEditor(false);
    } else if ((binary_panel_hovered_ || signals_panel_hovered_) && (keyPressed(ImGuiKey_Enter) || keyPressed(ImGuiKey_KeypadEnter))) {
      openSignalEditor(true);
    } else if (binary_panel_hovered_ && (keyPressed(ImGuiKey_C) || keyPressed(ImGuiKey_G) || keyPressed(ImGuiKey_P))) {
      const auto *target = hoveredSignal();
      if (!target) target = selectedSignal();
      if (target && has_selected_id_) showChart(selected_id_, target, true, false);
    } else if (binary_panel_hovered_ && (keyPressed(ImGuiKey_X) || keyPressed(ImGuiKey_Delete) || keyPressed(ImGuiKey_Backspace))) {
      const auto *target = hoveredSignal();
      if (target) {
        selected_signal_name_ = target->name;
        removeSelectedSignal();
      } else {
        removeSelectedSignal();
      }
    } else if (binary_panel_hovered_ && keyPressed(ImGuiKey_E)) {
      const auto *target = hoveredSignal();
      if (!target) target = selectedSignal();
      if (target) {
        std::string prev_sel = selected_signal_name_;
        selected_signal_name_ = target->name;
        updateSignalEndian(!target->is_little_endian);
        selected_signal_name_ = prev_sel;
      }
    } else if (binary_panel_hovered_ && keyPressed(ImGuiKey_S)) {
      const auto *target = hoveredSignal();
      if (!target) target = selectedSignal();
      if (target) {
        std::string prev_sel = selected_signal_name_;
        selected_signal_name_ = target->name;
        updateSignalSigned(!target->is_signed);
        selected_signal_name_ = prev_sel;
      }
    } else if (keyPressed(ImGuiKey_Equal) || keyPressed(ImGuiKey_KeypadAdd)) {
      if (stream_ && chart_range_) {
        double center = (chart_range_->first + chart_range_->second) * 0.5;
        double width = (chart_range_->second - chart_range_->first) * 0.8;
        updateChartRange(center, width);
      }
    } else if (keyPressed(ImGuiKey_Minus) || keyPressed(ImGuiKey_KeypadSubtract)) {
      if (stream_ && chart_range_) {
        double center = (chart_range_->first + chart_range_->second) * 0.5;
        double width = (chart_range_->second - chart_range_->first) * 1.25;
        updateChartRange(center, width);
      }
    } else if (keyPressed(ImGuiKey_Home)) {
      if (stream_) stream_->seekTo(stream_->minSeconds());
    } else if (keyPressed(ImGuiKey_End)) {
      if (stream_) stream_->seekTo(stream_->maxSeconds());
    }
  }
}

// Decode JPEG data from memory into RGBA pixels
static bool decodeJpeg(const uint8_t *data, size_t len, std::vector<uint8_t> &rgba, int &w, int &h) {
  struct jpeg_decompress_struct cinfo;
  struct jpeg_error_mgr jerr;
  cinfo.err = jpeg_std_error(&jerr);
  jpeg_create_decompress(&cinfo);
  jpeg_mem_src(&cinfo, data, len);
  if (jpeg_read_header(&cinfo, TRUE) != JPEG_HEADER_OK) { jpeg_destroy_decompress(&cinfo); return false; }
  cinfo.out_color_space = JCS_RGB;
  jpeg_start_decompress(&cinfo);
  w = cinfo.output_width;
  h = cinfo.output_height;
  rgba.resize(w * h * 4);
  std::vector<uint8_t> row(w * 3);
  while (cinfo.output_scanline < cinfo.output_height) {
    uint8_t *rp = row.data();
    jpeg_read_scanlines(&cinfo, &rp, 1);
    int y = cinfo.output_scanline - 1;
    for (int x = 0; x < w; ++x) {
      rgba[(y * w + x) * 4 + 0] = row[x * 3 + 0];
      rgba[(y * w + x) * 4 + 1] = row[x * 3 + 1];
      rgba[(y * w + x) * 4 + 2] = row[x * 3 + 2];
      rgba[(y * w + x) * 4 + 3] = 255;
    }
  }
  jpeg_finish_decompress(&cinfo);
  jpeg_destroy_decompress(&cinfo);
  return true;
}

void CabanaImguiApp::parseThumbnails() {
  // Thumbnails come from qlogs (loaded separately by Timeline without service filters).
  // The main segment log (rlog) filters out THUMBNAIL events via the allow list.
  auto *rs = dynamic_cast<ReplayStream *>(stream_);
  if (!rs) return;

  auto qlogs = rs->drainQlogs();
  for (const auto &qlog : qlogs) {
    for (const Event &e : qlog->events) {
      if (e.which != cereal::Event::Which::THUMBNAIL) continue;
      capnp::FlatArrayMessageReader reader(e.data);
      auto thumb = reader.getRoot<cereal::Event>().getThumbnail();
      uint64_t ts = thumb.getTimestampEof();
      if (thumbnails_.count(ts)) continue;  // Already parsed

      auto img_data = thumb.getThumbnail();
      std::vector<uint8_t> rgba;
      int w = 0, h = 0;
      if (decodeJpeg(reinterpret_cast<const uint8_t *>(img_data.begin()), img_data.size(), rgba, w, h)) {
        // Scale down to thumbnail height (~80px)
        const int target_h = 80;
        int tw = w * target_h / h;
        int th = target_h;

        GLuint tex;
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // Upload full image (GPU will scale for us)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        glBindTexture(GL_TEXTURE_2D, 0);

        ThumbnailEntry entry;
        entry.tex_id = tex;
        entry.width = tw;
        entry.height = th;
        thumbnails_[ts] = entry;
      }
    }
  }
}

void CabanaImguiApp::shutdown() {
  // Wait for any async Find Signal search before destroying the stream
  if (fsd_future_.valid()) fsd_future_.wait();
  fsd_searching_ = false;

  installDownloadProgressHandler(nullptr);
  saveUiState();
  settings.save();
  video_.reset();
  clearThumbnails();

  if (stream_) {
    if (can == stream_) can = nullptr;
    delete stream_;
    stream_ = nullptr;
  }
  clearCabanaShutdownRequested();
}

void CabanaImguiApp::draw() {
  video_rendered_this_frame_ = false;
  const ImGuiViewport *viewport = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(viewport->WorkPos);
  ImGui::SetNextWindowSize(viewport->WorkSize);
  ImGui::SetNextWindowViewport(viewport->ID);

  // In fullscreen, hide menu bar and status bar (matching Qt behavior)
  const bool is_fullscreen = glfw_window_ && glfwGetWindowMonitor(glfw_window_) != nullptr;

  ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
                                  ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                                  ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                                  ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
  if (!is_fullscreen) window_flags |= ImGuiWindowFlags_MenuBar;

  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::Begin("CabanaRoot", nullptr, window_flags);
  ImGui::PopStyleVar(3);

  if (ImGui::BeginMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open Stream...")) {
        if (confirmOrPromptUnsaved([this] { openStreamDialog(); })) openStreamDialog();
      }
      if (ImGui::MenuItem("Close Stream", nullptr, false, stream_ != nullptr && dynamic_cast<DummyStream *>(stream_) == nullptr)) {
        if (confirmOrPromptUnsaved([this] { replaceStream(new DummyStream()); })) replaceStream(new DummyStream());
      }
      ImGui::Separator();
      if (ImGui::MenuItem("New DBC File", "Ctrl+N")) {
        auto action = [this] { dbc()->closeAll(); dbc()->open(SOURCE_ALL, std::string(""), std::string("")); setStatusMessage("New DBC file created"); };
        if (confirmOrPromptUnsaved(action)) action();
      }
      if (ImGui::MenuItem("Open DBC File...", "Ctrl+O")) {
        auto action = [this] { openDbcFileDialog(); };
        if (confirmOrPromptUnsaved(action)) action();
      }
      if (ImGui::BeginMenu("Open Recent", !settings.recent_files.empty())) {
        for (const auto &file : settings.recent_files) {
          const std::string &path = file;
          const std::string label = std::filesystem::path(path).filename().string();
          if (ImGui::MenuItem(label.c_str())) {
            auto action = [this, path] { loadDbcFile(path); };
            if (confirmOrPromptUnsaved(action)) action();
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", path.c_str());
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Load DBC from commaai/opendbc")) {
        {
          namespace fs = std::filesystem;
          std::vector<std::string> dbc_names;
          if (fs::is_directory(OPENDBC_FILE_PATH)) {
            for (const auto &entry : fs::directory_iterator(OPENDBC_FILE_PATH)) {
              if (entry.is_regular_file() && entry.path().extension() == ".dbc") {
                dbc_names.push_back(entry.path().filename().string());
              }
            }
          }
          std::sort(dbc_names.begin(), dbc_names.end());
          for (const auto &name : dbc_names) {
            if (ImGui::MenuItem(name.c_str())) {
              std::string full_path = std::string(OPENDBC_FILE_PATH) + "/" + name;
              auto action = [this, full_path] { loadDbcFile(full_path); };
              if (confirmOrPromptUnsaved(action)) action();
            }
          }
        }
        ImGui::EndMenu();
      }
      if (ImGui::MenuItem("Load DBC From Clipboard")) {
        auto action = [this] { loadDbcFromClipboard(); };
        if (confirmOrPromptUnsaved(action)) action();
      }
      if (stream_ && !dynamic_cast<DummyStream *>(stream_) && ImGui::BeginMenu("Manage DBC Files")) {
        for (uint8_t src : stream_->sources) {
          if (src >= 64) continue;  // Sent/blocked buses handled implicitly with their parent
          SourceSet ss = {src, uint8_t(src + 128), uint8_t(src + 192)};
          auto *file = dbc()->findDBCFile(src);
          const std::string bus_label = "Bus " + std::to_string(src) +
            " (" + (file ? file->name().empty() ? "untitled" : file->name() : "No DBC loaded") + ")";
          if (ImGui::BeginMenu(bus_label.c_str())) {
            if (ImGui::MenuItem("New DBC...")) {
              auto action = [ss] { dbc()->close(ss); dbc()->open(ss, std::string(""), std::string("")); };
              if (confirmOrPromptUnsaved(action)) action();
            }
            if (ImGui::MenuItem("Open DBC...")) {
              auto action = [this, ss, src] {
                const std::string last_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
                auto do_open = [this, ss, src](const std::string &fn) {
                  dbc()->close(ss);
                  std::string error;
                  dbc()->open(ss, fn, &error);
                  if (!error.empty()) {
                    showErrorModal("DBC Load Failed", "Failed to load DBC file:\n" + error);
                  } else {
                    namespace fs = std::filesystem;
                    auto state = readPersistentState();
                    state.last_dir = fs::path(fn).parent_path().string();
                    rememberRecentFile(state, fn, 15);
                    writePersistentState(state);
                    setStatusMessage("DBC loaded for Bus " + std::to_string(src));
                    loadUiState();
                  }
                };
                if (hasNativeFileDialogs()) {
                  const std::string fn = nativeOpenFileDialog("Open DBC for Bus " + std::to_string(src), last_dir, "DBC", "*.dbc");
                  if (!fn.empty()) do_open(fn);
                } else {
                  showImguiOpenDialog("Open DBC for Bus " + std::to_string(src), last_dir, ".dbc", do_open);
                }
              };
              if (confirmOrPromptUnsaved(action)) action();
            }
            if (ImGui::MenuItem("Load From Clipboard")) {
              auto action = [this, ss] { loadDbcFromClipboard(ss); };
              if (confirmOrPromptUnsaved(action)) action();
            }
            if (file) {
              ImGui::Separator();
              std::string file_label = file->name().empty() ? "(untitled)" : file->name();
              file_label += " (" + toString(dbc()->sources(file)) + ")";
              ImGui::TextDisabled("%s", file_label.c_str());
              if (ImGui::MenuItem("Save")) {
                if (!file->filename.empty()) {
                  file->save();
                  UndoStack::instance()->setClean();
                  namespace fs = std::filesystem;
                  auto state = readPersistentState();
                  state.last_dir = fs::path(file->filename).parent_path().string();
                  rememberRecentFile(state, file->filename, 15);
                  writePersistentState(state);
                  setStatusMessage("DBC saved for Bus " + std::to_string(src));
                } else {
                  const std::string default_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
                  auto do_save = [this, file, src](const std::string &fn) {
                    file->saveAs(fn);
                    UndoStack::instance()->setClean();
                    namespace fs = std::filesystem;
                    auto state = readPersistentState();
                    state.last_dir = fs::path(fn).parent_path().string();
                    rememberRecentFile(state, fn, 15);
                    writePersistentState(state);
                    setStatusMessage("DBC saved for Bus " + std::to_string(src));
                  };
                  if (hasNativeFileDialogs()) {
                    const std::string fn = nativeSaveFileDialog("Save DBC (Bus " + std::to_string(src) + ")", default_dir + "/untitled.dbc", "DBC", "*.dbc");
                    if (!fn.empty()) do_save(fn);
                  } else {
                    showImguiSaveDialog("Save DBC (Bus " + std::to_string(src) + ")", default_dir, "untitled.dbc", ".dbc", do_save);
                  }
                }
              }
              if (ImGui::MenuItem("Save As...")) {
                const std::string default_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
                auto do_save_as = [this, file](const std::string &fn) {
                  file->saveAs(fn);
                  UndoStack::instance()->setClean();
                  namespace fs = std::filesystem;
                  auto state = readPersistentState();
                  state.last_dir = fs::path(fn).parent_path().string();
                  rememberRecentFile(state, fn, 15);
                  writePersistentState(state);
                  setStatusMessage("DBC saved as " + pathBasename(fn));
                };
                if (hasNativeFileDialogs()) {
                  const std::string fn = nativeSaveFileDialog("Save DBC As (Bus " + std::to_string(src) + ")", default_dir + "/untitled.dbc", "DBC", "*.dbc");
                  if (!fn.empty()) do_save_as(fn);
                } else {
                  showImguiSaveDialog("Save DBC As (Bus " + std::to_string(src) + ")", default_dir, "untitled.dbc", ".dbc", do_save_as);
                }
              }
              if (ImGui::MenuItem("Copy to Clipboard")) {
                ImGui::SetClipboardText(file->generateDBC().c_str());
                setStatusMessage("DBC copied to clipboard");
              }
              if (ImGui::MenuItem("Remove from this bus")) {
                auto action = [ss] {
                  dbc()->close(ss);
                  if (dbc()->dbcCount() == 0) dbc()->open(SOURCE_ALL, std::string(""), std::string(""));
                };
                if (confirmOrPromptUnsaved(action)) action();
              }
              if (ImGui::MenuItem("Remove from all buses")) {
                auto action = [file] {
                  dbc()->close(file);
                  if (dbc()->dbcCount() == 0) dbc()->open(SOURCE_ALL, std::string(""), std::string(""));
                };
                if (confirmOrPromptUnsaved(action)) action();
              }
            }
            ImGui::EndMenu();
          }
        }
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Save DBC...", "Ctrl+S", false, dbc()->nonEmptyDBCCount() > 0)) {
        saveDbc(false);
      }
      if (ImGui::MenuItem("Save DBC As...", "Ctrl+Shift+S", false, dbc()->nonEmptyDBCCount() == 1)) {
        saveDbc(true);
      }
      if (ImGui::MenuItem("Copy DBC To Clipboard", nullptr, false, dbc()->nonEmptyDBCCount() == 1)) {
        copyDbcToClipboard();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Export to CSV...", nullptr, false, stream_ != nullptr && !dynamic_cast<DummyStream *>(stream_))) {
        exportToCsvDialog();
      }
      if (ImGui::MenuItem("Route Info...", nullptr, false, stream_ != nullptr && !dynamic_cast<DummyStream *>(stream_))) {
        show_route_info_ = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Settings...")) {
        show_settings_ = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Exit", "Ctrl+Q")) {
        if (confirmOrPromptUnsaved([this] { exit_requested_ = true; })) exit_requested_ = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      std::string undo_label = "Undo";
      if (UndoStack::instance()->canUndo()) {
        undo_label += " " + UndoStack::instance()->undoText();
      }
      std::string redo_label = "Redo";
      if (UndoStack::instance()->canRedo()) {
        redo_label += " " + UndoStack::instance()->redoText();
      }
      if (ImGui::MenuItem(undo_label.c_str(), "Ctrl+Z", false, UndoStack::instance()->canUndo())) UndoStack::instance()->undo();
      if (ImGui::MenuItem(redo_label.c_str(), "Ctrl+Shift+Z", false, UndoStack::instance()->canRedo())) UndoStack::instance()->redo();
      ImGui::Separator();
      if (ImGui::BeginMenu("Command List", UndoStack::instance()->count() > 0)) {
        auto *stack = UndoStack::instance();
        for (int i = 0; i < stack->count(); ++i) {
          bool is_current = (i == stack->index() - 1);
          bool is_undone = (i >= stack->index());
          if (is_undone) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]);
          if (ImGui::MenuItem(stack->text(i).c_str(), nullptr, is_current)) {
            // Clicking an item undoes/redoes to that point
            while (stack->index() > i + 1 && stack->canUndo()) stack->undo();
            while (stack->index() <= i && stack->canRedo()) stack->redo();
          }
          if (is_undone) ImGui::PopStyleColor();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Messages", nullptr, &show_messages_);
      ImGui::MenuItem("Video", nullptr, &show_video_);
      ImGui::MenuItem("Crop Video to Fill", nullptr, &settings.video_crop_to_fill);
      ImGui::MenuItem("Charts", nullptr, &show_charts_);
      ImGui::MenuItem("Float Charts", nullptr, &charts_floating_);
      ImGui::Separator();
      if (ImGui::MenuItem("Reset Layout")) {
        layout_left_frac_ = 0.30f;
        layout_center_frac_ = 0.32f;
        layout_center_top_frac_ = 0.50f;
        layout_right_top_frac_ = 0.50f;
        show_messages_ = true;
        show_video_ = true;
        show_charts_ = true;
        charts_floating_ = false;
        setStatusMessage("Layout reset to defaults");
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Full Screen", "F11", is_fullscreen)) {
        if (glfw_window_) {
          if (is_fullscreen) {
            glfwSetWindowMonitor(glfw_window_, nullptr,
                                 settings.window_x >= 0 ? settings.window_x : 100,
                                 settings.window_y >= 0 ? settings.window_y : 100,
                                 settings.window_width > 0 ? settings.window_width : kWindowWidth,
                                 settings.window_height > 0 ? settings.window_height : kWindowHeight, 0);
          } else {
            GLFWmonitor *primary = glfwGetPrimaryMonitor();
            const GLFWvidmode *mode = glfwGetVideoMode(primary);
            glfwGetWindowPos(glfw_window_, &settings.window_x, &settings.window_y);
            glfwGetWindowSize(glfw_window_, &settings.window_width, &settings.window_height);
            glfwSetWindowMonitor(glfw_window_, primary, 0, 0, mode->width, mode->height, mode->refreshRate);
          }
        }
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Tools")) {
      if (ImGui::MenuItem("Find Similar Bits", nullptr, show_find_similar_bits_, stream_ != nullptr && !dynamic_cast<DummyStream *>(stream_))) {
        show_find_similar_bits_ = true;
        fsb_needs_init_ = true;
      }
      if (ImGui::MenuItem("Find Signal", nullptr, show_find_signal_, stream_ != nullptr && !dynamic_cast<DummyStream *>(stream_))) {
        show_find_signal_ = true;
        fsd_needs_init_ = true;
      }
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("Help", "F1")) {
        show_help_ = true;
      }
      if (ImGui::MenuItem("What's This?", "Shift+F1")) {
        whats_this_mode_ = true;
      }
      ImGui::EndMenu();
    }
    ImGui::TextDisabled("F1 Help");

    const bool is_dummy = !stream_ || dynamic_cast<DummyStream *>(stream_);
    const std::string route = stream_ && !stream_->routeName().empty() ? stream_->routeName() : "No Stream";
    const std::string current = is_dummy ? "" : formatTime(stream_->currentSec(), false);
    const std::string mode = is_dummy ? "" : (stream_->isPaused() ? "Paused" : "Playing");
    float rhs_width = ImGui::CalcTextSize(route.c_str()).x + ImGui::CalcTextSize(current.c_str()).x +
                      ImGui::CalcTextSize(mode.c_str()).x + 96.0f;
    float right_x = ImGui::GetWindowContentRegionMax().x - rhs_width;
    if (right_x > ImGui::GetCursorPosX()) {
      ImGui::SetCursorPosX(right_x);
    }
    ImGui::TextDisabled("%s", route.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("%s", current.c_str());
    ImGui::Separator();
    ImGui::TextDisabled("%s", mode.c_str());
    ImGui::EndMenuBar();
  }

  drawUnsavedPrompt();

  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float split_t = 4.0f;  // splitter thickness
  const float content_h = std::max(200.0f, avail.y);
  const int num_vsplits = show_messages_ ? 2 : 1;
  const float usable_w = avail.x - split_t * num_vsplits;
  float left_w, center_w, right_w;
  if (show_messages_) {
    left_w = std::floor(usable_w * layout_left_frac_);
    center_w = std::floor(usable_w * layout_center_frac_);
    right_w = std::max(100.0f, usable_w - left_w - center_w);
  } else {
    // When messages hidden, redistribute: center gets left's share
    left_w = 0.0f;
    const float total_frac = layout_center_frac_ + (1.0f - layout_left_frac_ - layout_center_frac_);
    center_w = std::floor(usable_w * layout_center_frac_ / total_frac);
    right_w = std::max(100.0f, usable_w - center_w);
  }
  const float center_top_h = std::floor((content_h - split_t) * layout_center_top_frac_);
  const float center_bottom_h = std::max(100.0f, content_h - center_top_h - split_t);
  const float right_top_h = std::floor((content_h - split_t) * layout_right_top_frac_);
  const float right_bottom_h = std::max(100.0f, content_h - right_top_h - split_t);

  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

  if (show_messages_) {
    { auto p = ImGui::GetCursorScreenPos(); panel_messages_ = {p.x, p.y, left_w, content_h}; }
    drawMessagesPanel(ImVec2(left_w, content_h));

    // Vertical splitter: left | center
    ImGui::SameLine(0.0f, 0.0f);
    if (drawSplitter("##vsplit1", true, ImVec2(split_t, content_h))) {
      layout_left_frac_ = std::clamp(layout_left_frac_ + ImGui::GetIO().MouseDelta.x / usable_w, 0.10f, 0.85f - layout_center_frac_);
    }
  }

  ImGui::SameLine(0.0f, 0.0f);
  ImGui::BeginGroup();
  if (!has_selected_id_) {
    // No message selected: show welcome screen centered in entire center pane (matching Qt)
    panel_binary_ = {};
    panel_signals_ = {};
    auto p = ImGui::GetCursorScreenPos();
    // Use the full center column height for the welcome/signals panel
    drawSignalsPanel(ImVec2(center_w, content_h));
    panel_signals_ = {p.x, p.y, center_w, content_h};
  } else {
    { auto p = ImGui::GetCursorScreenPos(); panel_binary_ = {p.x, p.y, center_w, center_top_h}; }
    drawBinaryPanel(ImVec2(center_w, center_top_h));
    // Horizontal splitter: binary / signals
    if (drawSplitter("##hsplit_center", false, ImVec2(center_w, split_t))) {
      layout_center_top_frac_ = std::clamp(layout_center_top_frac_ + ImGui::GetIO().MouseDelta.y / (content_h - split_t), 0.15f, 0.85f);
    }
    { auto p = ImGui::GetCursorScreenPos(); panel_signals_ = {p.x, p.y, center_w, center_bottom_h}; }
    drawSignalsPanel(ImVec2(center_w, center_bottom_h));
  }
  ImGui::EndGroup();

  // Vertical splitter: center | right
  ImGui::SameLine(0.0f, 0.0f);
  if (drawSplitter("##vsplit2", true, ImVec2(split_t, content_h))) {
    layout_center_frac_ = std::clamp(layout_center_frac_ + ImGui::GetIO().MouseDelta.x / usable_w, 0.10f, 0.85f - layout_left_frac_);
  }

  ImGui::SameLine(0.0f, 0.0f);
  ImGui::BeginGroup();
  const bool charts_inline = show_charts_ && !charts_floating_;
  if (show_video_ && charts_inline) {
    { auto p = ImGui::GetCursorScreenPos(); panel_video_ = {p.x, p.y, right_w, right_top_h}; }
    drawVideoPanel(ImVec2(right_w, right_top_h));
    // Horizontal splitter: video / charts
    if (drawSplitter("##hsplit_right", false, ImVec2(right_w, split_t))) {
      layout_right_top_frac_ = std::clamp(layout_right_top_frac_ + ImGui::GetIO().MouseDelta.y / (content_h - split_t), 0.15f, 0.85f);
    }
    { auto p = ImGui::GetCursorScreenPos(); panel_charts_ = {p.x, p.y, right_w, right_bottom_h}; }
    drawChartPanel(ImVec2(right_w, right_bottom_h));
  } else if (show_video_) {
    { auto p = ImGui::GetCursorScreenPos(); panel_video_ = {p.x, p.y, right_w, content_h}; }
    drawVideoPanel(ImVec2(right_w, content_h));
    panel_charts_ = {};
  } else if (charts_inline) {
    panel_video_ = {};
    { auto p = ImGui::GetCursorScreenPos(); panel_charts_ = {p.x, p.y, right_w, content_h}; }
    drawChartPanel(ImVec2(right_w, content_h));
  } else {
    panel_video_ = {};
    panel_charts_ = {};
    ImGui::BeginChild("EmptyRight", ImVec2(right_w, content_h), ImGuiChildFlags_Borders);
    ImGui::TextDisabled(charts_floating_ ? "Charts in floating window" : "Video and Charts hidden (View menu)");
    ImGui::EndChild();
  }
  ImGui::EndGroup();

  ImGui::PopStyleVar(2);  // WindowPadding, ItemSpacing

  // Floating charts window (separate ImGui window when charts are popped out)
  if (charts_floating_ && show_charts_) {
    ImGui::SetNextWindowSize(ImVec2(800, 500), ImGuiCond_FirstUseEver);
    bool charts_window_open = true;
    if (ImGui::Begin("Charts", &charts_window_open)) {
      drawChartPanel(ImGui::GetContentRegionAvail());
    }
    ImGui::End();
    if (!charts_window_open) charts_floating_ = false;  // closed window = dock back
  }

  if (show_route_info_) {
    ImGui::SetNextWindowSize(ImVec2(480, 400), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("Route Info", &show_route_info_)) {
      ImGui::Text("Route");
      ImGui::SameLine();
      ImGui::TextDisabled("%s", stream_ ? stream_->routeName().c_str() : "n/a");
      if (stream_) {
        ImGui::Text("Fingerprint");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", stream_->carFingerprint().c_str());
        ImGui::Text("Current");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", formatTime(stream_->currentSec(), false).c_str());
        ImGui::Text("Range");
        ImGui::SameLine();
        ImGui::TextDisabled("%s - %s", formatTime(stream_->minSeconds(), false).c_str(), formatTime(stream_->maxSeconds(), false).c_str());
      }
      if (Replay *r = replay()) {
        ImGui::Separator();
        ImGui::TextDisabled("%zu segments", r->route().segments().size());
        if (auto event_data = r->getEventData()) {
          int loaded = 0;
          for (const auto &[n, _] : r->route().segments()) {
            loaded += event_data->isSegmentLoaded(n) ? 1 : 0;
          }
          ImGui::TextDisabled("%d loaded", loaded);
        }
        ImGui::Spacing();
        if (ImGui::BeginTable("route_segments", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit,
                              ImVec2(0.0f, std::min(420.0f, ImGui::GetTextLineHeightWithSpacing() * (r->route().segments().size() + 2))))) {
          ImGui::TableSetupColumn("Seg");
          ImGui::TableSetupColumn("rlog");
          ImGui::TableSetupColumn("fcam");
          ImGui::TableSetupColumn("ecam");
          ImGui::TableSetupColumn("dcam");
          ImGui::TableSetupColumn("qlog");
          ImGui::TableSetupColumn("qcam");
          ImGui::TableHeadersRow();

          for (const auto &[seg_num, seg] : r->route().segments()) {
            ImGui::TableNextRow();
            ImGui::PushID(seg_num);
            ImGui::TableSetColumnIndex(0);
            if (ImGui::Selectable(std::to_string(seg_num).c_str(), false, ImGuiSelectableFlags_SpanAllColumns)) {
              stream_->seekTo(seg_num * 60.0);
              setStatusMessage("Sought to segment " + std::to_string(seg_num));
            }
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(seg.rlog.empty() ? "--" : "Yes");
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(seg.road_cam.empty() ? "--" : "Yes");
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(seg.wide_road_cam.empty() ? "--" : "Yes");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(seg.driver_cam.empty() ? "--" : "Yes");
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(seg.qlog.empty() ? "--" : "Yes");
            ImGui::TableSetColumnIndex(6);
            ImGui::TextUnformatted(seg.qcamera.empty() ? "--" : "Yes");
            ImGui::PopID();
          }
          ImGui::EndTable();
        }
      }
    }
    ImGui::End();
  }

  // Settings dialog
  drawSettingsDialog();
  { bool settings_open = true;
  if (ImGui::BeginPopupModal("Settings", &settings_open, ImGuiWindowFlags_AlwaysAutoResize)) {
    dismissOnEscape();
    ImGui::Text("FPS");
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("##fps", &settings_fps_, 10, 100);

    ImGui::Text("Max Cached Min");
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("##cached", &settings_cached_minutes_, 30, 120);

    ImGui::Text("Chart Height");
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(120.0f);
    ImGui::SliderInt("##charth", &settings_chart_height_, 100, 500);

    ImGui::Text("Drag Direction");
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(180.0f);
    const char *drag_items[] = {"MSB First", "LSB First", "Always LE", "Always BE"};
    ImGui::Combo("##drag_dir", &settings_drag_direction_, drag_items, IM_ARRAYSIZE(drag_items));

    ImGui::Text("Theme");
    ImGui::SameLine(140.0f);
    ImGui::SetNextItemWidth(120.0f);
    const char *theme_items[] = {"Auto", "Light", "Dark"};
    ImGui::Combo("##theme", &settings_theme_, theme_items, IM_ARRAYSIZE(theme_items));

    ImGui::Checkbox("Crop Video to Fill", &settings_video_crop_to_fill_);

    ImGui::Checkbox("Log Livestream", &settings_log_livestream_);
    if (settings_log_livestream_) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(200.0f);
      ImGui::InputText("##logpath", settings_log_path_.data(), settings_log_path_.size(), ImGuiInputTextFlags_ReadOnly);
      ImGui::SameLine();
      if (ImGui::Button("Browse##logpath")) {
        if (hasNativeFileDialogs()) {
          const std::string dir = nativeDirectoryDialog("Select Log Directory", settings_log_path_.data());
          if (!dir.empty()) std::strncpy(settings_log_path_.data(), dir.c_str(), settings_log_path_.size() - 1);
        } else {
          showImguiDirDialog("Select Log Directory", settings_log_path_.data(), [this](const std::string &dir) {
            std::strncpy(settings_log_path_.data(), dir.c_str(), settings_log_path_.size() - 1);
          });
        }
      }
    }

    ImGui::Separator();
    if (ImGui::Button("Save")) {
      settings.fps = settings_fps_;
      settings.max_cached_minutes = settings_cached_minutes_;
      settings.chart_height = settings_chart_height_;
      settings.drag_direction = static_cast<Settings::DragDirection>(settings_drag_direction_);
      if (settings.theme != settings_theme_) {
        settings.theme = settings_theme_;
        applyCabanaTheme(settings.theme, cabanaUiScale());
      }
      settings.log_livestream = settings_log_livestream_;
      settings.video_crop_to_fill = settings_video_crop_to_fill_;
      settings.log_path = settings_log_path_.data();
      settings.emitChanged();
      setStatusMessage("Settings updated");
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }}

  // Find Similar Bits dialog (modeless window, not modal popup)
  drawFindSimilarBitsDialog();
  if (show_find_similar_bits_) {
  ImGui::SetNextWindowSize(ImVec2(700, 500), ImGuiCond_FirstUseEver);
  { const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); }
  if (ImGui::Begin("Find Similar Bits", &show_find_similar_bits_)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_find_similar_bits_ = false;
    }
    const auto &sources = can->sources;
    std::vector<std::string> bus_labels;
    for (uint8_t s : sources) bus_labels.push_back(std::to_string(s));

    ImGui::Text("Source:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::BeginCombo("##srcbus", bus_labels.empty() ? "-" : bus_labels[std::min(fsb_src_bus_, (int)bus_labels.size() - 1)].c_str())) {
      for (int i = 0; i < (int)bus_labels.size(); ++i) {
        if (ImGui::Selectable(bus_labels[i].c_str(), fsb_src_bus_ == i)) fsb_src_bus_ = i;
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();

    // Message combo — Qt uses getMessages(-1) to show all DBC messages regardless of bus
    std::vector<std::pair<uint32_t, std::string>> msg_list;
    for (auto &[address, msg] : dbc()->getMessages(-1)) {
      msg_list.push_back({address, msg.name});
    }
    std::sort(msg_list.begin(), msg_list.end(), [](auto &a, auto &b) { return a.second < b.second; });
    fsb_msg_idx_ = std::clamp(fsb_msg_idx_, 0, std::max(0, (int)msg_list.size() - 1));
    const char *msg_preview = msg_list.empty() ? "No messages" : msg_list[fsb_msg_idx_].second.c_str();
    ImGui::SetNextItemWidth(200.0f);
    if (ImGui::BeginCombo("##msg", msg_preview)) {
      for (int i = 0; i < (int)msg_list.size(); ++i) {
        if (ImGui::Selectable(msg_list[i].second.c_str(), fsb_msg_idx_ == i)) fsb_msg_idx_ = i;
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Text("Byte:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    ImGui::InputInt("##byte", &fsb_byte_idx_, 0);
    fsb_byte_idx_ = std::clamp(fsb_byte_idx_, 0, 63);
    ImGui::SameLine();
    ImGui::Text("Bit:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(50.0f);
    ImGui::InputInt("##bit", &fsb_bit_idx_, 0);
    fsb_bit_idx_ = std::clamp(fsb_bit_idx_, 0, 7);

    ImGui::Text("Find in bus:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(60.0f);
    if (ImGui::BeginCombo("##findbus", bus_labels.empty() ? "-" : bus_labels[std::min(fsb_find_bus_, (int)bus_labels.size() - 1)].c_str())) {
      for (int i = 0; i < (int)bus_labels.size(); ++i) {
        if (ImGui::Selectable(bus_labels[i].c_str(), fsb_find_bus_ == i)) fsb_find_bus_ = i;
      }
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    const char *eq_items[] = {"Equal", "Not Equal"};
    ImGui::SetNextItemWidth(100.0f);
    ImGui::Combo("##equal", &fsb_equal_, eq_items, 2);
    ImGui::SameLine();
    ImGui::Text("Min msgs:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(70.0f);
    ImGui::InputText("##minmsgs", fsb_min_msgs_.data(), fsb_min_msgs_.size(), ImGuiInputTextFlags_CharsDecimal);
    ImGui::SameLine();
    if (ImGui::Button("Find")) {
      if (!msg_list.empty() && !sources.empty()) {
        uint8_t src_bus = *std::next(sources.begin(), std::min(fsb_src_bus_, (int)sources.size() - 1));
        uint8_t find_bus = *std::next(sources.begin(), std::min(fsb_find_bus_, (int)sources.size() - 1));
        uint32_t addr = msg_list[fsb_msg_idx_].first;
        int min_count = std::atoi(fsb_min_msgs_.data());
        bool equal = fsb_equal_ == 0;

        // Run the search (same algorithm as Qt version)
        std::unordered_map<uint32_t, std::vector<uint32_t>> mismatches;
        std::unordered_map<uint32_t, uint32_t> msg_count;
        const auto &events = can->allEvents();
        int bit_to_find = -1;
        for (const CanEvent *e : events) {
          if (e->src == src_bus && e->address == addr && e->size > fsb_byte_idx_) {
            bit_to_find = ((e->dat[fsb_byte_idx_] >> (7 - fsb_bit_idx_)) & 1) != 0;
          }
          if (e->src == find_bus) {
            ++msg_count[e->address];
            if (bit_to_find == -1) continue;
            auto &m = mismatches[e->address];
            if (m.size() < (size_t)e->size * 8) m.resize(e->size * 8);
            for (int i = 0; i < e->size; ++i) {
              for (int j = 0; j < 8; ++j) {
                int bit = ((e->dat[i] >> (7 - j)) & 1) != 0;
                m[i * 8 + j] += equal ? (bit != bit_to_find) : (bit == bit_to_find);
              }
            }
          }
        }
        fsb_results_.clear();
        for (auto &[a, mm] : mismatches) {
          if (auto cnt = msg_count[a]; cnt > (uint32_t)min_count) {
            for (int i = 0; i < (int)mm.size(); ++i) {
              float perc = (mm[i] / (double)cnt) * 100.0f;
              if (perc < 50.0f) {
                fsb_results_.push_back({a, (uint32_t)i / 8, (uint32_t)i % 8, mm[i], cnt, perc});
              }
            }
          }
        }
        std::sort(fsb_results_.begin(), fsb_results_.end(), [](auto &a, auto &b) { return a.perc < b.perc; });
        setStatusMessage("Found " + std::to_string(fsb_results_.size()) + " results");
      }
    }

    ImGui::Separator();
    if (ImGui::BeginTable("fsb_results", 6, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Sortable)) {
      ImGui::TableSetupColumn("Address", ImGuiTableColumnFlags_WidthFixed, 80.0f);
      ImGui::TableSetupColumn("Byte", ImGuiTableColumnFlags_WidthFixed, 50.0f);
      ImGui::TableSetupColumn("Bit", ImGuiTableColumnFlags_WidthFixed, 40.0f);
      ImGui::TableSetupColumn("Mismatches", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableSetupColumn("Total", ImGuiTableColumnFlags_WidthFixed, 70.0f);
      ImGui::TableSetupColumn("% Mismatch", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      // Apply interactive sort when user clicks column headers
      if (ImGuiTableSortSpecs *sort_specs = ImGui::TableGetSortSpecs()) {
        if (sort_specs->SpecsDirty && sort_specs->SpecsCount > 0) {
          const auto &spec = sort_specs->Specs[0];
          auto cmp = [&](const auto &a, const auto &b) -> bool {
            int result = 0;
            switch (spec.ColumnIndex) {
              case 0: result = (a.address < b.address) ? -1 : (a.address > b.address) ? 1 : 0; break;
              case 1: result = (a.byte_idx < b.byte_idx) ? -1 : (a.byte_idx > b.byte_idx) ? 1 : 0; break;
              case 2: result = (a.bit_idx < b.bit_idx) ? -1 : (a.bit_idx > b.bit_idx) ? 1 : 0; break;
              case 3: result = (a.mismatches < b.mismatches) ? -1 : (a.mismatches > b.mismatches) ? 1 : 0; break;
              case 4: result = (a.total < b.total) ? -1 : (a.total > b.total) ? 1 : 0; break;
              case 5: result = (a.perc < b.perc) ? -1 : (a.perc > b.perc) ? 1 : 0; break;
            }
            return spec.SortDirection == ImGuiSortDirection_Ascending ? result < 0 : result > 0;
          };
          std::sort(fsb_results_.begin(), fsb_results_.end(), cmp);
          sort_specs->SpecsDirty = false;
        }
      }
      ImGuiListClipper clipper;
      clipper.Begin((int)fsb_results_.size());
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          auto &r = fsb_results_[row];
          ImGui::TableNextRow();
          ImGui::PushID(row);
          ImGui::TableSetColumnIndex(0);
          char addr_buf[16];
          std::snprintf(addr_buf, sizeof(addr_buf), "0x%X", r.address);
          ImGui::Selectable(addr_buf, false, ImGuiSelectableFlags_SpanAllColumns);
          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            uint8_t find_bus = *std::next(sources.begin(), std::min(fsb_find_bus_, (int)sources.size() - 1));
            activateMessage({.source = find_bus, .address = r.address});
          }
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%u", r.byte_idx);
          ImGui::TableSetColumnIndex(2);
          ImGui::Text("%u", r.bit_idx);
          ImGui::TableSetColumnIndex(3);
          ImGui::Text("%u", r.mismatches);
          ImGui::TableSetColumnIndex(4);
          ImGui::Text("%u", r.total);
          ImGui::TableSetColumnIndex(5);
          ImGui::Text("%.2f%%", r.perc);
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
  }

  // Find Signal dialog (modeless window, not modal popup)
  drawFindSignalDialog();
  if (show_find_signal_) {
  ImGui::SetNextWindowSize(ImVec2(750, 650), ImGuiCond_FirstUseEver);
  { const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
  ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f)); }
  if (ImGui::Begin("Find Signal", &show_find_signal_)) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_find_signal_ = false;
    }
    if (!fsd_properties_locked_) {
      ImGui::Text("Bus:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(200.0f);
      ImGui::InputTextWithHint("##fsd_bus", "comma-sep (blank=all)", fsd_bus_.data(), fsd_bus_.size());
      ImGui::SameLine();
      ImGui::Text("Address:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(200.0f);
      ImGui::InputTextWithHint("##fsd_addr", "comma-sep hex (blank=all)", fsd_address_.data(), fsd_address_.size());

      ImGui::Text("Time:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.0f);
      ImGui::InputText("##fsd_t1", fsd_first_time_.data(), fsd_first_time_.size(), ImGuiInputTextFlags_CharsDecimal);
      ImGui::SameLine();
      ImGui::Text("-");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.0f);
      ImGui::InputText("##fsd_t2", fsd_last_time_.data(), fsd_last_time_.size(), ImGuiInputTextFlags_CharsDecimal);
      ImGui::SameLine();
      ImGui::Text("sec");

      ImGui::Text("Size:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(60.0f);
      ImGui::InputInt("##fsd_min", &fsd_min_size_, 0);
      fsd_min_size_ = std::clamp(fsd_min_size_, 1, 64);
      ImGui::SameLine();
      ImGui::Text("-");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(60.0f);
      ImGui::InputInt("##fsd_max", &fsd_max_size_, 0);
      fsd_max_size_ = std::clamp(fsd_max_size_, 1, 64);
      ImGui::SameLine();
      ImGui::Checkbox("Little-endian", &fsd_little_endian_);
      ImGui::SameLine();
      ImGui::Checkbox("Signed", &fsd_is_signed_);

      ImGui::Text("Factor:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.0f);
      ImGui::InputText("##fsd_fac", fsd_factor_.data(), fsd_factor_.size(), ImGuiInputTextFlags_CharsDecimal);
      ImGui::SameLine();
      ImGui::Text("Offset:");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(80.0f);
      ImGui::InputText("##fsd_off", fsd_offset_.data(), fsd_offset_.size(), ImGuiInputTextFlags_CharsDecimal);
    } else {
      ImGui::TextDisabled("Signal properties locked after first search. Reset to change.");
    }

    ImGui::Separator();
    const char *cmp_items[] = {"=", ">", ">=", "!=", "<", "<=", "between"};
    ImGui::Text("Value");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::Combo("##fsd_cmp", &fsd_compare_, cmp_items, IM_ARRAYSIZE(cmp_items));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(100.0f);
    ImGui::InputText("##fsd_v1", fsd_value1_.data(), fsd_value1_.size(), ImGuiInputTextFlags_CharsDecimal);
    if (fsd_compare_ == 6) {
      ImGui::SameLine();
      ImGui::Text("-");
      ImGui::SameLine();
      ImGui::SetNextItemWidth(100.0f);
      ImGui::InputText("##fsd_v2", fsd_value2_.data(), fsd_value2_.size(), ImGuiInputTextFlags_CharsDecimal);
    }
    // Poll async search result
    if (fsd_searching_ && fsd_future_.valid() &&
        fsd_future_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
      fsd_results_ = fsd_future_.get();
      fsd_history_.push_back(fsd_results_);
      fsd_searching_ = false;
      setStatusMessage("Found " + std::to_string(fsd_results_.size()) + " matches");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(fsd_searching_);
    if (ImGui::Button(fsd_searching_ ? "Searching..." : "Find")) {
      double v1 = std::strtod(fsd_value1_.data(), nullptr);
      double v2 = std::strtod(fsd_value2_.data(), nullptr);
      std::function<bool(double)> cmp;
      switch (fsd_compare_) {
        case 0: cmp = [v1](double v) { return v == v1; }; break;
        case 1: cmp = [v1](double v) { return v > v1; }; break;
        case 2: cmp = [v1](double v) { return v >= v1; }; break;
        case 3: cmp = [v1](double v) { return v != v1; }; break;
        case 4: cmp = [v1](double v) { return v < v1; }; break;
        case 5: cmp = [v1](double v) { return v <= v1; }; break;
        case 6: cmp = [v1, v2](double v) { return v >= v1 && v <= v2; }; break;
        default: cmp = [](double) { return true; }; break;
      }

      if (!fsd_properties_locked_) {
        // First search: build initial signals
        fsd_properties_locked_ = true;
        fsd_results_.clear();
        fsd_history_.clear();

        // Parse bus/address filters (with safe parsing to avoid crashes on bad input)
        std::set<uint16_t> buses;
        std::string bus_str(fsd_bus_.data());
        if (!bus_str.empty()) {
          std::istringstream ss(bus_str);
          std::string token;
          while (std::getline(ss, token, ',')) {
            try { if (!token.empty()) buses.insert(static_cast<uint16_t>(std::stoi(token))); } catch (...) {}
          }
        }
        std::set<uint32_t> addresses;
        std::string addr_str(fsd_address_.data());
        if (!addr_str.empty()) {
          std::istringstream ss(addr_str);
          std::string token;
          while (std::getline(ss, token, ',')) {
            try { if (!token.empty()) addresses.insert(std::stoul(token, nullptr, 16)); } catch (...) {}
          }
        }

        double first_sec = std::strtod(fsd_first_time_.data(), nullptr);
        std::string last_str(fsd_last_time_.data());
        double last_sec = (last_str == "MAX" || last_str.empty()) ? 1e12 : std::strtod(last_str.c_str(), nullptr);
        if (first_sec > last_sec) std::swap(first_sec, last_sec);
        uint64_t first_time = can->toMonoTime(first_sec);

        cabana::Signal base_sig{};
        base_sig.is_little_endian = fsd_little_endian_;
        base_sig.is_signed = fsd_is_signed_;
        base_sig.factor = std::strtod(fsd_factor_.data(), nullptr);
        base_sig.offset = std::strtod(fsd_offset_.data(), nullptr);

        uint64_t last_time = last_sec > 1e11 ? std::numeric_limits<uint64_t>::max() : can->toMonoTime(last_sec);
        int min_size = fsd_min_size_;
        int max_size = fsd_max_size_;

        // Snapshot message IDs, event vectors, and timing for background thread
        // Copy event vectors to avoid data races with mergeEvents
        struct MsgSnapshot {
          MessageId id;
          int dat_size_bits;
          std::vector<const CanEvent *> events;
        };
        std::vector<MsgSnapshot> msg_snapshots;
        for (const auto &[id, m] : can->lastMessages()) {
          if (!buses.empty() && buses.find(id.source) == buses.end()) continue;
          if (!addresses.empty() && addresses.find(id.address) == addresses.end()) continue;
          msg_snapshots.push_back({id, static_cast<int>(m.dat.size() * 8), stream_->events(id)});
        }

        uint64_t begin_mono = stream_->beginMonoTime();
        fsd_searching_ = true;
        fsd_future_ = std::async(std::launch::async, [msg_snapshots = std::move(msg_snapshots), base_sig, cmp, first_time, last_time, min_size, max_size, begin_mono]() {
          auto toSec = [begin_mono](uint64_t mt) { return std::max(0.0, (mt - begin_mono) / 1e9); };
          // Pass 1: Seed candidate signals at the first event at/after first_time
          std::vector<FSDSignal> seed_signals;
          for (const auto &snap : msg_snapshots) {
            auto first_it = std::lower_bound(snap.events.cbegin(), snap.events.cend(), first_time, CompareCanEvent());
            if (first_it == snap.events.cend()) continue;
            for (int sz = min_size; sz <= max_size; ++sz) {
              for (int start = 0; start <= snap.dat_size_bits - sz; ++start) {
                cabana::Signal sig = base_sig;
                sig.start_bit = start;
                sig.size = sz;
                updateMsbLsb(sig);
                double val = get_raw_value((*first_it)->dat, (*first_it)->size, sig);
                seed_signals.push_back({snap.id, first_time, start, sz, val, ""});
              }
            }
          }

          // Build lookup for snapshotted events by MessageId
          std::unordered_map<MessageId, const std::vector<const CanEvent *> *> events_lookup;
          for (const auto &snap : msg_snapshots) {
            events_lookup[snap.id] = &snap.events;
          }

          // Pass 2: Search strictly after the seed time for first match
          std::vector<FSDSignal> initial;
          for (const auto &s : seed_signals) {
            auto it_snap = events_lookup.find(s.id);
            if (it_snap == events_lookup.end()) continue;
            const auto &events = *it_snap->second;
            auto first = std::upper_bound(events.cbegin(), events.cend(), s.mono_time, CompareCanEvent());
            auto last = (last_time < std::numeric_limits<uint64_t>::max())
                          ? std::upper_bound(events.cbegin(), events.cend(), last_time, CompareCanEvent())
                          : events.cend();
            cabana::Signal sig = base_sig;
            sig.start_bit = s.start_bit;
            sig.size = s.size;
            updateMsbLsb(sig);
            for (auto it = first; it != last; ++it) {
              double val = get_raw_value((*it)->dat, (*it)->size, sig);
              if (cmp(val)) {
                char vbuf[64];
                std::snprintf(vbuf, sizeof(vbuf), "(%.3f, %.4g)", toSec((*it)->mono_time), val);
                initial.push_back({s.id, (*it)->mono_time, s.start_bit, s.size, val, std::string(vbuf)});
                break;
              }
            }
          }
          return initial;
        });
      } else {
        // Subsequent search: filter existing results
        double last_sec2 = 1e12;
        std::string last_str2(fsd_last_time_.data());
        if (last_str2 != "MAX" && !last_str2.empty()) last_sec2 = std::strtod(last_str2.c_str(), nullptr);
        uint64_t last_time2 = last_sec2 > 1e11 ? std::numeric_limits<uint64_t>::max() : can->toMonoTime(last_sec2);

        cabana::Signal base_sig{};
        base_sig.is_little_endian = fsd_little_endian_;
        base_sig.is_signed = fsd_is_signed_;
        base_sig.factor = std::strtod(fsd_factor_.data(), nullptr);
        base_sig.offset = std::strtod(fsd_offset_.data(), nullptr);

        auto prev_results = fsd_results_;
        // Snapshot event vectors for all referenced messages to avoid data races
        std::unordered_map<MessageId, std::vector<const CanEvent *>> events_snap;
        for (const auto &s : prev_results) {
          if (events_snap.find(s.id) == events_snap.end()) {
            events_snap[s.id] = stream_->events(s.id);
          }
        }
        uint64_t begin_mono2 = stream_->beginMonoTime();
        fsd_searching_ = true;
        fsd_future_ = std::async(std::launch::async, [events_snap = std::move(events_snap), prev_results, base_sig, cmp, last_time2, begin_mono2]() {
          auto toSec = [begin_mono2](uint64_t mt) { return std::max(0.0, (mt - begin_mono2) / 1e9); };
          std::vector<FSDSignal> next;
          for (const auto &s : prev_results) {
            auto it_snap = events_snap.find(s.id);
            if (it_snap == events_snap.end()) continue;
            const auto &events = it_snap->second;
            cabana::Signal sig = base_sig;
            sig.start_bit = s.start_bit;
            sig.size = s.size;
            updateMsbLsb(sig);
            auto first = std::upper_bound(events.cbegin(), events.cend(), s.mono_time, CompareCanEvent());
            auto last = (last_time2 < std::numeric_limits<uint64_t>::max())
                          ? std::upper_bound(first, events.cend(), last_time2, CompareCanEvent())
                          : events.cend();
            for (auto it = first; it != last; ++it) {
              double val = get_raw_value((*it)->dat, (*it)->size, sig);
              if (cmp(val)) {
                char vbuf[64];
                std::snprintf(vbuf, sizeof(vbuf), " (%.3f, %.4g)", toSec((*it)->mono_time), val);
                next.push_back({s.id, (*it)->mono_time, s.start_bit, s.size, val, s.values_text + vbuf});
                break;
              }
            }
          }
          return next;
        });
      }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(fsd_searching_);
    if (ImGui::Button("Undo") && fsd_history_.size() > 1) {
      fsd_history_.pop_back();
      fsd_results_ = fsd_history_.back();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
      if (fsd_future_.valid()) fsd_future_.wait();
      fsd_searching_ = false;
      fsd_results_.clear();
      fsd_history_.clear();
      fsd_properties_locked_ = false;
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (fsd_searching_) {
      ImGui::TextDisabled("Searching...");
    } else {
      ImGui::TextDisabled("%zu matches", fsd_results_.size());
    }

    if (ImGui::BeginTable("fsd_results", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY)) {
      ImGui::TableSetupColumn("Id", ImGuiTableColumnFlags_WidthFixed, 100.0f);
      ImGui::TableSetupColumn("Start, Size", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      ImGui::TableSetupColumn("(time, value)", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableHeadersRow();
      ImGuiListClipper clipper;
      clipper.Begin(static_cast<int>(fsd_results_.size()));
      while (clipper.Step()) {
        for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row) {
          auto &r = fsd_results_[row];
          ImGui::TableNextRow();
          ImGui::PushID(row);
          ImGui::TableSetColumnIndex(0);
          ImGui::Selectable(r.id.toString().c_str(), false, ImGuiSelectableFlags_SpanAllColumns);
          if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            activateMessage(r.id);
          }
          if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Create Signal")) {
              cabana::Signal sig{};
              sig.start_bit = r.start_bit;
              sig.size = r.size;
              sig.is_little_endian = fsd_little_endian_;
              sig.is_signed = fsd_is_signed_;
              sig.factor = std::strtod(fsd_factor_.data(), nullptr);
              sig.offset = std::strtod(fsd_offset_.data(), nullptr);
              updateMsbLsb(sig);
              UndoStack::push(new AddSigCommand(r.id, sig));
              activateMessage(r.id);
            }
            ImGui::EndPopup();
          }
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%d, %d", r.start_bit, r.size);
          ImGui::TableSetColumnIndex(2);
          ImGui::TextUnformatted(r.values_text.c_str());
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
  }
  ImGui::End();
  }

  // Signal Selector (Manage Signals) dialog
  drawSignalSelectorDialog();
  ImGui::SetNextWindowSize(ImVec2(600, 450), ImGuiCond_FirstUseEver);
  { bool ss_open = true;
  if (ImGui::BeginPopupModal("Manage Signals", &ss_open, ImGuiWindowFlags_None)) {
    dismissOnEscape();
    // Build list of messages that have DBC definitions
    std::vector<std::pair<MessageId, std::string>> msg_list;
    for (const auto &[id, _] : can->lastMessages()) {
      if (auto *m = dbc()->msg(id)) {
        msg_list.push_back({id, m->name + " (" + id.toString() + ")"});
      }
    }
    std::sort(msg_list.begin(), msg_list.end(), [](auto &a, auto &b) { return a.second < b.second; });

    // Left column: Available Signals
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Available Signals");
    ImGui::SetNextItemWidth(250.0f);
    const char *msg_preview = (signal_selector_msg_idx_ >= 0 && signal_selector_msg_idx_ < (int)msg_list.size())
                                  ? msg_list[signal_selector_msg_idx_].second.c_str() : "Select a message...";
    if (ImGui::BeginCombo("##ss_msg", msg_preview)) {
      ImGui::SetNextItemWidth(240.0f);
      ImGui::InputText("##ss_filter", signal_selector_filter_.data(), signal_selector_filter_.size());
      std::string filter_lower = signal_selector_filter_.data();
      for (auto &c : filter_lower) c = std::tolower(c);
      for (int i = 0; i < (int)msg_list.size(); ++i) {
        if (!filter_lower.empty()) {
          std::string label_lower = msg_list[i].second;
          for (auto &c : label_lower) c = std::tolower(c);
          if (label_lower.find(filter_lower) == std::string::npos) continue;
        }
        if (ImGui::Selectable(msg_list[i].second.c_str(), signal_selector_msg_idx_ == i)) {
          signal_selector_msg_idx_ = i;
          signal_selector_avail_idx_ = -1;  // Clear stale selection when message changes
        }
      }
      ImGui::EndCombo();
    }
    // Show available signals for selected message (click to select, double-click or >> to add)
    ImGui::BeginChild("##avail_sigs", ImVec2(250, 300), ImGuiChildFlags_Borders);
    std::vector<std::pair<MessageId, const cabana::Signal *>> avail_sigs;
    if (signal_selector_msg_idx_ >= 0 && signal_selector_msg_idx_ < (int)msg_list.size()) {
      const auto &sel_id = msg_list[signal_selector_msg_idx_].first;
      if (auto *m = dbc()->msg(sel_id)) {
        for (auto *sig : m->getSignals()) {
          bool already_selected = std::any_of(signal_selector_selected_.begin(), signal_selector_selected_.end(),
              [&](const ChartSeriesRef &r) { return r.msg_id == sel_id && r.signal_name == sig->name; });
          if (already_selected) continue;
          avail_sigs.push_back({sel_id, sig});
        }
      }
    }
    for (int ai = 0; ai < (int)avail_sigs.size(); ++ai) {
      auto &[id, sig] = avail_sigs[ai];
      ImGui::PushStyleColor(ImGuiCol_Text, imColor(sig->color));
      if (ImGui::Selectable(sig->name.c_str(), signal_selector_avail_idx_ == ai)) {
        signal_selector_avail_idx_ = ai;
      }
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        signal_selector_selected_.push_back({id, sig->name});
        signal_selector_avail_idx_ = -1;
      }
      ImGui::PopStyleColor();
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    // Middle: Add/Remove buttons
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(0, 110));
    ImGui::BeginDisabled(signal_selector_avail_idx_ < 0 || signal_selector_avail_idx_ >= (int)avail_sigs.size());
    if (ImGui::Button("  >>  ")) {
      auto &[id, sig] = avail_sigs[signal_selector_avail_idx_];
      signal_selector_selected_.push_back({id, sig->name});
      signal_selector_avail_idx_ = -1;
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(signal_selector_sel_idx_ < 0 || signal_selector_sel_idx_ >= (int)signal_selector_selected_.size());
    if (ImGui::Button("  <<  ")) {
      signal_selector_selected_.erase(signal_selector_selected_.begin() + signal_selector_sel_idx_);
      signal_selector_sel_idx_ = -1;
    }
    ImGui::EndDisabled();
    ImGui::EndGroup();
    ImGui::SameLine();

    // Right column: Selected Signals (click to select, double-click or << to remove)
    ImGui::BeginGroup();
    ImGui::TextUnformatted("Selected Signals");
    ImGui::BeginChild("##sel_sigs", ImVec2(250, 330), ImGuiChildFlags_Borders);
    int remove_idx = -1;
    for (int i = 0; i < (int)signal_selector_selected_.size(); ++i) {
      auto &ref = signal_selector_selected_[i];
      auto *m = dbc()->msg(ref.msg_id);
      std::string label = ref.signal_name;
      if (m) label += " (" + m->name + " " + ref.msg_id.toString() + ")";
      const cabana::Signal *sig = m ? m->sig(ref.signal_name) : nullptr;
      if (sig) ImGui::PushStyleColor(ImGuiCol_Text, imColor(sig->color));
      if (ImGui::Selectable(label.c_str(), signal_selector_sel_idx_ == i)) {
        signal_selector_sel_idx_ = i;
      }
      if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        remove_idx = i;
      }
      if (sig) ImGui::PopStyleColor();
    }
    if (remove_idx >= 0) {
      signal_selector_selected_.erase(signal_selector_selected_.begin() + remove_idx);
      signal_selector_sel_idx_ = -1;
    }
    ImGui::EndChild();
    ImGui::EndGroup();

    ImGui::Separator();
    ImGui::BeginDisabled(signal_selector_selected_.empty());
    if (ImGui::Button("OK", ImVec2(80, 0))) {
      if (signal_selector_chart_id_ < 0) {
        // Create new chart with selected signals
        ensureChartTabs();
        ChartState nc;
        nc.id = next_chart_id_++;
        nc.series_type = settings.chart_series_type;
        nc.series = signal_selector_selected_;
        currentCharts().push_back(std::move(nc));
      } else {
        // Edit existing chart
        for (auto &tab : chart_tabs_) {
          for (auto &chart : tab.charts) {
            if (chart.id == signal_selector_chart_id_) {
              chart.series = signal_selector_selected_;
              chart.hidden.clear();
              chart_caches_.erase(chart.id);
              break;
            }
          }
        }
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(80, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }}

  // Stream Selector dialog
  drawStreamSelectorDialog();
  { bool rb_was_open = show_route_browser_;
  drawRouteBrowser();
  if (rb_was_open && !show_route_browser_) show_stream_selector_ = true; }
  drawFileBrowser();

  // CSV export format choice popup (fallback when zenity/kdialog unavailable)
  if (show_csv_choice_) {
    ImGui::OpenPopup("Export Format");
    show_csv_choice_ = false;
  }
  { bool csv_open = true;
  if (ImGui::BeginPopupModal("Export Format", &csv_open, ImGuiWindowFlags_AlwaysAutoResize)) {
    dismissOnEscape();
    ImGui::TextUnformatted("Export signal values instead of hex bytes?");
    ImGui::Spacing();
    if (ImGui::Button("Yes")) {
      if (csv_export_msg_id_.has_value()) {
        utils::exportSignalsToCSV(csv_export_path_, *csv_export_msg_id_);
        settings.last_dir = pathDirname(csv_export_path_);
        setStatusMessage("CSV exported: " + pathBasename(csv_export_path_));
      }
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("No")) {
      utils::exportToCSV(csv_export_path_, csv_export_msg_id_);
      settings.last_dir = pathDirname(csv_export_path_);
      setStatusMessage("CSV exported: " + pathBasename(csv_export_path_));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }}

  ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_FirstUseEver);
  { bool open_stream_open = true;
  if (ImGui::BeginPopupModal("Open Stream", &open_stream_open, ImGuiWindowFlags_None)) {
    dismissOnEscape();
    // DBC file row
    ImGui::Text("DBC File:");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 80.0f);
    ImGui::TextDisabled("%s", ss_dbc_file_.empty() ? "No DBC file selected" : ss_dbc_file_.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Browse##dbc")) {
      const std::string last_dir = settings.last_dir.empty() ? homeDir() : settings.last_dir;
      if (hasNativeFileDialogs()) {
        const std::string fn = nativeOpenFileDialog("Select DBC File", last_dir, "DBC", "*.dbc");
        if (!fn.empty()) {
          ss_dbc_file_ = fn;
          settings.last_dir = pathDirname(fn);
        }
      } else {
        showImguiOpenDialog("Select DBC File", last_dir, ".dbc", [this](const std::string &fn) {
          ss_dbc_file_ = fn;
          settings.last_dir = pathDirname(fn);
        });
      }
    }
    ImGui::Separator();

    // Tab bar
    if (ImGui::BeginTabBar("StreamTabs")) {
      if (ImGui::BeginTabItem("Replay")) {
        ss_tab_ = 0;
        ImGui::Text("Route:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 170.0f);
        ImGui::InputTextWithHint("##ss_route", "Enter route name or browse for local route", ss_route_.data(), ss_route_.size());
        ImGui::SameLine();
        if (ImGui::Button("Local...")) {
          const std::string last_dir = settings.last_route_dir.empty() ? homeDir() : settings.last_route_dir;
          if (hasNativeFileDialogs()) {
            const std::string dir = nativeDirectoryDialog("Select Local Route", last_dir);
            if (!dir.empty()) {
              std::strncpy(ss_route_.data(), dir.c_str(), ss_route_.size() - 1);
              settings.last_route_dir = pathDirname(dir);
            }
          } else {
            showImguiDirDialog("Select Local Route", last_dir, [this](const std::string &dir) {
              std::strncpy(ss_route_.data(), dir.c_str(), ss_route_.size() - 1);
              settings.last_route_dir = pathDirname(dir);
            });
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remote...")) {
          show_route_browser_ = true;
          rb_error_.clear();
          rb_routes_.clear();
          rb_route_idx_ = 0;
          // Kick off device fetch if we don't have devices yet
          if (rb_devices_.empty() && !rb_fetching_devices_) {
            rb_fetching_devices_ = true;
            rb_future_ = std::async(std::launch::async, []() -> std::string {
              FILE *p = popen("python3 -m openpilot.tools.lib.file_downloader devices 2>/dev/null", "r");
              if (!p) return "{\"error\":\"failed to run python\"}";
              std::string out;
              char buf[4096];
              while (fgets(buf, sizeof(buf), p)) out += buf;
              pclose(p);
              return out;
            });
          }
          // Close the Open Stream modal so the route browser can receive input
          ImGui::CloseCurrentPopup();
        }
        ImGui::Spacing();
        ImGui::Checkbox("Road camera", &ss_road_cam_);
        ImGui::SameLine();
        ImGui::Checkbox("Driver camera", &ss_driver_cam_);
        ImGui::SameLine();
        ImGui::Checkbox("Wide road camera", &ss_wide_cam_);
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Panda")) {
        ss_tab_ = 1;
        // Guard: block new Panda connection if one is already active (matching Qt behavior)
        const bool panda_already_connected = stream_ && dynamic_cast<PandaStream *>(stream_);
        if (panda_already_connected) {
          ImGui::TextWrapped("Already connected to %s.", stream_->routeName().c_str());
          ImGui::TextWrapped("Close the current connection via [File menu -> Close Stream] before connecting to another Panda.");
        }
        if (panda_already_connected) ImGui::BeginDisabled();
        ImGui::Text("Serial:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(250.0f);
        int prev_serial_idx = ss_panda_serial_idx_;
        if (ss_panda_serials_.empty()) {
          ImGui::TextDisabled("No panda found");
        } else {
          const char *preview = ss_panda_serial_idx_ < static_cast<int>(ss_panda_serials_.size()) ? ss_panda_serials_[ss_panda_serial_idx_].c_str() : "";
          if (ImGui::BeginCombo("##ss_serial", preview)) {
            for (int i = 0; i < static_cast<int>(ss_panda_serials_.size()); ++i) {
              if (ImGui::Selectable(ss_panda_serials_[i].c_str(), ss_panda_serial_idx_ == i)) ss_panda_serial_idx_ = i;
            }
            ImGui::EndCombo();
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh##panda")) {
          ss_panda_serials_ = Panda::list();
          ss_panda_serial_idx_ = 0;
          prev_serial_idx = -1;  // Force FD re-probe
        }
        // Probe CAN-FD capability when serial changes or on first tab display
        if ((prev_serial_idx != ss_panda_serial_idx_ || ss_panda_needs_probe_) && ss_panda_serial_idx_ < static_cast<int>(ss_panda_serials_.size())) {
          ss_panda_needs_probe_ = false;
          ss_panda_has_fd_ = false;
          try {
            Panda panda(ss_panda_serials_[ss_panda_serial_idx_]);
            ss_panda_has_fd_ = (panda.hw_type == cereal::PandaState::PandaType::RED_PANDA) ||
                               (panda.hw_type == cereal::PandaState::PandaType::RED_PANDA_V2);
          } catch (...) {}
        }
        if (!ss_panda_serials_.empty()) {
          ImGui::Spacing();
          for (int bus = 0; bus < 3; ++bus) {
            ImGui::PushID(bus);
            ImGui::Text("Bus %d:", bus);
            ImGui::SameLine(70.0f);
            ImGui::Text("CAN Speed:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            const char *speed_labels[] = {"10", "20", "50", "100", "125", "250", "500", "1000"};
            ImGui::Combo("##speed", &ss_bus_config_[bus].speed_idx, speed_labels, 8);
            if (ss_panda_has_fd_) {
              ImGui::SameLine();
              ImGui::Checkbox("CAN-FD", &ss_bus_config_[bus].can_fd);
              if (ss_bus_config_[bus].can_fd) {
                ImGui::SameLine();
                ImGui::Text("Data:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(100.0f);
                const char *data_speed_labels[] = {"10", "20", "50", "100", "125", "250", "500", "1000", "2000", "5000"};
                ImGui::Combo("##dspeed", &ss_bus_config_[bus].data_speed_idx, data_speed_labels, 10);
              }
            }
            ImGui::PopID();
          }
        }
        if (panda_already_connected) ImGui::EndDisabled();
        ImGui::EndTabItem();
      }
#ifndef __APPLE__
      if (ImGui::BeginTabItem("SocketCAN")) {
        ss_tab_ = 2;
        ImGui::Text("Device:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(250.0f);
        if (ss_can_devices_.empty()) {
          ImGui::TextDisabled("No CAN devices found");
        } else {
          const char *preview = ss_can_device_idx_ < static_cast<int>(ss_can_devices_.size()) ? ss_can_devices_[ss_can_device_idx_].c_str() : "";
          if (ImGui::BeginCombo("##ss_can_dev", preview)) {
            for (int i = 0; i < static_cast<int>(ss_can_devices_.size()); ++i) {
              if (ImGui::Selectable(ss_can_devices_[i].c_str(), ss_can_device_idx_ == i)) ss_can_device_idx_ = i;
            }
            ImGui::EndCombo();
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Refresh##can")) {
          ss_can_devices_.clear();
          namespace fs = std::filesystem;
          const fs::path net_dir("/sys/class/net");
          if (fs::is_directory(net_dir)) {
            for (const auto &entry : fs::directory_iterator(net_dir)) {
              if (!entry.is_directory()) continue;
              std::ifstream type_file(entry.path() / "type");
              if (type_file) {
                int type = 0;
                type_file >> type;
                if (type == 280) ss_can_devices_.push_back(entry.path().filename().string());
              }
            }
          }
          ss_can_device_idx_ = 0;
        }
        ImGui::EndTabItem();
      }
#endif
      if (ImGui::BeginTabItem("Device")) {
        ss_tab_ = 3;
        ImGui::RadioButton("MSGQ", &ss_device_mode_, 0);
        ImGui::RadioButton("ZMQ", &ss_device_mode_, 1);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(250.0f);
        const bool zmq_mode = ss_device_mode_ == 1;
        if (!zmq_mode) ImGui::BeginDisabled();
        ImGui::InputTextWithHint("##ss_ip", "Enter device IP address", ss_device_ip_.data(), ss_device_ip_.size());
        if (!zmq_mode) ImGui::EndDisabled();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }

    // Error display
    if (!ss_error_.empty()) {
      ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "%s", ss_error_.c_str());
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Open / Cancel buttons
    bool can_open = true;
    if (ss_tab_ == 1 && (ss_panda_serials_.empty() || (stream_ && dynamic_cast<PandaStream *>(stream_)))) can_open = false;
    if (ss_tab_ == 2 && ss_can_devices_.empty()) can_open = false;

    if (!can_open) ImGui::BeginDisabled();
    if (ImGui::Button("Open", ImVec2(100, 0))) {
      ss_error_.clear();
      AbstractStream *new_stream = nullptr;
      try {
        if (ss_tab_ == 0) {
          // Replay
          std::string route_str(ss_route_.data());
          if (route_str.empty()) {
            ss_error_ = "Please enter a route name.";
          } else {
            // Split local directory path into (route_name, data_dir) like Qt does
            std::string data_dir;
            if (std::filesystem::exists(route_str)) {
              auto pos = route_str.find_last_of('/');
              if (pos != std::string::npos) {
                data_dir = route_str.substr(0, pos + 1);
                route_str = route_str.substr(pos + 1);
              }
            }
            auto route_id = Route::parseRoute(route_str);
            if (route_id.str.empty() && data_dir.empty()) {
              ss_error_ = "Invalid route format: '" + route_str + "'";
            } else {
              uint32_t flags = REPLAY_FLAG_NONE;
              if (ss_driver_cam_) flags |= REPLAY_FLAG_DCAM;
              if (ss_wide_cam_) flags |= REPLAY_FLAG_ECAM;
              if (!ss_road_cam_ && !ss_driver_cam_ && !ss_wide_cam_) flags |= REPLAY_FLAG_NO_VIPC;
              auto *rs = new ReplayStream();
              if (rs->loadRoute(route_str, data_dir, flags)) {
                new_stream = rs;
              } else {
                RouteLoadError err = rs->getReplay()->lastRouteError();
                delete rs;
                if (err == RouteLoadError::Unauthorized) {
                  std::string auth_path = util::getenv("HOME") + "/.comma/auth.json";
                  if (util::read_file(auth_path).empty()) {
                    ss_error_ = "Authentication Required. Please run:\n  python3 tools/lib/auth.py\nto grant access to routes from your comma account.";
                  } else {
                    ss_error_ = "Access Denied. You do not have permission to access route:\n  " + route_str + "\nThis is likely a private route.";
                  }
                } else if (err == RouteLoadError::NetworkError) {
                  ss_error_ = "Network Error. Unable to load route:\n  " + route_str + "\nPlease check your network connection and try again.";
                } else if (err == RouteLoadError::FileNotFound) {
                  ss_error_ = "Route Not Found:\n  " + route_str + "\nPlease check the route name and try again.";
                } else if (data_dir.empty()) {
                  ss_error_ = "Failed to load remote route '" + route_str + "'.";
                } else {
                  ss_error_ = "Failed to load local route '" + route_str + "' from '" + data_dir + "'. Verify the directory contains valid log files.";
                }
              }
            }
          }
        } else if (ss_tab_ == 1) {
          // Panda
          PandaStreamConfig config;
          config.serial = ss_panda_serials_[ss_panda_serial_idx_];
          const uint32_t speed_values[] = {10, 20, 50, 100, 125, 250, 500, 1000};
          const uint32_t data_speed_values[] = {10, 20, 50, 100, 125, 250, 500, 1000, 2000, 5000};
          config.bus_config.resize(3);
          for (int i = 0; i < 3; ++i) {
            config.bus_config[i].can_speed_kbps = speed_values[ss_bus_config_[i].speed_idx];
            config.bus_config[i].can_fd = ss_bus_config_[i].can_fd;
            config.bus_config[i].data_speed_kbps = data_speed_values[ss_bus_config_[i].data_speed_idx];
          }
          new_stream = new PandaStream(config);
        } else if (ss_tab_ == 2) {
          // SocketCAN
#ifndef __APPLE__
          SocketCanStreamConfig config;
          config.device = ss_can_devices_[ss_can_device_idx_];
          new_stream = new SocketCanStream(config);
#endif
        } else if (ss_tab_ == 3) {
          // Device
          std::string addr;
          if (ss_device_mode_ == 1) {  // ZMQ
            addr = std::string(ss_device_ip_.data());
            if (addr.empty()) {
              addr = "127.0.0.1";  // Match Qt's default
            } else {
              // Basic IPv4 validation
              struct in_addr tmp;
              if (inet_pton(AF_INET, addr.c_str(), &tmp) != 1) {
                ss_error_ = "Invalid IP address: " + addr;
                addr.clear();
              }
            }
          }
          if (!addr.empty() || ss_device_mode_ == 0) {
            new_stream = new DeviceStream(addr);
          }
        }
      } catch (const std::exception &e) {
        ss_error_ = std::string("Connection failed: ") + e.what();
      }

      if (new_stream) {
        // For DeviceStream, pre-start to check for bridge errors before destroying current session
        if (auto *ds = dynamic_cast<DeviceStream *>(new_stream)) {
          ds->start();
          if (!ds->lastError().empty()) {
            ss_error_ = ds->lastError();
            delete new_stream;
            new_stream = nullptr;
          }
        }
        if (new_stream) {
          replaceStream(new_stream, ss_dbc_file_);
          ImGui::CloseCurrentPopup();
        }
      }
    }
    if (!can_open) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
    }

    ImGui::EndPopup();
  }}

  // Modal loading progress overlay (matches Qt's QProgressDialog during initial route load)
  // Show when downloading and no actual CAN events received yet (stream has no last messages)
  if (download_active_.load(std::memory_order_acquire) && replayStream() &&
      (!stream_ || stream_->lastMessages().empty())) {
    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(320, 0));
    if (ImGui::BeginPopupModal("Loading Route...", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
      uint64_t cur = download_cur_.load(std::memory_order_relaxed);
      uint64_t total = download_total_.load(std::memory_order_relaxed);
      float frac = total > 0 ? static_cast<float>(static_cast<double>(cur) / total) : 0.0f;
      ImGui::ProgressBar(frac, ImVec2(-1, 0));
      if (total > (1024 * 1024)) {
        ImGui::Text("Downloading %.1f / %.1f MB", cur / (1024.0 * 1024.0), total / (1024.0 * 1024.0));
      } else {
        ImGui::Text("Downloading %d%%", total > 0 ? static_cast<int>(frac * 100) : 0);
      }
      // Abort button (matches Qt's QProgressDialog abort action)
      if (ImGui::Button("Abort")) {
        ImGui::CloseCurrentPopup();
        exit_requested_ = true;
      }
      ImGui::EndPopup();
    }
    if (!ImGui::IsPopupOpen("Loading Route...")) {
      ImGui::OpenPopup("Loading Route...");
    }
  } else if (ImGui::IsPopupOpen("Loading Route...")) {
    // Close the loading popup when no longer needed
    ImGui::OpenPopup("Loading Route...");  // Ensure popup context exists
    if (ImGui::BeginPopupModal("Loading Route...", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
  }

  // Help overlay (Qt-style: semi-transparent overlay with per-panel help text, click/Escape/F1 to dismiss)
  if (show_help_) {
    // Full-screen invisible window to capture mouse input and prevent click-through
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(display);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
    if (ImGui::Begin("##HelpOverlayInput", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoDecoration)) {
      if (ImGui::InvisibleButton("##help_capture", display)) {
        show_help_ = false;
      }
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      show_help_ = false;
    }
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 128));

    auto drawPanelHelp = [&](const PanelRect &r, const char *title, const char *text) {
      if (r.w <= 0 || r.h <= 0) return;
      const float pad = 12.0f;
      ImVec2 text_size = ImGui::CalcTextSize(text, nullptr, false, r.w - pad * 2);
      ImVec2 title_size = ImGui::CalcTextSize(title);
      float total_h = title_size.y + 6.0f + text_size.y;
      float cx = r.x + r.w * 0.5f;
      float cy = r.y + r.h * 0.5f;
      ImVec2 box_min(cx - (std::max(text_size.x, title_size.x) + pad * 2) * 0.5f, cy - (total_h + pad * 2) * 0.5f);
      ImVec2 box_max(cx + (std::max(text_size.x, title_size.x) + pad * 2) * 0.5f, cy + (total_h + pad * 2) * 0.5f);
      fg->AddRectFilled(box_min, box_max, IM_COL32(30, 30, 35, 220), 0.0f);
      fg->AddText(ImVec2(cx - title_size.x * 0.5f, box_min.y + pad), IM_COL32(130, 200, 255, 255), title);
      fg->AddText(nullptr, 0.0f, ImVec2(box_min.x + pad, box_min.y + pad + title_size.y + 6.0f),
                  IM_COL32(220, 220, 220, 255), text, nullptr, r.w - pad * 2);
    };

    if (show_messages_) {
      drawPanelHelp(panel_messages_, "Message View",
                    "Byte color: gray = constant, blue = increasing, red = decreasing.\n"
                    "Horizontal scrolling: Shift+wheel.");
    }
    drawPanelHelp(panel_binary_, "Binary View",
                  "Click and drag to create signals.\n"
                  "Signals are color-coded by assignment.\n"
                  "A: Add signal  E: Toggle endian\n"
                  "S: Toggle signed  X/Del: Remove\n"
                  "C/G/P: Plot signal");
    drawPanelHelp(panel_signals_, "Signals",
                  "Click to select, double-click or Enter to edit.");
    if (panel_video_.w > 0) {
      drawPanelHelp(panel_video_, "Video / Timeline",
                    "Timeline colors: gray = disengaged,\n"
                    "green = engaged, magenta = user flag,\n"
                    "blue = info, yellow = warning, red = critical.\n"
                    "Space: Pause/Resume  Click: Seek");
    }
    if (panel_charts_.w > 0) {
      drawPanelHelp(panel_charts_, "Chart View",
                    "Click: Seek to time  Drag: Zoom\n"
                    "Shift+Drag: Scrub  Mid-drag: Pan\n"
                    "Scroll: Zoom  +/-: Zoom in/out\n"
                    "Right-click: Undo zoom");
    }

    // Shortcut list at top center
    const char *shortcuts =
      "F1: Toggle help  Shift+F1: What's This?  Ctrl+O: Open DBC  Ctrl+S: Save  Ctrl+Shift+S: Save as\n"
      "Ctrl+Z / Ctrl+Shift+Z: Undo/Redo  Space: Pause/Resume  Left/Right: Seek\n"
      "Ctrl+N: New DBC  F11: Full screen  Ctrl+Q: Exit  Esc: Close dialog";
    ImVec2 sc_size = ImGui::CalcTextSize(shortcuts);
    float sc_x = (display.x - sc_size.x) * 0.5f;
    float sc_y = 40.0f;
    fg->AddRectFilled(ImVec2(sc_x - 10, sc_y - 6), ImVec2(sc_x + sc_size.x + 10, sc_y + sc_size.y + 6),
                      IM_COL32(30, 30, 35, 220), 0.0f);
    fg->AddText(ImVec2(sc_x, sc_y), IM_COL32(180, 180, 180, 255), shortcuts);

    // "Click anywhere to close" hint at bottom
    const char *hint = "Click anywhere or press F1 / Escape to close";
    ImVec2 hint_size = ImGui::CalcTextSize(hint);
    fg->AddText(ImVec2((display.x - hint_size.x) * 0.5f, display.y - 30.0f), IM_COL32(140, 140, 140, 200), hint);
  }

  // "What's This?" mode (Shift+F1): click on a panel to see its help (matches Qt's QWhatsThis)
  if (whats_this_mode_) {
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    ImDrawList *fg = ImGui::GetForegroundDrawList();
    fg->AddRectFilled(ImVec2(0, 0), display, IM_COL32(0, 0, 0, 64));

    // Show "?" cursor hint near mouse
    ImVec2 mouse = ImGui::GetIO().MousePos;
    fg->AddText(ImVec2(mouse.x + 16, mouse.y), IM_COL32(255, 255, 100, 255), "?");

    const char *hint = "Click on a panel for help, or press Escape to cancel";
    ImVec2 hint_size = ImGui::CalcTextSize(hint);
    fg->AddRectFilled(ImVec2((display.x - hint_size.x) * 0.5f - 8, 38),
                      ImVec2((display.x + hint_size.x) * 0.5f + 8, 38 + hint_size.y + 12),
                      IM_COL32(30, 30, 35, 220), 0.0f);
    fg->AddText(ImVec2((display.x - hint_size.x) * 0.5f, 44), IM_COL32(180, 180, 180, 255), hint);

    // Per-panel help text (matches Qt's whatsThis() content)
    struct PanelHelp { PanelRect r; const char *title; const char *text; };
    PanelHelp panels[] = {
      {panel_messages_, "Message View",
       "Byte color:\n"
       "  Gray = constant changing\n"
       "  Blue = increasing\n"
       "  Red = decreasing\n\n"
       "Shortcuts:\n"
       "  Shift+Wheel: Horizontal scrolling"},
      {panel_binary_, "Binary View",
       "Click and drag to create signals.\n"
       "Signals are color-coded by assignment.\n\n"
       "Shortcuts:\n"
       "  X / Backspace / Delete: Remove signal\n"
       "  E: Toggle endianness\n"
       "  S: Toggle signedness\n"
       "  C / G / P: Plot signal in chart"},
      {panel_signals_, "Signal View",
       "Click to select a signal.\n"
       "Double-click or Enter to edit.\n"
       "Use the editor to modify properties."},
      {panel_video_, "Video / Timeline",
       "Timeline colors:\n"
       "  Gray = disengaged, Green = engaged\n"
       "  Magenta = user flag, Blue = info\n"
       "  Yellow = warning, Red = critical\n\n"
       "Space: Pause/Resume  Click: Seek"},
      {panel_charts_, "Chart View",
       "Click: Seek to time  Drag: Zoom\n"
       "Shift+Drag: Scrub  Middle-drag: Pan\n"
       "Scroll: Zoom  +/-: Zoom in/out\n"
       "Right-click: Undo zoom"},
    };

    // Highlight hovered panel
    for (const auto &p : panels) {
      if (p.r.w <= 0 || p.r.h <= 0) continue;
      ImVec2 pmin(p.r.x, p.r.y), pmax(p.r.x + p.r.w, p.r.y + p.r.h);
      if (mouse.x >= pmin.x && mouse.x <= pmax.x && mouse.y >= pmin.y && mouse.y <= pmax.y) {
        fg->AddRectFilled(pmin, pmax, IM_COL32(100, 180, 255, 30));
        fg->AddRect(pmin, pmax, IM_COL32(100, 180, 255, 120), 0.0f, 0, 2.0f);
      }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
      whats_this_mode_ = false;
    }

    if (ImGui::IsMouseClicked(0)) {
      bool found = false;
      for (const auto &p : panels) {
        if (p.r.w <= 0 || p.r.h <= 0) continue;
        if (mouse.x >= p.r.x && mouse.x <= p.r.x + p.r.w &&
            mouse.y >= p.r.y && mouse.y <= p.r.y + p.r.h) {
          // Show help popup for this panel
          whats_this_mode_ = false;
          whats_this_panel_title_ = p.title;
          whats_this_panel_text_ = p.text;
          found = true;
          break;
        }
      }
      if (!found) whats_this_mode_ = false;
    }
  }

  // "What's This" result popup
  if (!whats_this_panel_title_.empty() && !ImGui::IsPopupOpen("##WhatsThis")) {
    ImGui::OpenPopup("##WhatsThis");
  }
  if (ImGui::BeginPopup("##WhatsThis")) {
    ImGui::TextColored(ImVec4(0.5f, 0.78f, 1.0f, 1.0f), "%s", whats_this_panel_title_.c_str());
    ImGui::Separator();
    ImGui::TextUnformatted(whats_this_panel_text_.c_str());
    ImGui::EndPopup();
  } else if (!whats_this_panel_title_.empty()) {
    // Popup was dismissed — clear stored content
    whats_this_panel_title_.clear();
    whats_this_panel_text_.clear();
  }

  // Error modal (matches Qt's QMessageBox::warning for important failures)
  if (!error_modal_title_.empty()) {
    if (!ImGui::IsPopupOpen(error_modal_title_.c_str())) {
      ImGui::OpenPopup(error_modal_title_.c_str());
    }
    bool error_open = true;
    if (ImGui::BeginPopupModal(error_modal_title_.c_str(), &error_open, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", error_modal_message_.c_str());
      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
        error_open = false;
      }
      dismissOnEscape();
      ImGui::EndPopup();
    }
    if (!error_open) {
      error_modal_title_.clear();
      error_modal_message_.clear();
    }
  }

  ImGui::End();
}
