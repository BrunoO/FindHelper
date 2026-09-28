#include "AppBootstrap_win.h"
#include "core/main_common.h"
#include "platform/windows/CrashHandler_win.h"

#ifdef ENABLE_MIMALLOC
// Dynamic override path (/MD runtime): mimalloc.dll itself defines global
// new/delete (mimalloc compiles its sources as C++), so this TU must NOT
// include <mimalloc-new-delete.h> (it would redefine the same operators ->
// duplicate symbols at link). The mi_version() call in main() is a genuine
// code reference from live code, so the linker keeps the mimalloc.dll import in
// every build phase (including /OPT:REF and both PGO links) with no PGO-sensitive
// link options. The redirector (mimalloc-redirect.dll, staged beside the exe by
// CMake) reroutes CRT malloc/free at runtime.
#include <mimalloc.h>
#endif  // ENABLE_MIMALLOC

// Traits struct for Windows bootstrap
struct WindowsBootstrapTraits {
  static AppBootstrapResult Initialize(const CommandLineArgs& cmd_args,
                                       FileIndex& file_index,
                                       int& last_window_width,
                                       int& last_window_height) {
    return AppBootstrap::Initialize(cmd_args, file_index,
                                    last_window_width, last_window_height);
  }

  static void Cleanup(AppBootstrapResult& result) {
    AppBootstrap::Cleanup(result);
  }
};

// Main entry point for Windows
int main(int argc, char** argv) {
#ifdef ENABLE_MIMALLOC
  // Keep the mimalloc DLL import live even when /OPT:REF is enabled.  The
  // redirector is linked directly by CMake so its import is ordered before the
  // CRT; this call only verifies that the mimalloc API is available.
  (void)mi_version();
#endif  // ENABLE_MIMALLOC
  crash_handler::InstallCrashHandler();
  return RunApplicationWithCatch<AppBootstrapResult, WindowsBootstrapTraits>(argc, argv);
}

// WndProc removed: GLFW handles window messages internally
// Window resize is handled via glfwSetWindowSizeCallback in AppBootstrap
