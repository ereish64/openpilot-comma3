#pragma once

#include <functional>
#include <string>
#include <vector>

enum CabanaTheme : int {
  THEME_AUTO = 0,
  LIGHT_THEME = 1,
  DARK_THEME = 2,
};

class Settings {
public:
  enum DragDirection {
    MsbFirst,
    LsbFirst,
    AlwaysLE,
    AlwaysBE,
  };

  Settings();
  ~Settings();

  void load();
  void save();
  void emitChanged();

  bool absolute_time = false;
  int fps = 10;
  int max_cached_minutes = 30;
  int chart_height = 200;
  int chart_column_count = 1;
  int chart_range = 3 * 60;
  int chart_series_type = 0;
  int theme = 0;
  int sparkline_range = 15;
  bool multiple_lines_hex = false;
  bool log_livestream = true;
  bool video_crop_to_fill = true;
  bool suppress_defined_signals = false;
  std::string log_path;
  std::string last_dir;
  std::string last_route_dir;
  std::vector<std::string> recent_files;
  DragDirection drag_direction = MsbFirst;

  // window geometry
  int window_x = -1;
  int window_y = -1;
  int window_width = 0;
  int window_height = 0;
  bool window_maximized = false;

  // Callback registration for change notification
  std::vector<std::function<void()>> on_changed_;
};

extern Settings settings;
