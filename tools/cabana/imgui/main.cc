#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <vector>

#include "tools/cabana/core/launch_config.h"
#include "tools/cabana/imgui/bootstrap.h"
#include "tools/cabana/imgui/dbcmanager.h"
#include "tools/cabana/imgui/stream.h"
#include "tools/cabana/imgui/util.h"
#include "tools/cabana/imgui/app.h"

int main(int argc, char *argv[]) {
  initApp(argc, argv);

  std::vector<std::string> args(argv, argv + argc);
  auto parsed = parseCabanaLaunchConfig(args);
  if (parsed.show_help) {
    std::cout << cabanaLaunchHelp(args.front());
    return 0;
  }
  if (!parsed.ok) {
    std::cerr << parsed.error << "\n\n" << cabanaLaunchHelp(args.front());
    return 1;
  }

  std::string startup_error;
  try {
    can = createStreamForLaunchConfig(parsed.config, &startup_error);
  } catch (const std::exception &e) {
    startup_error = std::string("Failed to create stream: ") + e.what();
    std::cerr << startup_error << "\n";
    can = nullptr;
  }
  if (can) {
    can->start();
  } else {
    if (!startup_error.empty()) {
      std::cerr << startup_error << "\n";
    }
    can = new DummyStream();
  }

  std::string dbc_error;
  if (!parsed.config.dbc_file.empty()) {
    dbc()->open(SOURCE_ALL, parsed.config.dbc_file, &dbc_error);
    if (!dbc_error.empty()) {
      std::cerr << "Failed to load DBC file '" << parsed.config.dbc_file << "': " << dbc_error << "\n";
    }
  }

  CabanaImguiApp app(std::move(parsed.config), can);
  if (!startup_error.empty()) app.setStartupError(startup_error);
  if (!dbc_error.empty()) app.setStartupError("DBC: " + dbc_error);
  const int exit_code = app.run();
  std::fflush(nullptr);
  std::_Exit(exit_code);
}
