#include "AppBootstrap_linux.h"
#include "core/main_common.h"


// Traits struct for Linux bootstrap
struct LinuxBootstrapTraits {
  static AppBootstrapResultLinux Initialize(const CommandLineArgs& cmd_args,
                                            FileIndex& file_index) {
    return AppBootstrapLinux::Initialize(cmd_args, file_index);
  }

  static void Cleanup(AppBootstrapResultLinux& result) {
    AppBootstrapLinux::Cleanup(result);
  }
};

// Main entry point for Linux
int main(int argc, char** argv) {
  return RunApplicationWithCatch<AppBootstrapResultLinux, LinuxBootstrapTraits>(argc, argv);
}

