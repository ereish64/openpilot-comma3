#pragma once

#include <cstddef>
#include <string>
#include <vector>

struct CabanaPersistentState {
  std::string last_dir;
  std::vector<std::string> recent_files;
};

void rememberRecentFile(CabanaPersistentState &state, const std::string &filename, size_t max_recent_files);
