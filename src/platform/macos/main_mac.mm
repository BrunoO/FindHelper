#include "AppBootstrap_mac.h"
#include "core/main_common.h"


// Traits struct for macOS bootstrap
struct MacBootstrapTraits {
  static AppBootstrapResultMac Initialize(const CommandLineArgs& cmd_args,
                                          FileIndex& file_index) {
    return AppBootstrapMac::Initialize(cmd_args, file_index);
  }
  
  static void Cleanup(AppBootstrapResultMac& result) {
    AppBootstrapMac::Cleanup(result);
  }
};

// Main entry point for macOS
int main(int argc, char** argv) {
  return RunApplication<AppBootstrapResultMac, MacBootstrapTraits>(argc, argv);
}

