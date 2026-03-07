/**
 * @file AppBootstrap.cpp
 * @brief Implementation of Windows-specific application initialization and cleanup
 *
 * This module implements the complete Windows application initialization sequence:
 * - GLFW window creation with DPI awareness
 * - COM/OLE initialization for Shell APIs
 * - Administrator privilege checking and elevation
 * - Index loading from file (if requested)
 * - Settings loading with command-line overrides
 * - DirectX 11 device and swap chain creation
 * - ImGui context and backends initialization
 * - UsnMonitor startup (if not indexing from file)
 *
 * All initialization is wrapped in exception handling to ensure proper cleanup
 * on failure. The window resize callback is configured to update DirectX swap
 * chain and track window dimensions.
 */

// GLFW defines must be set before including AppBootstrapCommon.h (which includes glfw3.h)
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE

#include <cassert>
#include <cctype>
#include <chrono>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <thread>
#include <utility>

// GLFW includes (native Win32 access for DirectX HWND)
// Note: GLFW defines are set before AppBootstrapCommon.h include above
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <ole2.h>
#include <tchar.h>
#include <windows.h>  // NOSONAR(cpp:S3806) - lowercase in source; Sonar may see SDK case on Windows

#include "AppBootstrap_win.h"
#include "core/AppBootstrapCommon.h"
#include "core/CommandLineArgs.h"
#include "core/Settings.h"
#include "core/Version.h"
#include "gui/RendererInterface.h"
#include "imgui.h"
#include "imgui_impl_dx11.h"
#include "imgui_impl_glfw.h"
#include "index/FileIndex.h"
#include "index/IndexFromFilePopulator.h"
#include "platform/FileOperations.h"
#include "platform/windows/DirectXManager.h"
#include "platform/windows/FontUtils_win.h"
#include "platform/windows/ShellContextUtils.h"
#include "platform/windows/resource.h"
#include "usn/UsnMonitor.h"
#include "utils/CpuFeatures.h"
#include "utils/Logger.h"

#ifdef _WIN32
// Helper function to convert volume string (e.g., "C:") to Windows volume path format ("\\\\.\\C:")
static std::string ToWindowsVolumePath(std::string_view volume) {
  return R"(\\.\)" + std::string(volume);
}

// Helper to verify that a given monitored volume (e.g., "C:") is an NTFS volume
// with USN Journal support. Returns true if the filesystem is NTFS and exposes
// USN (FILE_SUPPORTS_USN_JOURNAL), false otherwise (and logs a diagnostic).
static bool IsNtfsVolume(std::string_view volume) {
  if (volume.length() != 2 || volume[1] != ':') {
    LOG_WARNING_BUILD("Invalid monitoredVolume format '" << volume
                     << "' (expected format like \"C:\"), USN monitoring disabled for this volume.");
    return false;
  }

  const auto raw_drive = static_cast<unsigned char>(volume[0]);
  const auto drive_letter =
      static_cast<char>(std::toupper(raw_drive));
  if (std::isalpha(static_cast<unsigned char>(drive_letter)) == 0) {
    LOG_WARNING_BUILD("Invalid monitoredVolume drive letter '" << volume
                     << "', USN monitoring disabled for this volume.");
    return false;
  }

  std::array<char, 4> root_path{{drive_letter, ':', '\\', '\0'}};

  // Optional: log drive type to aid diagnostics, but do not gate USN on it directly.
  if (UINT drive_type = GetDriveTypeA(root_path.data());
      drive_type == DRIVE_UNKNOWN || drive_type == DRIVE_NO_ROOT_DIR) {
    LOG_ERROR_BUILD("Volume '" << volume
                   << "' has invalid or unknown drive type ("
                   << drive_type
                   << "), USN monitoring disabled for this volume.");
    return false;
  }

  std::array<char, 32> fs_name_buffer{};
  DWORD fs_flags = 0;

  if (!GetVolumeInformationA(root_path.data(),
                             nullptr,
                             0,
                             nullptr,
                             nullptr,
                             &fs_flags,
                             fs_name_buffer.data(),
                             static_cast<DWORD>(fs_name_buffer.size()))) {
    const DWORD err = GetLastError();
    LOG_ERROR_BUILD("GetVolumeInformationA failed for root '" << root_path.data()
                   << "' (error " << err
                   << "), USN monitoring disabled for this volume.");
    return false;
  }

  // Compare filesystem name case-insensitively against "NTFS".
  if (_stricmp(fs_name_buffer.data(), "NTFS") != 0) {
    LOG_ERROR_BUILD("USN monitoring requires NTFS. Volume " << volume
                   << " is formatted as " << fs_name_buffer.data()
                   << " - real-time USN monitoring is disabled.");
    return false;
  }

  // Ensure the filesystem reports USN Journal support explicitly.
  if ((fs_flags & FILE_SUPPORTS_USN_JOURNAL) == 0) {
    LOG_ERROR_BUILD("Volume '" << volume
                   << "' is NTFS but does not report FILE_SUPPORTS_USN_JOURNAL; "
                   << "real-time USN monitoring is disabled for this volume.");
    return false;
  }

  return true;
}

namespace {
// PGO (Profile-Guided Optimization) runtime flush. Called from platform_exit_hook only on Windows.
void FlushPgoData(const char* name) {
  HMODULE pgort_handle = GetModuleHandleA("pgort140.dll");  // NOSONAR(cpp:S5350) - HMODULE is opaque handle; pointer is reassigned for fallback DLL, pointee is never modified
  if (pgort_handle == nullptr) {
    pgort_handle = GetModuleHandleA("pgort.dll");
  }
  if (pgort_handle != nullptr) {
    using PgoAutoSweepFunc = void (*)(const char*);
    auto pgo_autosweep = reinterpret_cast<PgoAutoSweepFunc>(GetProcAddress(pgort_handle, "PgoAutoSweep"));  // NOSONAR(cpp:S3630) - GetProcAddress returns void*, must cast to function pointer type
    if (pgo_autosweep != nullptr) {
      pgo_autosweep(name);
    }
  }
}

void OnPlatformExit(const char* reason) {
  const bool is_shutdown = (std::strcmp(reason, "shutdown") == 0);
  if (is_shutdown) {
    HMODULE pgort_handle = GetModuleHandleA("pgort140.dll");  // NOSONAR(cpp:S5350) - HMODULE is opaque handle; pointer is reassigned for fallback DLL, pointee is never modified
    if (pgort_handle == nullptr) {
      pgort_handle = GetModuleHandleA("pgort.dll");
    }
    if (pgort_handle != nullptr) {
      LOG_IMPORTANT_BUILD("PGO runtime detected (instrumented build). Flushing profile data...");
      FlushPgoData("shutdown");
      std::this_thread::sleep_for(std::chrono::milliseconds(500));
      std::string exe_path(MAX_PATH, '\0');
      const DWORD path_len = GetModuleFileNameA(nullptr, exe_path.data(), static_cast<DWORD>(exe_path.size()));
      if (path_len > 0) {
        exe_path.resize(path_len);
        const size_t last_slash = exe_path.find_last_of("\\/");
        const std::string exe_dir = (last_slash != std::string::npos) ? exe_path.substr(0, last_slash) : exe_path;
        LOG_IMPORTANT_BUILD("PGO profile flush complete. Check for FindHelper*.pgc files in: " << exe_dir);
      } else {
        LOG_IMPORTANT_BUILD("PGO profile flush complete. Check for FindHelper*.pgc files in executable directory.");
      }
    } else {
      LOG_INFO_BUILD("PGO runtime not detected (non-instrumented build). PgoAutoSweep() is a no-op.");
    }
  } else {
    FlushPgoData("exception_exit");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

bool InitializeCom(AppBootstrapResult& result) {
  HRESULT com_init_result = OleInitialize(nullptr);
  if (FAILED(com_init_result)) {
    LOG_ERROR("Failed to initialize OLE/COM library.");
    return false;
  }
  result.com_initialized = true;
  return true;
}

bool HandleIndexingWithoutAdmin(const CommandLineArgs& cmd_args, FileIndex& /* file_index */) {
  // GRACEFUL DEGRADATION: Continue to UI without admin rights
  // Indexing will be handled asynchronously by the normal flow in main_common.h
  // (either via auto-crawl, or via index builder if crawl_folder is provided)
  // This matches the behavior on macOS/Linux where indexing happens asynchronously
  if (!cmd_args.index_from_file.empty()) {
    LOG_INFO_BUILD(
      "Index file provided, will be loaded in normal flow: " << cmd_args.index_from_file);
  } else if (!cmd_args.crawl_folder.empty()) {
    LOG_INFO("Running without admin rights with --crawl-folder specified. "
             "USN Journal monitoring disabled. "
             "Folder crawl will start asynchronously after UI is shown.");
  } else {
    LOG_INFO("Running without admin rights and no folder specified. "
             "USN Journal monitoring disabled. "
             "Auto-crawl will use settings folder or HOME folder if available.");
  }
  return true;  // Always continue to UI - let main_common.h handle indexing asynchronously
}

bool InitializeDirectXAndWindow(GLFWwindow* window, DirectXManager& direct_x_manager,
                                int& last_window_width, int& last_window_height,
                                const AppSettings& app_settings) {
  HWND hwnd = glfwGetWin32Window(window);
  if (hwnd == nullptr) {
    LOG_ERROR("glfwGetWin32Window returned null HWND");
    glfwDestroyWindow(window);
    glfwTerminate();
    return false;
  }
  // Assert only in debug builds for additional safety
  assert(hwnd != nullptr);

  // Set window icon explicitly (ensures icon appears even if resource embedding has issues)
  HICON hIcon = LoadIcon(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDI_APP_ICON));
  if (hIcon != nullptr) {  // NOSONAR(cpp:S6004) - Variable used after if block (line 223-224)
    SendMessage(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(hIcon));
    SendMessage(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(hIcon));
  }

  last_window_width = app_settings.windowWidth;
  last_window_height = app_settings.windowHeight;

  AppBootstrapCommon::SetupWindowResizeCallback(window, &direct_x_manager, &last_window_width,
                                                &last_window_height);

  if (!direct_x_manager.Initialize(window)) {
    LOG_ERROR("Failed to initialize Direct3D");
    glfwDestroyWindow(window);
    glfwTerminate();
    return false;
  }

  return true;
}

bool InitializeImGuiBackend(GLFWwindow* window, DirectXManager& direct_x_manager) {
  AppBootstrapCommon::InitializeImGuiContext();

  ImGui_ImplGlfw_InitForOther(window, true);
  if (!direct_x_manager.InitializeImGui()) {
    LOG_ERROR("Failed to initialize ImGui renderer backend");
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    direct_x_manager.Cleanup();
    glfwDestroyWindow(window);
    glfwTerminate();
    return false;
  }

  return true;
}

// Forward declaration
void StartUsnMonitor(const CommandLineArgs& cmd_args, const AppSettings& app_settings,
                     FileIndex& file_index, AppBootstrapResult& result);

void TryStartUsnMonitor(const CommandLineArgs& cmd_args, const AppSettings& app_settings,
                        FileIndex& file_index, AppBootstrapResult& result) {
  try {
    StartUsnMonitor(cmd_args, app_settings, file_index, result);
  } catch (
    const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all for std::exception needed:
                                // UsnMonitor construction/Start can throw various exception types
    (void)e;                    // Suppress unused variable warning in Release mode
    LOG_ERROR_BUILD("Exception while starting USN monitor: "
                    << e.what() << ". Continuing without USN monitoring.");
    // Continue without USN monitoring - user can use folder crawling instead
    result.monitor.reset();
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for non-standard exceptions from Windows APIs
    LOG_ERROR("Unknown exception while starting USN monitor. Continuing without USN monitoring.");
    // Continue without USN monitoring - user can use folder crawling instead
    result.monitor.reset();
  }
}

void StartUsnMonitor(const CommandLineArgs& cmd_args, const AppSettings& app_settings,
                     FileIndex& file_index, AppBootstrapResult& result) {
  if (!cmd_args.index_from_file.empty()) {
    return;  // Skip monitoring when indexing from file
  }

  if (app_settings.monitoredVolume.empty()) {
    LOG_WARNING_BUILD("No monitoredVolume configured; skipping USN monitoring.");
    return;
  }

  if (!IsNtfsVolume(app_settings.monitoredVolume)) {
    return;
  }

  MonitoringConfig monitor_config{ToWindowsVolumePath(app_settings.monitoredVolume)};
  result.monitor = std::make_unique<UsnMonitor>(file_index, monitor_config);
  if (!result.monitor->Start()) {
    // SECURITY: Check if this was a privilege dropping failure (critical security issue)
    if (result.monitor->DidPrivilegeDropFail()) {
      LOG_ERROR("SECURITY: Failed to drop privileges after acquiring volume handle. "
                "Continuing with elevated privileges is a security risk. Application will exit.");
      result.security_fatal_error = true;
      return;
    }
    LOG_ERROR("Failed to start USN monitoring - real-time file system monitoring disabled");
  }
}

void CleanupOnException(AppBootstrapResult& result) {
  AppBootstrapCommon::CleanupImGuiAndGlfw(result);
  if (result.com_initialized) {
    OleUninitialize();
    result.com_initialized = false;
  }
}

// Helper function to handle initialization exceptions consistently
AppBootstrapResult HandleInitializationException(std::string_view error_message,
                                                 AppBootstrapResult& result) {
  LOG_ERROR_BUILD(error_message);
  CleanupOnException(result);
  // Use move semantics because AppBootstrapResultBase disables copy
  return std::move(result);
}
}  // anonymous namespace

namespace AppBootstrap {

AppBootstrapResult Initialize(const CommandLineArgs& cmd_args, FileIndex& file_index,
                              int& last_window_width, int& last_window_height) {
  // Implementation details:
  // 1. Initialize GLFW with error callback
  // 2. Log CPU information (Debug and Release builds)
  // 3. Initialize COM/OLE for Shell APIs
  // 4. Check admin privileges (restart if needed)
  // 5. Load index from file (if requested)
  // 6. Load settings and apply command-line overrides
  // 7. Create GLFW window
  // 8. Initialize DirectX manager
  // 9. Setup window resize callback
  // 10. Initialize ImGui context and backends
  // 11. Apply font settings
  // 12. Start UsnMonitor (if not indexing from file)
  AppBootstrapResult result;
  result.file_index = &file_index;
  result.last_window_width = &last_window_width;
  result.last_window_height = &last_window_height;
  result.platform_exit_hook = [](const char* reason) { OnPlatformExit(reason); };

  LOG_INFO("Application starting");

  // Initialize GLFW (handles DPI awareness automatically)
  AppBootstrapCommon::SetupGlfwErrorCallback();

  if (!glfwInit()) {
    LOG_ERROR("Failed to initialize GLFW");
    return result;
  }

  AppBootstrapCommon::LogCpuInformation();
  InitializeCom(result);

  // Check for admin privileges and handle indexing
  if (!IsProcessElevated()) {
    result.running_without_elevation = true;
    if (!HandleIndexingWithoutAdmin(cmd_args, file_index)) {
      return result;
    }
  }

  // Load index from file if specified
  if (!AppBootstrapCommon::LoadIndexFromFile(cmd_args.index_from_file, file_index)) {
    return result;
  }

  // Load application settings and initialize graphics subsystems.
  // Wrap in try-catch to handle exceptions gracefully.
  try {
    // Static variables are safe here because Initialize() is only called once per
    // application lifecycle (from main_common.h). These are single-instance objects
    // that persist for the application's lifetime.
    static AppSettings app_settings;
    LoadSettings(app_settings);
    AppBootstrapCommon::ApplyCommandLineOverrides(cmd_args, app_settings);

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    GLFWwindow* window = AppBootstrapCommon::CreateGlfwWindow(app_settings, result, APP_DISPLAY_NAME);
    if (window == nullptr) {
      return result;
    }

    static DirectXManager direct_x_manager;
    result.renderer = &direct_x_manager;

    if (!InitializeDirectXAndWindow(window, direct_x_manager, last_window_width, last_window_height,
                                    app_settings)) {
      return result;
    }

    if (!InitializeImGuiBackend(window, direct_x_manager)) {
      return result;
    }

    {
      const ImGuiIO& io = ImGui::GetIO();
      const bool viewports_enabled =
          (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0;
      ui::Theme::Apply(app_settings.themeId, viewports_enabled);
    }

    ApplyFontSettings(app_settings);
    LOG_INFO("ImGui initialized successfully");

    // Show window immediately so UI is visible while monitor initializes
    glfwShowWindow(window);

    // Only start USN monitor if running with elevated privileges
    // When running without elevation, the user will select a folder from Settings UI
    if (!result.running_without_elevation) {
      TryStartUsnMonitor(cmd_args, app_settings, file_index, result);
    } else {
      LOG_INFO("Skipping USN monitor - running without elevation. "
               "User can select a folder from Settings > Index Configuration.");
    }
    result.settings = &app_settings;
  } catch (const std::bad_alloc& e) {
    return HandleInitializationException(AppBootstrapCommon::BuildBadAllocErrorMessage(e), result);
  } catch (const std::runtime_error& e) {  // NOSONAR(cpp:S1181) - Generic catch needed:
                                           // initialization code may throw various runtime errors
    return HandleInitializationException(AppBootstrapCommon::BuildRuntimeErrorMessage(e), result);
  } catch (...) {  // NOSONAR(cpp:S2738) - Catch-all needed for initialization: must handle all
                   // exceptions including non-standard ones
    return HandleInitializationException(AppBootstrapCommon::GetUnknownExceptionMessage(), result);
  }

  return result;
}

void Cleanup(AppBootstrapResult& result) {
  // Cleanup order (reverse of initialization):
  // 1. Stop UsnMonitor (platform-specific)
  // 2. Common ImGui/GLFW cleanup (via AppBootstrapCommon)
  // 3. Uninitialize COM/OLE (platform-specific)

  // Stop monitoring explicitly before shutdown
  if (result.monitor) {
    result.monitor->Stop();
    result.monitor.reset();
  }

  // Common ImGui and GLFW cleanup
  AppBootstrapCommon::CleanupImGuiAndGlfw(result);

  // Platform-specific cleanup: Uninitialize COM/OLE
  if (result.com_initialized) {
    OleUninitialize();
  }
}

}  // namespace AppBootstrap

#endif  // _WIN32
