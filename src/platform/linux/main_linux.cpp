#include "AppBootstrap_linux.h"
#include "core/main_common.h"

#ifdef ENABLE_MIMALLOC
// Static override path: the force-loaded mimalloc-static archive (see
// apply_mimalloc) already defines global new/delete (mimalloc compiles its
// sources as C++), so this TU must NOT include <mimalloc-new-delete.h> (it
// would redefine the same operators -> duplicate symbols at link). Keep
// <mimalloc.h> for the API; the mi_version() call in main() touches the
// allocator first so the archive is engaged before anything allocates.
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

