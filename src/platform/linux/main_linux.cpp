#include "AppBootstrap_linux.h"
#include "core/main_common.h"

#ifdef ENABLE_MIMALLOC
// Static override path: route global new/delete through mimalloc in this TU
// (single TU per binary) and touch the allocator first in main() so the
// force-loaded archive (see apply_mimalloc) is engaged before anything allocates.
#include <mimalloc-new-delete.h>
#include <mimalloc.h>
#endif  // ENABLE_MIMALLOC

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
#ifdef ENABLE_MIMALLOC
  (void)mi_version();
#endif  // ENABLE_MIMALLOC
  return RunApplicationWithCatch<AppBootstrapResultLinux, LinuxBootstrapTraits>(argc, argv);
}

