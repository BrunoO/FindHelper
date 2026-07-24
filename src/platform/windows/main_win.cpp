#include "AppBootstrap_win.h"
#include "core/main_common.h"
#include "platform/windows/CrashHandler_win.h"

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
  crash_handler::InstallCrashHandler();
  return RunApplicationWithCatch<AppBootstrapResult, WindowsBootstrapTraits>(argc, argv);
}

// WndProc removed: GLFW handles window messages internally
// Window resize is handled via glfwSetWindowSizeCallback in AppBootstrap
