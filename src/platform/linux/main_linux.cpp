#include "AppBootstrap_linux.h"
#include "core/main_common.h"

// Traits struct for Linux bootstrap
struct LinuxBootstrapTraits {
  static AppBootstrapResultLinux Initialize(const CommandLineArgs& cmd_args,
                                            FileIndex& file_index,
                                            int& last_window_width,
                                            int& last_window_height) {
    return AppBootstrapLinux::Initialize(cmd_args, file_index,
                                         last_window_width, last_window_height);
  }

  static void Cleanup(AppBootstrapResultLinux& result) {
    AppBootstrapLinux::Cleanup(result);
  }
};

// Main entry point for Linux
int main(int argc, char** argv) {
  return RunApplicationWithCatch<AppBootstrapResultLinux, LinuxBootstrapTraits>(argc, argv);
}

