/**
 * @file ui/AboutSectionHelpers.cpp
 * @brief Implementation of Help window About section helpers (build, platform, system).
 */

#include "ui/AboutSectionHelpers.h"

#include "core/AppIdentity.h"
#include "core/Version.h"
#include "utils/Logger.h"
#include "utils/StringUtils.h"
#include "utils/ThreadUtils.h"

#include "imgui.h"

#ifdef FAST_LIBS_BOOST
#include <boost/version.hpp>  // NOSONAR - Header-only: BOOST_VERSION version number
#endif  // FAST_LIBS_BOOST

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

const char* GetAboutAllocatorLabel() {
  return "system allocator";
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

void RenderPgoTooltipIfHovered(char pgo_mode) {
  if (!ImGui::IsItemHovered()) {
    return;
  }
  if (const char* tooltip = GetAboutPgoTooltip(pgo_mode); tooltip != nullptr) {
    ImGui::BeginTooltip();
    ImGui::TextUnformatted(tooltip);
    ImGui::EndTooltip();
  }
}

const char* GetAboutPlatformShortLabel() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__APPLE__)
  return "macOS";
#else
  return "Linux";
#endif  // _WIN32 / __APPLE__
}

const char* GetAboutPlatformMonitoringLabel([[maybe_unused]] bool is_monitoring_active) {
#if defined(_WIN32)
  return is_monitoring_active ? "Windows (Monitoring Active)" : "Windows (No Monitoring)";
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

const char* GetAboutRegexEnginesLabel() {
  // Literal and simple patterns are always handled by string search.
  // Complex patterns use either Boost.Regex (FAST_LIBS_BOOST) or std::regex.
  // When RE2 is integrated, prepend "RE2, " for the primary engine and adjust fallbacks here.
#ifdef FAST_LIBS_BOOST
  return "String search, Boost.Regex";
#else
  return "String search, std::regex";
#endif  // FAST_LIBS_BOOST
}

std::string GetAboutBoostVersionLabel() {
#ifdef FAST_LIBS_BOOST
  // BOOST_VERSION (boost/version.hpp) is one integer:
  // MAJOR * 100000 + MINOR * 100 + PATCH. Same decomposition CMakeLists.txt uses
  // when it parses version.hpp for the BOOST_ROOT fallback, so both agree.
  constexpr int kVersionNumber = BOOST_VERSION;
  constexpr int kMajor = kVersionNumber / 100000;
  constexpr int kMinor = (kVersionNumber / 100) % 1000;
  constexpr int kPatch = kVersionNumber % 100;
  static const std::string kLabel =
      std::to_string(kMajor) + "." + std::to_string(kMinor) + "." + std::to_string(kPatch);
  return kLabel;
#else
  return {};
#endif  // FAST_LIBS_BOOST
}

}  // namespace ui
