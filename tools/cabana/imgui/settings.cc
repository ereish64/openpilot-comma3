#include "tools/cabana/imgui/settings.h"

#include <fstream>
#include <sstream>

#include "third_party/json11/json11.hpp"
#include "tools/cabana/imgui/util.h"

Settings settings;

static std::string settingsPath() {
  return homeDir() + "/.cabana_settings.json";
}

Settings::Settings() {
  std::string home_dir = homeDir();
  last_dir = home_dir;
  last_route_dir = home_dir;
  log_path = home_dir + "/cabana_live_stream/";
  load();
}

Settings::~Settings() {
  save();
}

void Settings::load() {
  std::ifstream f(settingsPath());
  if (!f.is_open()) return;

  std::stringstream buf;
  buf << f.rdbuf();
  std::string err;
  auto j = json11::Json::parse(buf.str(), err);
  if (!err.empty()) return;

  if (j["absolute_time"].is_bool()) absolute_time = j["absolute_time"].bool_value();
  if (j["fps"].is_number()) fps = j["fps"].int_value();
  if (j["max_cached_minutes"].is_number()) max_cached_minutes = j["max_cached_minutes"].int_value();
  if (j["chart_height"].is_number()) chart_height = j["chart_height"].int_value();
  if (j["chart_range"].is_number()) chart_range = j["chart_range"].int_value();
  if (j["chart_column_count"].is_number()) chart_column_count = j["chart_column_count"].int_value();
  if (j["chart_series_type"].is_number()) chart_series_type = j["chart_series_type"].int_value();
  if (j["theme"].is_number()) theme = j["theme"].int_value();
  if (j["sparkline_range"].is_number()) sparkline_range = j["sparkline_range"].int_value();
  if (j["multiple_lines_hex"].is_bool()) multiple_lines_hex = j["multiple_lines_hex"].bool_value();
  if (j["log_livestream"].is_bool()) log_livestream = j["log_livestream"].bool_value();
  if (j["video_crop_to_fill"].is_bool()) video_crop_to_fill = j["video_crop_to_fill"].bool_value();
  if (j["suppress_defined_signals"].is_bool()) suppress_defined_signals = j["suppress_defined_signals"].bool_value();
  if (j["log_path"].is_string()) log_path = j["log_path"].string_value();
  if (j["last_dir"].is_string()) last_dir = j["last_dir"].string_value();
  if (j["last_route_dir"].is_string()) last_route_dir = j["last_route_dir"].string_value();
  if (j["drag_direction"].is_number()) drag_direction = static_cast<DragDirection>(j["drag_direction"].int_value());
  if (j["window_x"].is_number()) window_x = j["window_x"].int_value();
  if (j["window_y"].is_number()) window_y = j["window_y"].int_value();
  if (j["window_width"].is_number()) window_width = j["window_width"].int_value();
  if (j["window_height"].is_number()) window_height = j["window_height"].int_value();
  if (j["window_maximized"].is_bool()) window_maximized = j["window_maximized"].bool_value();

  if (j["recent_files"].is_array()) {
    recent_files.clear();
    for (const auto &item : j["recent_files"].array_items())
      if (item.is_string()) recent_files.push_back(item.string_value());
  }
}

void Settings::save() {
  json11::Json::array rf_arr;
  for (const auto &s : recent_files) rf_arr.push_back(s);

  json11::Json j = json11::Json::object {
    {"absolute_time", absolute_time},
    {"fps", fps},
    {"max_cached_minutes", max_cached_minutes},
    {"chart_height", chart_height},
    {"chart_range", chart_range},
    {"chart_column_count", chart_column_count},
    {"chart_series_type", chart_series_type},
    {"theme", theme},
    {"sparkline_range", sparkline_range},
    {"multiple_lines_hex", multiple_lines_hex},
    {"log_livestream", log_livestream},
    {"video_crop_to_fill", video_crop_to_fill},
    {"suppress_defined_signals", suppress_defined_signals},
    {"log_path", log_path},
    {"last_dir", last_dir},
    {"last_route_dir", last_route_dir},
    {"drag_direction", static_cast<int>(drag_direction)},
    {"window_x", window_x},
    {"window_y", window_y},
    {"window_width", window_width},
    {"window_height", window_height},
    {"window_maximized", window_maximized},
    {"recent_files", rf_arr},
  };

  std::ofstream f(settingsPath());
  if (f.is_open()) {
    f << j.dump();
  }
}

void Settings::emitChanged() {
  for (auto &cb : on_changed_) {
    cb();
  }
}
