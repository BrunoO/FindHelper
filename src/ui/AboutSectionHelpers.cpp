/**
 * @file ui/AboutSectionHelpers.cpp
 * @brief Implementation of Help window About section helpers (build, platform, system).
 */

#include "ui/AboutSectionHelpers.h"

#include "core/Version.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only for PGO detection
#endif  // _WIN32

namespace ui {

const char* GetAboutAppDisplayName() {
  return APP_DISPLAY_NAME_STR;
}

const char* GetAboutAppVersion() {
  return APP_VERSION;
}

const char* GetAboutHelpWindowTitle() {
  return HELP_WINDOW_TITLE_STR;
}

const char* GetAboutBuildTypeLabel() {
#ifdef NDEBUG
#ifdef FAST_LIBS_BOOST
  return "(Release, Boost)";
#else
  return "(Release)";
#endif  // FAST_LIBS_BOOST
#else
#ifdef FAST_LIBS_BOOST
  return "(Debug, Boost)";
#else
  return "(Debug)";
#endif  // FAST_LIBS_BOOST
#endif  // NDEBUG
}

namespace {

// Returns 'G' for GENPROFILE, 'U' for USEPROFILE, or '\0' for none (Windows only).
char GetPgoModeImpl() {
#ifdef _WIN32
  const HMODULE pgort_handle = []() {
    HMODULE handle = GetModuleHandleA("pgort140.dll");
    if (handle == nullptr) {
      handle = GetModuleHandleA("pgort.dll");
    }
    return handle;
  }();
  if (pgort_handle != nullptr) {
#ifdef _GENPROFILE
    return 'G';
#elif defined(_USEPROFILE)
    return 'U';
#else
    return 'G';
#endif  // _GENPROFILE
  }
#ifdef _GENPROFILE
  return 'G';
#elif defined(_USEPROFILE)
  return 'U';
#endif  // _GENPROFILE
#endif  // _WIN32
  return '\0';
}

}  // namespace

char GetAboutPgoMode() {
  return GetPgoModeImpl();
}

const char* GetAboutPgoTooltip(char pgo_mode) {
  if (pgo_mode == 'G') {
    return "PGO: GENPROFILE (Instrumented build)";
  }
  if (pgo_mode == 'U') {
    return "PGO: USEPROFILE (Optimized build)";
  }
  return nullptr;
}

const char* GetAboutPlatformMonitoringLabel() {
#if defined(_WIN32)
  return "Windows (No Monitoring)";
#elif defined(__APPLE__)
  return "macOS (No Monitoring)";
#else
  return "Linux (No Monitoring)";
#endif  // _WIN32 / __APPLE__
}

size_t GetAboutLogicalProcessorCount() {
  return GetLogicalProcessorCount();
}

std::string GetAboutProcessMemoryDisplay() {
  const size_t memory_bytes = Logger::Instance().GetPrivateMemoryBytes();
  return GetAboutProcessMemoryDisplayFromBytes(memory_bytes);
}

std::string GetAboutProcessMemoryDisplayFromBytes(size_t memory_bytes) {
  return FormatMemoryOrNa(memory_bytes);
}

}  // namespace ui
