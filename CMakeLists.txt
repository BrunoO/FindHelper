cmake_minimum_required(VERSION 3.19)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
# CMake 3.30+: find_package(Boost) uses BoostConfig.cmake, not the removed FindBoost module.
if(POLICY CMP0167)
    cmake_policy(SET CMP0167 NEW)
endif()

# RC is Windows-only; OBJCXX is Apple-only. WIN32/APPLE are known on the host before project().
if(WIN32)
    project(FindHelper LANGUAGES CXX RC)
elseif(APPLE)
    project(FindHelper LANGUAGES CXX OBJCXX)
else()
    project(FindHelper LANGUAGES CXX)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)  # Force -std=c++17, not -std=gnu++17; required for MSVC portability

# Set MSVC runtime library to avoid LNK4098 warnings
# Use MultiThreaded DLL (/MD) for all configurations (including Debug)
# This ensures all targets (including subdirectories like FreeType) use the same CRT
if(MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreadedDLL")
    message(STATUS "MSVC Runtime Library: ${CMAKE_MSVC_RUNTIME_LIBRARY}")
    # /MP: parallel compilation; /Zc:__cplusplus: correct __cplusplus macro; /Zc:inline: drop unreferenced COMDATs
    add_compile_options(/MP /Zc:__cplusplus /Zc:inline)
endif()

# Common Windows defines (avoid duplication in every target)
# Target Windows 7 (0x0601) as minimum supported OS to prevent linking APIs introduced in Windows 8+ (e.g. CreateFile2)
if(WIN32)
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE _WIN32_WINNT=0x0601 WINVER=0x0601)
endif()

# Export compile commands for clang-tidy and other tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# clang-tidy target (per AGENTS.md: cmake --build . --target clang-tidy)
find_program(RUN_CLANG_TIDY_EXE NAMES run-clang-tidy run-clang-tidy.py PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
find_program(CLANG_TIDY_EXE NAMES clang-tidy PATHS /opt/homebrew/opt/llvm/bin /usr/local/opt/llvm/bin)
set(_clang_tidy_extra_args "")
if(APPLE)
    if(CMAKE_OSX_SYSROOT)
        set(_clang_tidy_extra_args "-extra-arg=-isysroot${CMAKE_OSX_SYSROOT}")
    else()
        execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE _osx_sdk_path OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(_osx_sdk_path)
            set(_clang_tidy_extra_args "-extra-arg=-isysroot${_osx_sdk_path}")
        endif()
    endif()
endif()

if(RUN_CLANG_TIDY_EXE)
    add_custom_target(clang-tidy
        COMMAND "${RUN_CLANG_TIDY_EXE}"
            -p "${CMAKE_BINARY_DIR}"
            ${_clang_tidy_extra_args}
            "^${CMAKE_SOURCE_DIR}/src/"
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy on src/..."
        USES_TERMINAL
        VERBATIM
    )
elseif(CLANG_TIDY_EXE)
    file(GLOB_RECURSE _clang_tidy_srcs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
    # Only lint sources the current platform compiles. The compile DB has no
    # entry for other-platform TUs, so clang-tidy would parse them without
    # flags (no windows.h / wrong SDK) and fail the target. Ground truth for
    # the unsuffixed Windows-only set: files absent from the macOS
    # compile_commands.json (UsnMonitor.cpp, InitialIndexPopulator.cpp, ...).
    if(APPLE)
        list(FILTER _clang_tidy_srcs EXCLUDE REGEX ".*/platform/(windows|linux)/.*")
        list(FILTER _clang_tidy_srcs EXCLUDE REGEX ".*_(win|linux)\\.cpp$")
        list(REMOVE_ITEM _clang_tidy_srcs
            "${CMAKE_SOURCE_DIR}/src/usn/UsnMonitor.cpp"
            "${CMAKE_SOURCE_DIR}/src/usn/UsnRecordUtils.cpp"
            "${CMAKE_SOURCE_DIR}/src/usn/WindowsIndexBuilder.cpp"
            "${CMAKE_SOURCE_DIR}/src/index/InitialIndexPopulator.cpp"
            "${CMAKE_SOURCE_DIR}/src/index/mft/MftMetadataReader.cpp"
            "${CMAKE_SOURCE_DIR}/src/utils/LoggingUtils.cpp")
    elseif(UNIX)
        list(FILTER _clang_tidy_srcs EXCLUDE REGEX ".*/platform/(windows|macos)/.*")
        list(FILTER _clang_tidy_srcs EXCLUDE REGEX ".*_(win|mac)\\.cpp$")
        list(REMOVE_ITEM _clang_tidy_srcs
            "${CMAKE_SOURCE_DIR}/src/usn/UsnMonitor.cpp"
            "${CMAKE_SOURCE_DIR}/src/usn/UsnRecordUtils.cpp"
            "${CMAKE_SOURCE_DIR}/src/usn/WindowsIndexBuilder.cpp"
            "${CMAKE_SOURCE_DIR}/src/index/InitialIndexPopulator.cpp"
            "${CMAKE_SOURCE_DIR}/src/index/mft/MftMetadataReader.cpp"
            "${CMAKE_SOURCE_DIR}/src/utils/LoggingUtils.cpp")
    elseif(WIN32)
        list(FILTER _clang_tidy_srcs EXCLUDE REGEX ".*/platform/(linux|macos)/.*")
        list(FILTER _clang_tidy_srcs EXCLUDE REGEX ".*_(linux|mac)\\.cpp$")
    endif()
    add_custom_target(clang-tidy
        COMMAND "${CLANG_TIDY_EXE}"
            -p "${CMAKE_BINARY_DIR}"
            ${_clang_tidy_extra_args}
            ${_clang_tidy_srcs}
        WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
        COMMENT "Running clang-tidy on src/..."
        USES_TERMINAL
        VERBATIM
    )
else()
    message(STATUS "clang-tidy: neither run-clang-tidy nor clang-tidy found; 'clang-tidy' target not defined")
endif()

# Multi-config generators (Visual Studio, Xcode) ignore CMAKE_BUILD_TYPE for the actual
# compile; binaries go under build/<Config>/ and you must pass --config / -C explicitly.
# Setting -DCMAKE_BUILD_TYPE=Debug at configure time does NOT produce build/Debug.
if(CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Multi-config generator (${CMAKE_GENERATOR}): use cmake --build . --config Debug|Release")
    message(STATUS "  Outputs: ${CMAKE_BINARY_DIR}/<Config>/  (e.g. Debug, Release)")
    if(CMAKE_BUILD_TYPE)
        message(STATUS "  Note: CMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} is ignored by this generator")
    endif()
elseif(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Choose the type of build." FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release RelWithDebInfo MinSizeRel)
    message(STATUS "Single-config generator (${CMAKE_GENERATOR}): CMAKE_BUILD_TYPE defaulted to RelWithDebInfo")
endif()

# --- Compiler Caching ---
# MSVC: sccache (ccache does not support MSVC). macOS/Linux: ccache.
if(MSVC)
    if(ENABLE_PGO)
        message(STATUS "sccache disabled: ENABLE_PGO=ON (sccache is incompatible with MSVC /GL and PGO)")
    else()
        find_program(SCCACHE_PROGRAM sccache)
        if(SCCACHE_PROGRAM)
            set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
            set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
            message(STATUS "sccache found: ${SCCACHE_PROGRAM} - compiler caching enabled")
        else()
            message(STATUS "sccache not found - install from https://github.com/mozilla/sccache for faster MSVC rebuilds")
        endif()
    endif()
else()
    find_program(CCACHE_PROGRAM ccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "ccache found: ${CCACHE_PROGRAM} - compiler caching enabled")
    else()
        message(STATUS "ccache not found - install with 'brew install ccache' (macOS) or 'apt-get install ccache' (Linux) for faster rebuilds")
    endif()
endif()

# MSVC standards-conformance flags for project-owned targets (not third-party submodules).
function(apply_msvc_project_compile_options target_name)
    if(MSVC)
        target_compile_options(${target_name} PRIVATE
            /permissive-
            /W3
            /Zc:preprocessor
            /utf-8
        )
    endif()
endfunction()

# --- Project options (cache variables; keep in one block) ---
option(BUILD_APPLICATION "Build the main application" ON)
option(ENABLE_IMGUI_TEST_ENGINE "Build with Dear ImGui Test Engine for in-process UI tests (macOS)" OFF)
# Crash handler: compile-time only.
option(IMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER "Install test engine default crash handler (disable for CI or custom handler)" ON)
# FAST_LIBS_BOOST: swap std::unordered_map for boost::unordered_flat_map (open-addressing, cache-friendly)
option(FAST_LIBS_BOOST "Use Boost unordered containers for faster hash maps (requires Boost 1.81+)" OFF)
# ENABLE_PGO: two-pass Profile-Guided Optimization (MSVC only; ignored on other compilers)
option(ENABLE_PGO "Enable Profile-Guided Optimization (requires two build passes)" OFF)
# ENABLE_CTRACK_PROFILING: keep ctrack instrumentation active. OFF (shipping builds) defines
# CTRACK_DISABLE so all CTRACK* macros compile out — per CTRACK_PROFILING_GUIDE.md best practice #5.
option(ENABLE_CTRACK_PROFILING "Keep ctrack instrumentation active (OFF for shipping binaries)" ON)
# auto: Phase 2 if FindHelper.pgd is found next to the binary (or in Release/); otherwise Phase 1.
set(PGO_PHASE "auto" CACHE STRING "PGO phase: auto, instrument, or optimize")
set_property(CACHE PGO_PHASE PROPERTY STRINGS auto instrument optimize)

option(BUILD_TESTS "Build unit tests" ON)
# ASan is ON by default for tests - catches memory bugs like use-after-free
option(ENABLE_ASAN "Enable Address Sanitizer for tests (GCC/Clang only)" ON)
# TSAN (Thread Sanitizer) is OFF by default - catches data races; macOS/Linux only (not supported on Windows). Mutually exclusive with ASAN.
option(ENABLE_TSAN "Enable Thread Sanitizer for tests (macOS/Linux, Clang/GCC only)" OFF)
# Code coverage is OFF by default
option(ENABLE_COVERAGE "Enable code coverage instrumentation (Clang/GCC only)" OFF)
# When ENABLE_COVERAGE and APPLE, also instrument find_helper for ImGui test coverage.
option(COVERAGE_INSTRUMENT_FIND_HELPER "Instrument find_helper for coverage on macOS when ENABLE_COVERAGE is ON (ImGui tests)" ON)
option(ENABLE_COVERAGE_ALL_SOURCES "Include all source files in coverage report" OFF)
option(TEST_VERBOSE_OUTPUT "Show detailed test output (test names, durations, etc.)" OFF)
option(TEST_EXTRA_VERBOSE "Show extra verbose test output (all test details)" OFF)

# Test engine is macOS-only; force-disable on other platforms
if(ENABLE_IMGUI_TEST_ENGINE AND NOT APPLE)
    message(WARNING "ImGui Test Engine is supported only on macOS; disabling ENABLE_IMGUI_TEST_ENGINE for this build.")
    set(ENABLE_IMGUI_TEST_ENGINE OFF CACHE BOOL "Build with Dear ImGui Test Engine for in-process UI tests (macOS)" FORCE)
endif()

# ASan and TSAN are mutually exclusive (cannot link both runtimes).
if(ENABLE_TSAN AND ENABLE_ASAN)
    set(ENABLE_ASAN OFF CACHE BOOL "Disabled because ENABLE_TSAN is ON (mutually exclusive)" FORCE)
    message(STATUS "  TSAN and ASAN are mutually exclusive; ASAN disabled because TSAN is enabled")
endif()

# Enable Release LTO via CMake IPO on a single target (do not set this globally:
# it would apply to FreeType and tests). Clang's default IPO is thin LTO; override
# CMAKE_*_COMPILE_OPTIONS_IPO to preserve -flto=full (Apple) and -flto=auto (Linux).
function(enable_release_ipo target_name)
    if(APPLE)
        set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto=full")
        set(CMAKE_C_LINK_OPTIONS_IPO "-flto=full")
        set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto=full")
        set(CMAKE_CXX_LINK_OPTIONS_IPO "-flto=full")
        set(CMAKE_OBJCXX_COMPILE_OPTIONS_IPO "-flto=full")
        set(CMAKE_OBJCXX_LINK_OPTIONS_IPO "-flto=full")
        set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto=full" PARENT_SCOPE)
        set(CMAKE_C_LINK_OPTIONS_IPO "-flto=full" PARENT_SCOPE)
        set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto=full" PARENT_SCOPE)
        set(CMAKE_CXX_LINK_OPTIONS_IPO "-flto=full" PARENT_SCOPE)
        set(CMAKE_OBJCXX_COMPILE_OPTIONS_IPO "-flto=full" PARENT_SCOPE)
        set(CMAKE_OBJCXX_LINK_OPTIONS_IPO "-flto=full" PARENT_SCOPE)
    elseif(UNIX)
        set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto=auto")
        set(CMAKE_C_LINK_OPTIONS_IPO "-flto=auto")
        set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto=auto")
        set(CMAKE_CXX_LINK_OPTIONS_IPO "-flto=auto")
        set(CMAKE_C_COMPILE_OPTIONS_IPO "-flto=auto" PARENT_SCOPE)
        set(CMAKE_C_LINK_OPTIONS_IPO "-flto=auto" PARENT_SCOPE)
        set(CMAKE_CXX_COMPILE_OPTIONS_IPO "-flto=auto" PARENT_SCOPE)
        set(CMAKE_CXX_LINK_OPTIONS_IPO "-flto=auto" PARENT_SCOPE)
    endif()

    include(CheckIPOSupported)
    check_ipo_supported(RESULT _ipo_supported OUTPUT _ipo_err LANGUAGES CXX)
    if(_ipo_supported)
        set_property(TARGET ${target_name} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        if(MSVC)
            message(STATUS "MSVC: WPO/LTCG enabled via CMake IPO (correct MSBuild property path)")
        elseif(APPLE)
            message(STATUS "macOS: Release LTO enabled via CMake IPO (-flto=full)")
        else()
            message(STATUS "Linux: Release LTO enabled via CMake IPO (-flto=auto)")
        endif()
    else()
        message(WARNING "IPO not supported (${_ipo_err}); falling back to explicit LTO flags")
        if(MSVC)
            target_compile_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:/GL>
            )
            target_link_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:/LTCG>
            )
        elseif(APPLE)
            target_compile_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:-flto=full>
            )
            target_link_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:-flto=full>
            )
        elseif(UNIX)
            target_compile_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:-flto=auto>
            )
            target_link_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:-flto=auto>
            )
        endif()
    endif()
endfunction()

# --- Version Generation ---
set(VERSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/GenerateVersion.cmake")
file(WRITE "${VERSION_SCRIPT}" "
# CMake script to generate GitVersion.h
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND \${GIT_EXECUTABLE} rev-parse --short HEAD
        WORKING_DIRECTORY \"${CMAKE_CURRENT_SOURCE_DIR}\"
        OUTPUT_VARIABLE GIT_DESCRIBE_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()

if(NOT GIT_DESCRIBE_OUTPUT)
    set(GIT_DESCRIBE_OUTPUT \"unknown\")
endif()

set(VERSION_CONTENT \"// Auto-generated by CMake build\\n#define APP_VERSION \\\"\${GIT_DESCRIBE_OUTPUT}\\\"\\n\")

if(EXISTS \"${CMAKE_CURRENT_BINARY_DIR}/GitVersion.h\")
    file(READ \"${CMAKE_CURRENT_BINARY_DIR}/GitVersion.h\" OLD_CONTENT)
else()
    set(OLD_CONTENT \"\")
endif()

if(NOT \"\${VERSION_CONTENT}\" STREQUAL \"\${OLD_CONTENT}\")
    file(WRITE \"${CMAKE_CURRENT_BINARY_DIR}/GitVersion.h\" \"\${VERSION_CONTENT}\")
endif()
")

add_custom_target(GenerateVersion ALL
    COMMAND ${CMAKE_COMMAND} -P "${VERSION_SCRIPT}"
    COMMENT "Updating GitVersion.h..."
    VERBATIM
)

# --- FreeType Setup (only needed for main application) ---
if(BUILD_APPLICATION)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/freetype/CMakeLists.txt")
        message(FATAL_ERROR "FreeType submodule not found. Please run: git submodule update --init --recursive")
    endif()
    # Hermetic vendored build: no system HarfBuzz/PNG/bzip2/brotli, bundled zlib, no install rules.
    set(SKIP_INSTALL_ALL TRUE)
    set(FT_DISABLE_ZLIB ON CACHE BOOL "Vendored FreeType: no zlib" FORCE)
    set(FT_DISABLE_BZIP2 ON CACHE BOOL "Vendored FreeType: no bzip2" FORCE)
    set(FT_DISABLE_PNG ON CACHE BOOL "Vendored FreeType: no PNG" FORCE)
    set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "Vendored FreeType: no HarfBuzz" FORCE)
    set(FT_DISABLE_BROTLI ON CACHE BOOL "Vendored FreeType: no Brotli/WOFF2" FORCE)
    add_subdirectory(external/freetype)
endif()

# --- ImGui (set once; platform backend added per-platform below) ---
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui")
set(IMGUI_SOURCES_COMMON
    ${IMGUI_DIR}/imgui.cpp
    ${IMGUI_DIR}/imgui_draw.cpp
    ${IMGUI_DIR}/imgui_tables.cpp
    ${IMGUI_DIR}/imgui_widgets.cpp
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/misc/freetype/imgui_freetype.cpp
)

# --- Platform-specific variables (set once, reused everywhere) ---
if(WIN32)
    set(GEMINI_API_HTTP_SRC src/api/GeminiApiHttp_win.cpp)
    set(PLATFORM_THREAD_UTILS_SRC src/platform/windows/ThreadUtils_win.cpp)
    set(PLATFORM_STRING_UTILS_SRC src/platform/windows/StringUtils_win.cpp)
elseif(APPLE)
    set(GEMINI_API_HTTP_SRC src/api/GeminiApiHttp_mac.mm)
    set(PLATFORM_THREAD_UTILS_SRC src/platform/macos/ThreadUtils_mac.cpp)
    set(PLATFORM_STRING_UTILS_SRC src/platform/posix/StringUtils_posix.cpp)
elseif(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    find_package(CURL QUIET)
    if(CURL_FOUND)
        set(GEMINI_API_HTTP_SRC src/api/GeminiApiHttp_linux.cpp)
    else()
        set(GEMINI_API_HTTP_SRC src/api/GeminiApiHttp_stub.cpp)
    endif()
    set(PLATFORM_THREAD_UTILS_SRC src/platform/linux/ThreadUtils_linux.cpp)
    set(PLATFORM_STRING_UTILS_SRC src/platform/posix/StringUtils_posix.cpp)
endif()

# --- ctrack setup via FetchContent ---
set(_fh_saved_build_tests "${BUILD_TESTS}")
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    ctrack
    GIT_REPOSITORY https://github.com/Compaile/ctrack.git
    GIT_TAG        v1.1.0
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(ctrack)

set(BUILD_TESTS "${_fh_saved_build_tests}" CACHE BOOL "Build unit tests" FORCE)
unset(_fh_saved_build_tests)

if(NOT MSVC)
    add_compile_definitions(CTRACK_DISABLE_EXECUTION_POLICY)
endif()

# Shipping binaries: compile all CTRACK* macros out (zero overhead, no event accumulation).
# Profiling builds (default ON) keep instrumentation; pass -DENABLE_CTRACK_PROFILING=OFF for release packaging.
if(NOT ENABLE_CTRACK_PROFILING)
    add_compile_definitions(CTRACK_DISABLE)
endif()

# --- Shared utility sources (cross-platform, reused in app and test targets) ---
set(SHARED_UTIL_SOURCES
    src/utils/ClipboardUtils.cpp
    src/utils/ExceptionHandling.cpp
    src/utils/RegexGeneratorUtils.cpp
)
# Subset for test targets that need ExceptionHandling but not ClipboardUtils/SystemIdleDetector
# (those require GLFW/platform libs)
set(SHARED_UTIL_SOURCES_FOR_TESTS
    src/utils/ExceptionHandling.cpp
)

# --- GLFW detection (non-Windows; deduplicates macOS and Linux) ---
# Always produces a `glfw` CMake target (find_package, pkg-config alias, or FetchContent).
function(find_or_fetch_glfw)
    if(TARGET glfw)
        return()
    endif()

    find_package(glfw3 3.3 QUIET)
    if(glfw3_FOUND)
        message(STATUS "Found system GLFW via find_package")
        if(TARGET glfw)
            return()
        endif()
        if(TARGET glfw3::glfw)
            add_library(glfw ALIAS glfw3::glfw)
            return()
        endif()
        if(TARGET glfw3)
            add_library(glfw ALIAS glfw3)
            return()
        endif()
        message(FATAL_ERROR "find_package(glfw3) succeeded but did not create a glfw/glfw3 target")
    endif()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(GLFW3 QUIET IMPORTED_TARGET glfw3>=3.3)
        if(TARGET PkgConfig::GLFW3)
            message(STATUS "Found system GLFW via pkg-config: ${GLFW3_VERSION}")
            add_library(glfw ALIAS PkgConfig::GLFW3)
            return()
        endif()
    endif()

    message(STATUS "System GLFW 3.3+ not found - downloading and building GLFW 3.4 via FetchContent")
    message(STATUS "  (This may take a moment on first configure)")
    message(STATUS "  Alternatively, install via: brew install glfw")
    include(FetchContent)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
    endif()
    FetchContent_Declare(
        glfw
        GIT_REPOSITORY https://github.com/glfw/glfw.git
        GIT_TAG        3.4
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE
    )
    FetchContent_MakeAvailable(glfw)
    message(STATUS "GLFW 3.4 will be built from source")
endfunction()

function(link_glfw target_name)
    target_link_libraries(${target_name} PRIVATE glfw)
endfunction()

# --- Common App Sources (cross-platform, compiled on all three platforms) ---
set(COMMON_APP_SOURCES
    src/platform/FontUtilsCommon.cpp
    src/platform/EmbeddedFont_RobotoMedium.cpp
    src/platform/EmbeddedFont_Cousine.cpp
    src/platform/EmbeddedFont_Karla.cpp
    src/platform/EmbeddedFont_FontAwesome.cpp
    src/core/Application.cpp
    src/core/ApplicationLogic.cpp
    src/core/CommandLineArgs.cpp
    src/core/Settings.cpp
    src/filters/TimeFilterUtils.cpp
    src/filters/SizeFilterUtils.cpp
    src/search/SearchHistory.cpp
    src/search/SearchResultUtils.cpp
    src/search/SearchResultsService.cpp
    src/search/ExportSearchResultsService.cpp
    src/search/FolderSizeAggregator.cpp
    src/search/SearchWorker.cpp
    src/search/SearchController.cpp
    src/search/ParallelSearchEngine.cpp
    src/search/SearchContext.cpp
    src/search/SearchContextBuilder.cpp
    src/search/SearchThreadPoolManager.cpp
    src/search/SearchStatisticsCollector.cpp
    src/search/SearchThreadPool.cpp
    src/ui/WelcomePanel.cpp
    src/ui/StoppingState.cpp
    src/ui/FilterPanel.cpp
    src/ui/MetricsWindow.cpp
    src/ui/Popups.cpp
    src/ui/ResultsTable.cpp
    src/ui/ResultsTableKeyboard.cpp
    src/ui/IncrementalSearchState.cpp
    src/ui/SearchControls.cpp
    src/ui/SearchInputs.cpp
    src/ui/SearchInputsGeminiHelpers.cpp
    src/ui/AboutSectionHelpers.cpp
    src/ui/HelpWindow.cpp
    src/ui/SearchHelpWindow.cpp
    src/ui/CenteredToolWindow.cpp
    src/ui/SettingsWindow.cpp
    src/ui/RecentChangesWindow.cpp
    src/ui/UIRenderer.cpp
    src/ui/Theme.cpp
    src/ui/FolderBrowser.cpp
    src/ui/StatusBar.cpp
    src/index/FileIndex.cpp
    src/index/FileIndexMaintenance.cpp
    src/index/PathRecomputer.cpp
    src/index/FileIndexStorage.cpp
    src/index/LazyAttributeLoader.cpp
    src/index/IndexFromFilePopulator.cpp
    src/index/RemoveIndexedSubtree.cpp
    src/index/SystemPathFilter.cpp
    src/crawler/IndexOperations.cpp
    src/crawler/FolderCrawler.cpp
    src/crawler/FolderCrawlerIndexBuilder.cpp
    src/path/DirectoryResolver.cpp
    src/path/PathOperations.cpp
    src/path/PathBuilder.cpp
    src/path/PathStorage.cpp
    src/path/PathUtils.cpp
    src/path/PathPatternMatcher.cpp
    src/utils/CpuFeatures.cpp
    src/utils/StringSearchAVX2.cpp
    src/utils/StringSearchNEON.cpp
    src/utils/ThreadPool.cpp
    src/utils/LoadBalancingStrategy.cpp
    src/api/GeminiApiUtils.cpp
    src/gui/GuiState.cpp
    src/gui/ShortcutRegistry.cpp
    ${SHARED_UTIL_SOURCES}
)

if(WIN32)
    set(SYSTEM_STAY_AWAKE_SRC src/platform/windows/SystemStayAwake_win.cpp)
else()
    set(SYSTEM_STAY_AWAKE_SRC src/platform/SystemStayAwake_stub.cpp)
endif()
list(APPEND COMMON_APP_SOURCES ${SYSTEM_STAY_AWAKE_SRC})

# --- FAST_LIBS_BOOST: Boost detection (runs once; applies to app + test targets) ---
# Must run before if(BUILD_APPLICATION) so test-only builds (BUILD_APPLICATION=OFF) also find Boost.
if(FAST_LIBS_BOOST)
    if(POLICY CMP0144)
        cmake_policy(SET CMP0144 NEW)
    endif()
    # Boost 1.70+ ships BoostConfig.cmake. Do not use MODULE: CMake 3.30+ warns
    # (CMP0167) and will remove FindBoost. Header-only extracted trees (Windows CI
    # BOOST_ROOT) often have no config file; fall back to that include root.
    find_package(Boost 1.81.0 CONFIG QUIET)
    if(NOT Boost_FOUND)
        set(_boost_root "")
        if(BOOST_ROOT)
            set(_boost_root "${BOOST_ROOT}")
        elseif(Boost_ROOT)
            set(_boost_root "${Boost_ROOT}")
        elseif(DEFINED ENV{BOOST_ROOT} AND NOT "$ENV{BOOST_ROOT}" STREQUAL "")
            set(_boost_root "$ENV{BOOST_ROOT}")
        endif()
        set(_boost_version_hpp "${_boost_root}/boost/version.hpp")
        if(_boost_root AND EXISTS "${_boost_version_hpp}")
            file(STRINGS "${_boost_version_hpp}" _boost_ver_line REGEX "^#define BOOST_VERSION ")
            string(REGEX MATCH "[0-9]+" _boost_ver_raw "${_boost_ver_line}")
            math(EXPR _boost_major "${_boost_ver_raw} / 100000")
            math(EXPR _boost_minor "${_boost_ver_raw} / 100 % 1000")
            math(EXPR _boost_patch "${_boost_ver_raw} % 100")
            set(Boost_VERSION "${_boost_major}.${_boost_minor}.${_boost_patch}")
            if(Boost_VERSION VERSION_LESS "1.81.0")
                message(FATAL_ERROR "FAST_LIBS_BOOST requires Boost 1.81+, found ${Boost_VERSION} at ${_boost_root}")
            endif()
            set(Boost_INCLUDE_DIRS "${_boost_root}")
            set(Boost_FOUND TRUE)
            message(STATUS "Boost ${Boost_VERSION} (headers from BOOST_ROOT=${_boost_root})")
        endif()
        unset(_boost_root)
        unset(_boost_version_hpp)
        unset(_boost_ver_line)
        unset(_boost_ver_raw)
        unset(_boost_major)
        unset(_boost_minor)
        unset(_boost_patch)
    endif()
    if(NOT Boost_FOUND)
        message(FATAL_ERROR
            "FAST_LIBS_BOOST=ON requires Boost 1.81+.\n"
            "  Install Boost (Homebrew/vcpkg/apt) or set Boost_DIR to the directory\n"
            "  containing BoostConfig.cmake, or BOOST_ROOT to an extracted Boost source tree.")
    endif()
    message(STATUS "FAST_LIBS_BOOST enabled: boost::unordered_map + boost::unordered_flat_map (${Boost_VERSION})")
    if(Boost_INCLUDE_DIRS)
        message(STATUS "  Boost include dirs: ${Boost_INCLUDE_DIRS}")
    endif()
else()
    message(STATUS "FAST_LIBS_BOOST disabled: Using std::unordered_map")
endif()

# Helper: apply Boost define + includes/link to any target (app or test objects)
function(apply_fast_libs_boost target_name)
    if(FAST_LIBS_BOOST AND Boost_FOUND)
        target_compile_definitions(${target_name} PRIVATE FAST_LIBS_BOOST)
        if(TARGET Boost::headers)
            target_link_libraries(${target_name} PRIVATE Boost::headers)
        elseif(TARGET Boost::boost)
            target_link_libraries(${target_name} PRIVATE Boost::boost)
        else()
            target_include_directories(${target_name} PRIVATE ${Boost_INCLUDE_DIRS})
        endif()
    endif()
endfunction()

# --- Platform-Specific Executable ---
if(BUILD_APPLICATION)

if(WIN32)
    set(IMGUI_SOURCES_PLATFORM
        ${IMGUI_DIR}/backends/imgui_impl_dx11.cpp
    )

    # Windows-only application sources
    set(APP_SOURCES_PLATFORM
        src/platform/windows/main_win.cpp
        src/platform/windows/AppBootstrap_win.cpp
        src/platform/windows/CrashHandler_win.cpp
        src/platform/windows/FontUtils_win.cpp
        src/platform/windows/DragDropUtils.cpp
        src/platform/windows/FileOperations_win.cpp
        ${PLATFORM_THREAD_UTILS_SRC}
        ${PLATFORM_STRING_UTILS_SRC}
        src/platform/windows/PrivilegeUtils.cpp
        src/platform/windows/DirectXManager.cpp
        src/platform/windows/ShellContextUtils.cpp
        src/usn/UsnMonitor.cpp
        src/usn/UsnRecordUtils.cpp
        src/usn/WindowsIndexBuilder.cpp
        src/utils/LoggingUtils.cpp
        src/index/InitialIndexPopulator.cpp
        src/index/mft/MftMetadataReader.cpp
        ${GEMINI_API_HTTP_SRC}
    )

    set(APP_RESOURCES
        src/platform/windows/resource.rc
        resources/icons/app_icon.ico
    )

    add_executable(find_helper WIN32
        ${APP_SOURCES_PLATFORM}
        ${COMMON_APP_SOURCES}
        ${IMGUI_SOURCES_COMMON}
        ${IMGUI_SOURCES_PLATFORM}
        ${APP_RESOURCES}
    )
    message(STATUS "Windows executable target 'find_helper' configured (output: FindHelper.exe)")

    # Verify Windows icon file exists
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon.ico")
        message(STATUS "Windows: Application icon found (resources/icons/app_icon.ico)")
    else()
        message(WARNING "Windows: resources/icons/app_icon.ico not found - application icon will not be embedded")
    endif()

    # Windows GLFW: prefer a system/vcpkg package; fall back to pre-built in external/glfw/.
    # find_package creates the 'glfw' imported target when found.
    find_package(glfw3 3.3 QUIET)
    if(NOT glfw3_FOUND)
        set(_glfw_lib "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/lib/glfw3.lib")
        set(_glfw_inc "${CMAKE_CURRENT_SOURCE_DIR}/external/glfw/include")
        if(NOT EXISTS "${_glfw_lib}")
            message(FATAL_ERROR
                "Windows: GLFW not found via find_package(glfw3) and fallback library is "
                "missing at ${_glfw_lib}. Either install GLFW (e.g. vcpkg install glfw3:x64-windows) "
                "or place glfw3.lib in external/glfw/lib/ and headers in external/glfw/include/.")
        endif()
        message(STATUS "Windows: GLFW not found via find_package — using pre-built external/glfw/lib/glfw3.lib")
        add_library(glfw STATIC IMPORTED)
        set_target_properties(glfw PROPERTIES
            IMPORTED_LOCATION "${_glfw_lib}"
            INTERFACE_INCLUDE_DIRECTORIES "${_glfw_inc}"
        )
    else()
        message(STATUS "Windows: Found GLFW via find_package (${glfw3_VERSION})")
    endif()
    target_link_libraries(find_helper PRIVATE
        glfw
        freetype
        d3d11
        d3dcompiler
        dxgi
        dwmapi
        dbghelp
        ole32
        uuid
        shlwapi
        shcore
    )

elseif(APPLE)
    set(IMGUI_SOURCES_PLATFORM
        ${IMGUI_DIR}/backends/imgui_impl_metal.mm
    )

    # ImGui Test Engine (optional; macOS only)
    if(ENABLE_IMGUI_TEST_ENGINE)
        set(IMGUI_TEST_ENGINE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/imgui_test_engine/imgui_test_engine")
        set(IMGUI_TEST_ENGINE_SOURCES
            ${IMGUI_TEST_ENGINE_DIR}/imgui_te_context.cpp
            ${IMGUI_TEST_ENGINE_DIR}/imgui_te_engine.cpp
            ${IMGUI_TEST_ENGINE_DIR}/imgui_te_ui.cpp
            ${IMGUI_TEST_ENGINE_DIR}/imgui_te_utils.cpp
            ${IMGUI_TEST_ENGINE_DIR}/imgui_te_coroutine.cpp
            ${IMGUI_TEST_ENGINE_DIR}/imgui_te_exporters.cpp
            ${IMGUI_TEST_ENGINE_DIR}/imgui_te_perftool.cpp
            ${IMGUI_TEST_ENGINE_DIR}/imgui_capture_tool.cpp
        )
    endif()

    # macOS-only application sources
    set(APP_SOURCES_PLATFORM
        src/platform/macos/main_mac.mm
        src/platform/macos/AppBootstrap_mac.mm
        src/platform/macos/FontUtils_mac.mm
        src/platform/macos/FileOperations_mac.mm
        src/platform/macos/DragDrop_mac.mm
        ${PLATFORM_THREAD_UTILS_SRC}
        ${PLATFORM_STRING_UTILS_SRC}
        src/platform/macos/MetalManager.mm
        ${GEMINI_API_HTTP_SRC}
    )
    if(ENABLE_IMGUI_TEST_ENGINE)
        list(APPEND APP_SOURCES_PLATFORM ${IMGUI_TEST_ENGINE_SOURCES} src/ui/ImGuiTestEngineTests.cpp)
    endif()

    set(MACOS_ICON_FILE "${CMAKE_CURRENT_SOURCE_DIR}/resources/icons/app_icon.icns")

    add_executable(find_helper
        ${APP_SOURCES_PLATFORM}
        ${COMMON_APP_SOURCES}
        ${IMGUI_SOURCES_COMMON}
        ${IMGUI_SOURCES_PLATFORM}
    )

    # Compile Application.cpp as Objective-C++ on macOS (needed for Objective-C headers)
    set_source_files_properties(src/core/Application.cpp PROPERTIES LANGUAGE OBJCXX)

    # Create app bundle with icon
    # Note: MACOSX_BUNDLE_ICON_FILE must be just the filename (not path)
    # because macOS looks for it relative to Contents/Resources/
    set_target_properties(find_helper PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_BUNDLE_NAME "FindHelper"
        MACOSX_BUNDLE_ICON_FILE "app_icon.icns"
    )

    if(EXISTS "${MACOS_ICON_FILE}")
        set_source_files_properties(${MACOS_ICON_FILE} PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
        target_sources(find_helper PRIVATE ${MACOS_ICON_FILE})
        message(STATUS "macOS: App icon configured (resources/icons/app_icon.icns)")
    else()
        message(WARNING "macOS: resources/icons/app_icon.icns not found. Create it from app_icon.ico using iconutil.")
    endif()

    if(ENABLE_IMGUI_TEST_ENGINE)
        target_include_directories(find_helper PRIVATE ${IMGUI_TEST_ENGINE_DIR})
        target_compile_definitions(find_helper PRIVATE ENABLE_IMGUI_TEST_ENGINE)
        if(IMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER)
            target_compile_definitions(find_helper PRIVATE IMGUI_TEST_ENGINE_INSTALL_CRASH_HANDLER)
        endif()
    endif()

    find_or_fetch_glfw()
    link_glfw(find_helper)

    target_link_libraries(find_helper PRIVATE
        freetype
        "-framework Metal"
        "-framework Foundation"
        "-framework QuartzCore"
        "-framework IOKit"
        "-framework Cocoa"
        "-framework CoreVideo"
    )

    # When CMAKE_OSX_SYSROOT is set, add explicit framework search path so system
    # framework headers (Metal, CoreGraphics, CoreText, etc.) are found.
    if(CMAKE_OSX_SYSROOT)
        target_compile_options(find_helper PRIVATE "-F${CMAKE_OSX_SYSROOT}/System/Library/Frameworks")
    endif()

    target_compile_options(find_helper PRIVATE
        -fobjc-arc
        $<$<CONFIG:Debug>:-g -O0 -Winline>
        $<$<CONFIG:Release>:
            -g                      # Debug symbols for profiling (enables dSYM generation)
            -O3                     # Maximum optimization
            -fdata-sections         # Enable dead code elimination
            -ffunction-sections     # Enable dead code elimination
            -funroll-loops          # Loop unrolling for hot loops
            -finline-functions      # Aggressive function inlining
            # Note: -ffast-math removed - causes warnings with nlohmann/json (uses infinity)
            # Note: -fomit-frame-pointer removed - needed for proper stack traces in profiling
        >
    )

    # Enable dSYM generation for Xcode generator
    if(CMAKE_GENERATOR STREQUAL "Xcode")
        set(CMAKE_XCODE_ATTRIBUTE_DEBUG_INFORMATION_FORMAT "dwarf-with-dsym")
        set(CMAKE_XCODE_ATTRIBUTE_GCC_GENERATE_DEBUGGING_SYMBOLS "YES")
    else()
        # Ninja/Unix Makefiles: dsymutil -o writes the bundle next to the .app.
        # Do not use shell redirects (2>&1, || true) here: VERBATIM does not invoke a shell,
        # so those tokens would be passed as extra arguments.
        add_custom_command(TARGET find_helper POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_BUNDLE_DIR:find_helper>.dSYM
            COMMAND dsymutil $<TARGET_FILE:find_helper> -o $<TARGET_BUNDLE_DIR:find_helper>.dSYM
            COMMENT "Generating dSYM bundle for profiling..."
            VERBATIM
        )
        message(STATUS "macOS: dSYM generation enabled (using dsymutil post-build step)")
    endif()

    # Architecture-specific optimizations
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        target_compile_options(find_helper PRIVATE
            $<$<CONFIG:Release>:-mcpu=apple-m1>
        )
        message(STATUS "macOS: Using Apple Silicon optimizations (-mcpu=apple-m1)")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
        # No -march=native: that makes Release binaries non-reproducible and illegal on older Intel CPUs.
        set_property(SOURCE src/utils/StringSearchAVX2.cpp APPEND PROPERTY COMPILE_OPTIONS -mavx2)
        message(STATUS "macOS: Intel x86_64; AVX2 only in StringSearchAVX2.cpp")
    endif()

elseif(UNIX AND NOT APPLE)
    message(STATUS "Configuring for Linux")

    set(IMGUI_SOURCES_PLATFORM
        ${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp
    )

    # Linux-only application sources
    set(APP_SOURCES_PLATFORM
        src/platform/linux/main_linux.cpp
        src/platform/linux/AppBootstrap_linux.cpp
        src/platform/linux/FontUtils_linux.cpp
        src/platform/linux/OpenGLManager.cpp
        src/platform/linux/FileOperations_linux.cpp
        ${PLATFORM_THREAD_UTILS_SRC}
        ${PLATFORM_STRING_UTILS_SRC}
        ${GEMINI_API_HTTP_SRC}
    )

    add_executable(find_helper
        ${APP_SOURCES_PLATFORM}
        ${COMMON_APP_SOURCES}
        ${IMGUI_SOURCES_COMMON}
        ${IMGUI_SOURCES_PLATFORM}
    )

    target_compile_options(find_helper PRIVATE
        $<$<CONFIG:Debug>:-g -O0 -Winline>
        $<$<CONFIG:Release>:
            -O3                     # Maximum optimization
            -fdata-sections         # Enable dead code elimination
            -ffunction-sections     # Enable dead code elimination
            -funroll-loops          # Loop unrolling for hot loops
            -finline-functions      # Aggressive function inlining
            # Note: -ffast-math removed - causes warnings with nlohmann/json (uses infinity)
        >
    )

    # Architecture-specific: AVX2 only in StringSearchAVX2.cpp (runtime-dispatched)
    # Do NOT add -mavx2 or -march=native to the whole target: emits AVX2 in all code
    # and causes "illegal instruction" on CPUs without AVX2 (e.g. older VMs).
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64)")
        set_property(SOURCE src/utils/StringSearchAVX2.cpp APPEND PROPERTY COMPILE_OPTIONS -mavx2)
        message(STATUS "Linux: x86_64 with AVX2 only in StringSearchAVX2.cpp (baseline elsewhere)")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        message(STATUS "Linux: ARM64 architecture detected (no AVX2)")
    endif()

    target_link_options(find_helper PRIVATE
        $<$<CONFIG:Release>:-Wl,--gc-sections>
    )

    find_package(OpenGL REQUIRED)

    if(CURL_FOUND)
        message(STATUS "Linux: libcurl found - Gemini API enabled")
    else()
        message(STATUS "Linux: libcurl NOT found - Gemini API will use stub (no network support)")
    endif()

    find_package(X11 QUIET)
    find_or_fetch_glfw()
    link_glfw(find_helper)

    target_link_libraries(find_helper PRIVATE
        freetype
        OpenGL::GL
        Threads::Threads
    )

    if(CURL_FOUND)
        target_link_libraries(find_helper PRIVATE CURL::libcurl)
    endif()

    # Link stdc++fs for std::filesystem on GCC < 9
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS "9.0")
        target_link_libraries(find_helper PRIVATE stdc++fs)
        message(STATUS "Linux: Linking stdc++fs for std::filesystem support (GCC < 9)")
    else()
        message(STATUS "Linux: std::filesystem is built into libstdc++ (GCC >= 9)")
    endif()

    if(X11_FOUND)
        target_link_libraries(find_helper PRIVATE X11::X11)
    endif()

else()
    message(FATAL_ERROR "Unsupported platform: only WIN32, APPLE, and Linux are configured.")
endif()

# --- Common target configuration (all platforms) ---
# Set the final executable name (FindHelper) for all platforms
set_target_properties(find_helper PROPERTIES OUTPUT_NAME "FindHelper")
target_compile_features(find_helper PRIVATE cxx_std_17)

# Common include directories shared by all platforms
target_include_directories(find_helper PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}            # GitVersion.h must take precedence over source-tree fallback copies
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
    ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
    ${CMAKE_CURRENT_SOURCE_DIR}/external/freetype/include
)

if(TARGET ctrack::ctrack)
    target_link_libraries(find_helper PRIVATE ctrack::ctrack)
elseif(TARGET ctrack)
    target_link_libraries(find_helper PRIVATE ctrack)
endif()

target_compile_definitions(find_helper PRIVATE
    IMGUI_USER_CONFIG="${CMAKE_CURRENT_SOURCE_DIR}/src/imgui_config.h"
)

# Make the executable depend on version generation
add_dependencies(find_helper GenerateVersion)

# Initial generation to ensure file exists for the very first config
execute_process(COMMAND ${CMAKE_COMMAND} -P "${VERSION_SCRIPT}")

# Apply FAST_LIBS_BOOST to the main application target
apply_fast_libs_boost(find_helper)

# Unix Release LTO: CMake IPO (see enable_release_ipo). Keep -Wl,--gc-sections on Linux.
if(NOT MSVC)
    enable_release_ipo(find_helper)
endif()

# --- Compiler Options (MSVC) ---
if(MSVC)
    apply_msvc_project_compile_options(find_helper)

    if(ENABLE_PGO)
        message(STATUS "")
        message(STATUS "=== Profile-Guided Optimization (PGO) Configuration ===")
        message(STATUS "PGO enabled: Will use profile-guided optimization")
        message(STATUS "Current BUILD CONFIGURATION:")
        message(STATUS "  CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
        message(STATUS "  CMAKE_CONFIGURATION_TYPES: ${CMAKE_CONFIGURATION_TYPES}")
        message(STATUS "  Generator: ${CMAKE_GENERATOR}")
        message(STATUS "")
        message(STATUS "IMPORTANT: PGO only works in Release builds.")
        message(STATUS "REQUIRED: Build with explicit Release configuration:")
        message(STATUS "  cmake -S . -B build -DENABLE_PGO=ON -DCMAKE_BUILD_TYPE=Release")
        message(STATUS "  cmake --build build --config Release")
        message(STATUS "")

        # The linker uses $<TARGET_FILE_DIR:find_helper>/FindHelper.pgd (correct at build time).
        # Configure-time phase detection cannot use that genex, so probe the layouts CMake uses:
        #   Visual Studio multi-config → <binaryDir>/Release/
        #   Ninja/single-config        → <binaryDir>/
        # Override with -DPGO_PHASE=instrument or -DPGO_PHASE=optimize.
        set(_pgo_pgd_candidates
            "${CMAKE_CURRENT_BINARY_DIR}/Release/FindHelper.pgd"
            "${CMAKE_CURRENT_BINARY_DIR}/FindHelper.pgd"
        )
        if(CMAKE_BUILD_TYPE)
            list(APPEND _pgo_pgd_candidates
                "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_BUILD_TYPE}/FindHelper.pgd"
            )
        endif()
        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY)
            list(APPEND _pgo_pgd_candidates
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/FindHelper.pgd"
            )
        endif()
        if(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE)
            list(APPEND _pgo_pgd_candidates
                "${CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE}/FindHelper.pgd"
            )
        endif()
        list(REMOVE_DUPLICATES _pgo_pgd_candidates)

        set(PGD_FILE_PATH "")
        foreach(_pgd IN LISTS _pgo_pgd_candidates)
            if(EXISTS "${_pgd}")
                set(PGD_FILE_PATH "${_pgd}")
                break()
            endif()
        endforeach()

        string(TOLOWER "${PGO_PHASE}" _pgo_phase)
        if(_pgo_phase STREQUAL "instrument")
            set(_pgo_use_profile FALSE)
        elseif(_pgo_phase STREQUAL "optimize")
            set(_pgo_use_profile TRUE)
            if(NOT PGD_FILE_PATH)
                message(WARNING
                    "PGO_PHASE=optimize but FindHelper.pgd was not found at configure time. "
                    "The linker still looks next to FindHelper.exe. Searched: ${_pgo_pgd_candidates}")
            endif()
        elseif(_pgo_phase STREQUAL "auto")
            if(PGD_FILE_PATH)
                set(_pgo_use_profile TRUE)
            else()
                set(_pgo_use_profile FALSE)
            endif()
        else()
            message(FATAL_ERROR "PGO_PHASE must be auto, instrument, or optimize (got '${PGO_PHASE}')")
        endif()

        # /GL /Gy is required for both PGO phases (LNK1269 if mixed with non-/GL objects).
        target_compile_options(find_helper PRIVATE
            $<$<CONFIG:Release>:/GL /Gy>
        )

        if(_pgo_use_profile)
            # Phase 2: Optimized build with profile data
            message(STATUS "PGO: Phase 2 - Optimized build with profile data")
            if(PGD_FILE_PATH)
                message(STATUS "  Profile file found: ${PGD_FILE_PATH}")
                file(SIZE "${PGD_FILE_PATH}" PGD_FILE_SIZE)
                if(PGD_FILE_SIZE EQUAL 0)
                    message(WARNING "PGO: Profile file is empty! Re-run training and merge profile data before building Phase 2.")
                else()
                    message(STATUS "  Profile file size: ${PGD_FILE_SIZE} bytes")
                endif()
            endif()

            message(STATUS "  Compiler flags: /GL /Gy")
            message(STATUS "  Linker flags: /LTCG:PGOPTIMIZE /USEPROFILE /PGD:FindHelper.pgd /OPT:REF /OPT:ICF")
            message(STATUS "")

            target_compile_definitions(find_helper PRIVATE
                $<$<CONFIG:Release>:_USEPROFILE>
            )
            target_link_options(find_helper PRIVATE
                $<$<CONFIG:Release>:/LTCG:PGOPTIMIZE /USEPROFILE /PGD:$<TARGET_FILE_DIR:find_helper>/FindHelper.pgd /OPT:REF /OPT:ICF>
            )
        else()
            # Phase 1: Instrumented build
            message(STATUS "PGO: Phase 1 - Instrumented build (profile data collection)")
            message(STATUS "  Compiler flags: /GL /Gy")
            message(STATUS "  Linker flags: /LTCG:PGINSTRUMENT /GENPROFILE /OPT:REF /OPT:ICF")
            message(STATUS "")
            message(STATUS "  Next steps after building:")
            message(STATUS "    1. Build: cmake --build build --config Release")
            message(STATUS "    2. Run application with typical workloads to generate .pgc files")
            message(STATUS "       (from the directory that contains FindHelper.exe)")
            message(STATUS "    3. Merge profile data:")
            message(STATUS "       pgomgr /merge /summary /detail FindHelper*.pgc FindHelper.pgd")
            message(STATUS "    4. Reconfigure CMake (to switch to Phase 2):")
            message(STATUS "       cmake -S . -B build -DENABLE_PGO=ON -DPGO_PHASE=optimize")
            message(STATUS "    5. Rebuild with profile data:")
            message(STATUS "       cmake --build build --config Release")
            message(STATUS "")

            target_compile_definitions(find_helper PRIVATE
                $<$<CONFIG:Release>:_GENPROFILE>
            )
            target_link_options(find_helper PRIVATE
                $<$<CONFIG:Release>:/LTCG:PGINSTRUMENT /GENPROFILE /OPT:REF /OPT:ICF>
            )
        endif()
        unset(_pgo_pgd_candidates)
        unset(_pgo_phase)
        unset(_pgo_use_profile)
        unset(_pgd)

        # Apply PGO flags to FreeType (required to avoid LNK1269 errors)
        # FreeType must be compiled with the same PGO flags as the main executable.
        # Use standard linker flags (not PGO-specific) to avoid LNK1266 errors.
        target_compile_options(freetype PRIVATE
            $<$<CONFIG:Release>:/GL /Gy>
        )
        target_link_options(freetype PRIVATE
            $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
        )
    else()
        # Standard optimized build (no PGO): enable WPO/LTCG via CMake IPO so the
        # Visual Studio generator writes MSBuild WholeProgramOptimization and
        # LinkTimeCodeGeneration. Putting /LTCG in target_link_options lands in
        # AdditionalOptions, which MSBuild applies too late and causes
        # "restarting link with /LTCG" when it first sees /GL objects (e.g. FileIndex.obj).
        enable_release_ipo(find_helper)
    endif()

    # Debug configuration (/Oi /Ot removed: they contradict /Od)
    target_compile_options(find_helper PRIVATE
        $<$<CONFIG:Debug>:/Zi /Ob0 /Od /RTC1 /arch:SSE2>
        # /sdl on non-PGO Release only; PGO instrument/use phases use incompatible link flags (LNK1268).
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${ENABLE_PGO}>>>:/sdl>
    )

    # Enable AVX2 for StringSearchAVX2.cpp only (file-specific)
    set_property(SOURCE src/utils/StringSearchAVX2.cpp APPEND PROPERTY COMPILE_OPTIONS /arch:AVX2)

    # Linker options (common to all builds).
    # /OPT:REF /OPT:ICF are independent of IPO; /LTCG for non-PGO Release is set via
    # INTERPROCEDURAL_OPTIMIZATION_RELEASE (or the IPO-unsupported fallback above).
    # /guard:cf is omitted when ENABLE_PGO is on: incompatible with /GENPROFILE and /USEPROFILE (LNK1268).
    target_link_options(find_helper PRIVATE
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${ENABLE_PGO}>>>:/OPT:REF /OPT:ICF>
        $<$<AND:$<CONFIG:Release>,$<NOT:$<BOOL:${ENABLE_PGO}>>>:/guard:cf>
        $<$<CONFIG:Debug>:/DEBUG>
        /DYNAMICBASE
        /HIGHENTROPYVA
        # Disable automatic manifest generation; we provide our own in resource.rc
        /MANIFEST:NO
        # Use main() as entry point even for WINDOWS subsystem
        /ENTRY:mainCRTStartup
        # Suppress LNK4098 warning about defaultlib 'MSVCRT' conflicts with pre-built libs
        /ignore:4098
    )
endif()

endif() # BUILD_APPLICATION

# --- Unit Tests ---
# ASan helper function
function(apply_asan_to_target target_name)
    if(ENABLE_ASAN AND NOT ENABLE_TSAN AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=address)
    endif()
endfunction()

# TSAN helper function (macOS/Linux only; not supported on Windows - see LLVM ThreadSanitizer docs).
function(apply_tsan_to_target target_name)
    if(ENABLE_TSAN AND NOT MSVC AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
        target_link_options(${target_name} PRIVATE -fsanitize=thread)
    endif()
endfunction()

# Coverage helper function
function(apply_coverage_to_target target_name)
    if(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(${target_name} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
        target_link_options(${target_name} PRIVATE
            -fprofile-instr-generate
            -fcoverage-mapping
        )
    elseif(ENABLE_COVERAGE AND CMAKE_CXX_COMPILER_ID MATCHES "GNU")
        target_compile_options(${target_name} PRIVATE
            --coverage
            -fprofile-arcs
            -ftest-coverage
        )
        target_link_options(${target_name} PRIVATE --coverage)
    endif()
endfunction()

# Apply MSVC /LTCG and optional PGO /GL /Gy to a test target that links /GL object libraries.
# Only add /LTCG when ENABLE_PGO is true (tests get /GL); otherwise the linker warns
# "no code generation required" and suggests removing /LTCG for performance.
function(configure_msvc_ltcg target_name)
    if(MSVC)
        if(ENABLE_PGO)
            target_compile_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:/GL /Gy>
            )
            target_link_options(${target_name} PRIVATE
                $<$<CONFIG:Release>:/LTCG /OPT:REF /OPT:ICF>
            )
        endif()
    endif()
endfunction()

# Internal helper: compiler options, defines, base includes, and sanitizer/coverage/Boost
# instrumentation shared by configure_test_target and configure_obj_library.
# NOTE: no whole-target AVX2 here by design — StringSearchAVX2.cpp carries
# per-source flags (see platform blocks); whole-target -mavx2 would emit
# AVX2 in all code and fault on older CPUs (see comment at the platform blocks).
function(_configure_build_target target_name)
    cmake_parse_arguments(ARG "" "" "" ${ARGN})

    target_compile_features(${target_name} PRIVATE cxx_std_17)

    if(MSVC)
        apply_msvc_project_compile_options(${target_name})
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:/Zi /Od>
            $<$<CONFIG:Release>:/O2>
        )
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target_name} PRIVATE
            $<$<CONFIG:Debug>:-g -O0>
            $<$<CONFIG:Release>:-O2>
        )
    endif()

    if(NOT WIN32)
        target_compile_definitions(${target_name} PRIVATE USN_WINDOWS_TESTS)
    endif()

    target_include_directories(${target_name} PRIVATE
        ${CMAKE_CURRENT_BINARY_DIR}  # Prefer generated GitVersion.h over source-tree copies
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}
    )

    if(TARGET ctrack::ctrack)
        target_link_libraries(${target_name} PRIVATE ctrack::ctrack)
    elseif(TARGET ctrack)
        target_link_libraries(${target_name} PRIVATE ctrack)
    endif()

    apply_asan_to_target(${target_name})
    apply_tsan_to_target(${target_name})
    apply_coverage_to_target(${target_name})
    # CRITICAL: must match configure_obj_library so FileIndex.h is seen with the same
    # type aliases (hash_map_t = boost::unordered_map vs std::unordered_map) in both
    # the compiled object library and the test executable TU.  A mismatch changes
    # sizeof(FileIndex) between the allocating TU and the constructor TU, which causes
    # an ASan stack-buffer-overflow when the Boost version is larger.
    apply_fast_libs_boost(${target_name})
endfunction()

# Configure a test executable: delegates to _configure_build_target, then adds doctest
# and nlohmann_json include paths needed by the test runner and test code.
function(configure_test_target target_name)
    _configure_build_target(${target_name} ${ARGN})
    target_include_directories(${target_name} PRIVATE
        ${DOCTEST_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
    )
endfunction()

# Configure a shared-source object library: delegates entirely to _configure_build_target.
function(configure_obj_library target_name)
    _configure_build_target(${target_name} ${ARGN})
endfunction()

# Windows shell COM helpers used by path/index tests (shlwapi, shcore, ole32). Extra libs via ARGN.
function(link_windows_shell_libs target_name)
    if(WIN32)
        target_link_libraries(${target_name} PRIVATE shlwapi shcore ole32 ${ARGN})
    endif()
endfunction()

# Apple Foundation / Linux Threads + optional libcurl (Gemini HTTP TUs).
function(link_unix_network_libs target_name)
    if(APPLE)
        target_link_libraries(${target_name} PRIVATE "-framework Foundation")
    elseif(UNIX AND NOT APPLE)
        target_link_libraries(${target_name} PRIVATE Threads::Threads)
        if(CURL_FOUND)
            target_link_libraries(${target_name} PRIVATE CURL::libcurl)
        endif()
    endif()
endfunction()

# Convert snake_case target name to PascalCase CTest name (string_search_tests -> StringSearchTests).
function(_snake_to_pascal out_var snake)
    string(REPLACE "_" ";" _parts "${snake}")
    set(_acc "")
    foreach(_p IN LISTS _parts)
        string(SUBSTRING "${_p}" 0 1 _first)
        string(TOUPPER "${_first}" _first)
        string(LENGTH "${_p}" _len)
        if(_len GREATER 1)
            string(SUBSTRING "${_p}" 1 -1 _rest)
        else()
            set(_rest "")
        endif()
        string(APPEND _acc "${_first}${_rest}")
    endforeach()
    set(${out_var} "${_acc}" PARENT_SCOPE)
endfunction()

# add_findhelper_test(<name>
#   SOURCES ...
#   [LIBS ...] [INCLUDES ...] [WINDOWS_EXTRA_LIBS ...]
#   [MSVC_LTCG] [WINDOWS_LIBS] [NO_CTEST] [BENCHMARK]
#   [TEST_NAME <CTestName>] [STATUS_SUFFIX <text>]
# )
function(add_findhelper_test target_name)
    cmake_parse_arguments(ARG
        "MSVC_LTCG;WINDOWS_LIBS;NO_CTEST;BENCHMARK"
        "TEST_NAME;STATUS_SUFFIX"
        "SOURCES;LIBS;INCLUDES;WINDOWS_EXTRA_LIBS"
        ${ARGN}
    )
    if(NOT ARG_SOURCES)
        message(FATAL_ERROR "add_findhelper_test(${target_name}): SOURCES is required")
    endif()
    if(ARG_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "add_findhelper_test(${target_name}): unexpected arguments: ${ARG_UNPARSED_ARGUMENTS}")
    endif()

    add_executable(${target_name} ${ARG_SOURCES})
    configure_test_target(${target_name})
    if(ARG_MSVC_LTCG)
        configure_msvc_ltcg(${target_name})
    endif()
    if(ARG_LIBS)
        target_link_libraries(${target_name} PRIVATE ${ARG_LIBS})
    endif()
    if(ARG_WINDOWS_LIBS OR ARG_WINDOWS_EXTRA_LIBS)
        link_windows_shell_libs(${target_name} ${ARG_WINDOWS_EXTRA_LIBS})
    endif()
    if(ARG_INCLUDES)
        target_include_directories(${target_name} PRIVATE ${ARG_INCLUDES})
    endif()
    if(NOT ARG_NO_CTEST)
        if(ARG_TEST_NAME)
            set(_ctest_name "${ARG_TEST_NAME}")
        else()
            _snake_to_pascal(_ctest_name "${target_name}")
        endif()
        add_test(NAME ${_ctest_name} COMMAND ${target_name})
    endif()
    if(ARG_BENCHMARK)
        message(STATUS "  Benchmark executable: ${target_name}${ARG_STATUS_SUFFIX}")
    else()
        message(STATUS "  Test executable: ${target_name}${ARG_STATUS_SUFFIX}")
    endif()
endfunction()


# Instrument macOS find_helper for coverage when requested (ImGui tests contribute to report).
if(APPLE AND ENABLE_COVERAGE AND COVERAGE_INSTRUMENT_FIND_HELPER AND TARGET find_helper)
    apply_coverage_to_target(find_helper)
    message(STATUS "macOS: find_helper enabled for code coverage (ImGui tests)")
endif()

if(BUILD_TESTS)
    # doctest location (header is at external/doctest/doctest/doctest.h)
    set(DOCTEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/external/doctest")

    if(EXISTS "${DOCTEST_DIR}/doctest/doctest.h")
        message(STATUS "Building unit tests with doctest")

        enable_testing()

        # ============================================================================
        # Object Libraries for Shared Test Sources
        # These object libraries compile shared source files once and reuse them
        # across multiple test executables, significantly reducing build times.
        # configure_obj_library() handles compile features, options, defines, base
        # includes, and sanitizer/coverage/Boost instrumentation for each library.
        # ============================================================================

        add_library(test_utils_obj OBJECT
            src/utils/CpuFeatures.cpp
            src/utils/StringSearchAVX2.cpp
            src/utils/StringSearchNEON.cpp
        )
        configure_obj_library(test_utils_obj)  # No whole-target AVX2: StringSearchAVX2.cpp carries per-source flags (see platform blocks); whole-target -mavx2 is banned (illegal instruction on older CPUs)

        add_library(test_path_obj OBJECT
            src/path/PathUtils.cpp
            src/path/PathPatternMatcher.cpp
        )
        configure_obj_library(test_path_obj)

        # Platform-specific StringUtils object library
        add_library(test_string_utils_obj OBJECT ${PLATFORM_STRING_UTILS_SRC})
        configure_obj_library(test_string_utils_obj)

        add_library(test_core_obj OBJECT
            src/core/Settings.cpp
            src/utils/LoadBalancingStrategy.cpp
        )
        configure_obj_library(test_core_obj)
        target_include_directories(test_core_obj PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
        )

        add_library(test_search_obj OBJECT
            src/search/ParallelSearchEngine.cpp
            src/search/SearchContext.cpp
            src/search/SearchContextBuilder.cpp
            src/search/SearchThreadPoolManager.cpp
            src/search/SearchThreadPool.cpp
            src/search/SearchStatisticsCollector.cpp
            src/search/FolderSizeAggregator.cpp
        )
        configure_obj_library(test_search_obj)
        target_include_directories(test_search_obj PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
            ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
        )

        # FileIndex object library (FileIndex, FileIndexMaintenance, FileIndexStorage, etc.)
        # Used by 10+ test targets - compiling once saves massive build time
        add_library(test_fileindex_obj OBJECT
            src/index/FileIndex.cpp
            src/index/FileIndexMaintenance.cpp
            src/index/PathRecomputer.cpp
            src/index/FileIndexStorage.cpp
            src/index/LazyAttributeLoader.cpp
            src/index/RemoveIndexedSubtree.cpp
            src/crawler/IndexOperations.cpp
            src/path/DirectoryResolver.cpp
            src/path/PathOperations.cpp
            src/path/PathBuilder.cpp
            src/path/PathStorage.cpp
        )
        configure_obj_library(test_fileindex_obj)
        target_include_directories(test_fileindex_obj PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
        )
        # CRITICAL: PGO flags for test_fileindex_obj — must match find_helper's /GL /Gy to avoid LNK1269
        if(MSVC AND ENABLE_PGO)
            target_compile_options(test_fileindex_obj PRIVATE
                $<$<CONFIG:Release>:/GL /Gy>
            )
        endif()

        add_library(test_thread_utils_obj OBJECT ${PLATFORM_THREAD_UTILS_SRC})
        configure_obj_library(test_thread_utils_obj)

        add_library(test_gui_obj OBJECT
            src/search/SearchHistory.cpp
            src/search/SearchResultUtils.cpp
            src/filters/SizeFilterUtils.cpp
            src/filters/TimeFilterUtils.cpp
            src/gui/GuiState.cpp
        )
        configure_obj_library(test_gui_obj)
        target_include_directories(test_gui_obj PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
            ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
        )

        add_library(test_helpers_obj OBJECT tests/TestHelpers.cpp)
        configure_obj_library(test_helpers_obj)
        target_include_directories(test_helpers_obj PRIVATE
            ${DOCTEST_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui
            ${CMAKE_CURRENT_SOURCE_DIR}/external/nlohmann_json/single_include
        )
        if(MSVC AND ENABLE_PGO)
            target_compile_options(test_helpers_obj PRIVATE
                $<$<CONFIG:Release>:/GL /Gy>
            )
        endif()

        message(STATUS "  Created object libraries for shared test sources (faster incremental builds)")

        # ============================================================================
        # Test Executables
        # ============================================================================
        set(_imgui_include_dir ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)

        add_findhelper_test(string_search_tests
            SOURCES tests/StringSearchTests.cpp
            LIBS test_utils_obj
        )

        add_findhelper_test(fuzzy_search_tests
            SOURCES tests/FuzzySearchTests.cpp
            LIBS test_utils_obj test_path_obj test_string_utils_obj
        )

        add_findhelper_test(cpu_features_tests
            SOURCES tests/CpuFeaturesTests.cpp
            LIBS test_utils_obj
        )

        add_findhelper_test(string_search_avx2_tests
            SOURCES tests/StringSearchAVX2Tests.cpp
            LIBS test_utils_obj
            TEST_NAME StringSearchAVX2Tests
        )
        if(NOT CMAKE_SYSTEM_PROCESSOR MATCHES "(x86_64|AMD64|i[3-6]86)")
            message(STATUS "  AVX2 not available on this architecture")
        endif()

        # NEON string search tests (arm64 — Apple Silicon, ARM64 Linux/Windows)
        add_findhelper_test(string_search_neon_tests
            SOURCES tests/StringSearchNEONTests.cpp
            LIBS test_utils_obj
            TEST_NAME StringSearchNEONTests
        )
        if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
            message(STATUS "  NEON available: arm64 architecture")
        else()
            message(STATUS "  NEON not available on this architecture (scalar fallback tested)")
        endif()

        add_findhelper_test(path_utils_tests
            SOURCES tests/PathUtilsTests.cpp
            LIBS test_path_obj test_string_utils_obj test_utils_obj
            WINDOWS_LIBS
        )

        add_findhelper_test(path_pattern_matcher_tests
            SOURCES tests/PathPatternMatcherTests.cpp
            LIBS test_path_obj test_string_utils_obj test_utils_obj
            WINDOWS_LIBS
        )

        add_findhelper_test(path_pattern_benchmark
            SOURCES tests/PathPatternBenchmark.cpp
            LIBS test_path_obj test_string_utils_obj test_utils_obj
            WINDOWS_LIBS
            NO_CTEST
            BENCHMARK
        )

        add_findhelper_test(search_benchmark
            SOURCES
                tests/SearchBenchmark.cpp
                src/index/IndexFromFilePopulator.cpp
                src/api/GeminiApiUtils.cpp
                ${GEMINI_API_HTTP_SRC}
            LIBS
                test_search_obj
                test_utils_obj
                test_fileindex_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
            NO_CTEST
            BENCHMARK
        )
        link_unix_network_libs(search_benchmark)

        # Benchmark smoke tests (run to completion, exit 0; no timing assertion)
        add_test(NAME PathPatternBenchmarkSmoke COMMAND path_pattern_benchmark)
        add_test(NAME SearchBenchmarkSmoke COMMAND search_benchmark
            --index ${CMAKE_SOURCE_DIR}/tests/benchmark_index_sample.txt
            --config ${CMAKE_SOURCE_DIR}/tests/benchmark_config_sample.json)

        add_findhelper_test(path_pattern_integration_tests
            SOURCES tests/path_pattern_integration_test.cpp
            LIBS test_path_obj test_utils_obj test_string_utils_obj
            WINDOWS_LIBS
        )

        add_findhelper_test(simple_regex_tests
            SOURCES tests/SimpleRegexTests.cpp
        )

        add_findhelper_test(search_pattern_utils_tests
            SOURCES tests/SearchPatternUtilsTests.cpp
            LIBS test_path_obj test_utils_obj test_string_utils_obj
        )

        # ExportSearchResultsService tests (CSV escaping and ExportToCsv)
        add_findhelper_test(export_search_results_service_tests
            SOURCES
                tests/ExportSearchResultsServiceTests.cpp
                src/search/ExportSearchResultsService.cpp
            LIBS
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_gui_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        add_findhelper_test(regex_generator_utils_tests
            SOURCES
                tests/RegexGeneratorUtilsTests.cpp
                src/utils/RegexGeneratorUtils.cpp
        )

        add_findhelper_test(system_path_filter_tests
            SOURCES
                tests/SystemPathFilterTests.cpp
                src/index/SystemPathFilter.cpp
        )

        add_findhelper_test(string_utils_tests
            SOURCES
                tests/StringUtilsTests.cpp
                tests/RelativeTimeTests.cpp
            LIBS test_string_utils_obj
        )

        add_findhelper_test(file_system_utils_tests
            SOURCES tests/FileSystemUtilsTests.cpp
            LIBS test_string_utils_obj
            WINDOWS_LIBS
        )

        add_findhelper_test(lazy_attribute_loader_tests
            SOURCES
                tests/LazyAttributeLoaderTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
            WINDOWS_EXTRA_LIBS shell32 propsys uuid
        )

        add_findhelper_test(parallel_search_engine_tests
            SOURCES
                tests/ParallelSearchEngineTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
        )

        # RegexSearchPrefilter tests — regression baseline before the literal pre-filter optimisation
        add_findhelper_test(regex_search_prefilter_tests
            SOURCES
                tests/RegexSearchPrefilterTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
        )

        # UsnJournalQueue tests — cross-platform; no Windows stubs needed
        add_findhelper_test(usn_journal_queue_tests
            SOURCES tests/UsnJournalQueueTests.cpp
        )

        add_findhelper_test(usn_activity_tracker_tests
            SOURCES tests/UsnActivityTrackerTests.cpp
        )

        # JournalCursor tests — cross-platform value object; no Windows stubs needed
        add_findhelper_test(journal_cursor_tests
            SOURCES tests/JournalCursorTests.cpp
        )

        # NtfsFileReference/MftRecordNumber tests — cross-platform value objects; no Windows stubs needed
        add_findhelper_test(ntfs_file_reference_tests
            SOURCES tests/NtfsFileReferenceTests.cpp
        )

        # UsnReason/UsnRecord tests — cross-platform value objects; no Windows stubs needed
        add_findhelper_test(usn_reason_tests
            SOURCES tests/UsnReasonTests.cpp
        )

        # IndexDomainEvents tests — cross-platform event types; no Windows stubs needed
        add_findhelper_test(index_domain_events_tests
            SOURCES tests/IndexDomainEventsTests.cpp
        )

        if(WIN32)
            # UsnMonitor two-phase apply: synthetic-buffer regression test.
            # Windows-only (winioctl.h USN_RECORD_V2 layout + real UsnMonitor);
            # no target on other platforms, so the file(STRINGS ...) loop below
            # (if(TARGET) guard) and build_tests_macos.sh (missing-executable
            # skip) ignore it gracefully there.
            add_findhelper_test(usn_two_phase_apply_tests
                SOURCES
                    tests/UsnTwoPhaseApplyTests.cpp
                    src/usn/UsnMonitor.cpp
                    src/usn/UsnRecordUtils.cpp
                    src/index/InitialIndexPopulator.cpp
                    src/index/mft/MftMetadataReader.cpp
                    src/index/SystemPathFilter.cpp
                    src/utils/LoggingUtils.cpp
                    src/platform/windows/PrivilegeUtils.cpp
                LIBS
                    test_search_obj
                    test_utils_obj
                    test_path_obj
                    test_string_utils_obj
                    test_core_obj
                    test_fileindex_obj
                    test_thread_utils_obj
                MSVC_LTCG
                WINDOWS_LIBS
            )
            # VolumeGateway seam: scripted-fake regression test for the
            # baseline enumeration (no volume attached). Same WIN32-guarded
            # shape as usn_two_phase_apply_tests above.
            add_findhelper_test(volume_gateway_tests
                SOURCES
                    tests/VolumeGatewayTests.cpp
                    src/index/InitialIndexPopulator.cpp
                    src/index/mft/MftMetadataReader.cpp
                    src/index/SystemPathFilter.cpp
                    src/usn/UsnRecordUtils.cpp
                    src/utils/LoggingUtils.cpp
                LIBS
                    test_search_obj
                    test_utils_obj
                    test_path_obj
                    test_string_utils_obj
                    test_core_obj
                    test_fileindex_obj
                    test_thread_utils_obj
                MSVC_LTCG
                WINDOWS_LIBS
            )
            add_findhelper_test(mft_metadata_reader_tests
                SOURCES
                    tests/MftMetadataReaderTests.cpp
                    src/index/mft/MftMetadataReader.cpp
                    src/utils/LoggingUtils.cpp
                LIBS
                    test_search_obj
                    test_utils_obj
                    test_path_obj
                    test_string_utils_obj
                    test_core_obj
                    test_fileindex_obj
                    test_thread_utils_obj
                MSVC_LTCG
                WINDOWS_LIBS
            )
        endif()

        add_findhelper_test(time_filter_utils_tests
            SOURCES
                tests/TimeFilterUtilsTests.cpp
            LIBS
                test_helpers_obj
                test_fileindex_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_gui_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        add_findhelper_test(command_line_args_tests
            SOURCES
                tests/CommandLineArgsTests.cpp
                src/core/CommandLineArgs.cpp
            LIBS
                test_search_obj
                test_utils_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_fileindex_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
            WINDOWS_LIBS
        )

        # IndexBuildState status-label helper tests (header-only)
        add_findhelper_test(index_build_state_tests
            SOURCES tests/IndexBuildStateTests.cpp
            MSVC_LTCG
        )

        # ApplicationLogic helper tests (header-only ShouldEnablePeriodicRecrawl)
        add_findhelper_test(application_logic_tests
            SOURCES tests/ApplicationLogicTests.cpp
            MSVC_LTCG
        )

        add_findhelper_test(gui_state_tests
            SOURCES
                tests/GuiStateTests.cpp
                tests/MatchHighlightTests.cpp
                src/search/SearchController.cpp
                src/search/SearchWorker.cpp
                ${SHARED_UTIL_SOURCES_FOR_TESTS}
            LIBS
                test_fileindex_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_gui_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        add_findhelper_test(file_index_search_strategy_tests
            SOURCES
                tests/FileIndexSearchStrategyTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_fileindex_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_thread_utils_obj
            MSVC_LTCG
        )

        # SearchStatisticsCollector tests
        # Compiles SearchStatisticsCollector.cpp directly to avoid pulling in the full search engine.
        add_findhelper_test(search_statistics_collector_tests
            SOURCES
                tests/SearchStatisticsCollectorTests.cpp
                src/search/SearchStatisticsCollector.cpp
            LIBS test_path_obj test_string_utils_obj test_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        # GeminiApiUtils tests (requires CURL on Linux; always built on Win/Mac)
        # CURL was already detected at the top of the file for Linux; result is cached.
        if(CURL_FOUND OR NOT (UNIX AND NOT APPLE))
            add_findhelper_test(gemini_api_utils_tests
                SOURCES
                    tests/GeminiApiUtilsTests.cpp
                    src/api/GeminiApiUtils.cpp
                    ${GEMINI_API_HTTP_SRC}
                LIBS
                    test_helpers_obj
                    test_search_obj
                    test_utils_obj
                    test_path_obj
                    test_string_utils_obj
                    test_core_obj
                    test_fileindex_obj
                    test_thread_utils_obj
                MSVC_LTCG
                WINDOWS_LIBS
                WINDOWS_EXTRA_LIBS winhttp
            )
            link_unix_network_libs(gemini_api_utils_tests)
        endif()

        add_findhelper_test(settings_tests
            SOURCES tests/SettingsTests.cpp
            LIBS
                test_search_obj
                test_utils_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(search_controller_tests
            SOURCES
                tests/SearchControllerTests.cpp
                src/search/SearchController.cpp
                src/search/SearchWorker.cpp
                src/utils/ExceptionHandling.cpp
            LIBS
                test_gui_obj
                test_search_obj
                test_utils_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_fileindex_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(search_worker_tests
            SOURCES
                tests/SearchWorkerTests.cpp
                src/search/SearchWorker.cpp
                src/utils/ExceptionHandling.cpp
            LIBS
                test_gui_obj
                test_search_obj
                test_utils_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(search_history_tests
            SOURCES tests/SearchHistoryTests.cpp
            LIBS
                test_gui_obj
                test_search_obj
                test_utils_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(search_context_tests
            SOURCES
                tests/SearchContextTests.cpp
                src/search/SearchContext.cpp
            LIBS test_path_obj test_string_utils_obj test_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(index_operations_tests
            SOURCES
                tests/IndexOperationsTests.cpp
                tests/FileIndexInsertPathDedupTests.cpp
                tests/MergeAndConvertSearchResultsTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_gui_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        # PathBuilder tests
        add_findhelper_test(path_builder_tests
            SOURCES tests/PathBuilderTests.cpp
            LIBS
                test_fileindex_obj
                test_search_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_utils_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        # PathOperations tests (PathOperations now depends on FileIndexStorage for path_storage_index)
        add_findhelper_test(path_operations_tests
            SOURCES tests/PathOperationsTests.cpp
            LIBS
                test_fileindex_obj
                test_search_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_utils_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        # FileOperations::ValidatePath boundary condition tests
        add_findhelper_test(path_extraction_tests
            SOURCES tests/PathExtractionTests.cpp
            LIBS test_path_obj test_string_utils_obj test_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(directory_resolver_tests
            SOURCES
                tests/DirectoryResolverTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(file_index_maintenance_tests
            SOURCES
                tests/FileIndexMaintenanceTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_thread_utils_obj
            MSVC_LTCG
            WINDOWS_LIBS
        )

        add_findhelper_test(std_regex_utils_tests
            SOURCES tests/StdRegexUtilsTests.cpp
            LIBS test_utils_obj test_path_obj test_string_utils_obj
            WINDOWS_LIBS
        )

        add_findhelper_test(search_result_sort_tests
            SOURCES
                tests/SearchResultSortTests.cpp
                src/search/SearchController.cpp
                src/search/SearchWorker.cpp
                ${SHARED_UTIL_SOURCES_FOR_TESTS}
            LIBS
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_gui_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        add_findhelper_test(exception_handling_tests
            SOURCES
                tests/ExceptionHandlingTests.cpp
                ${SHARED_UTIL_SOURCES_FOR_TESTS}
            LIBS test_utils_obj test_path_obj test_string_utils_obj
        )

        add_findhelper_test(total_size_computation_tests
            SOURCES tests/TotalSizeComputationTests.cpp
            LIBS
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_gui_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        add_findhelper_test(sort_staging_tests
            SOURCES
                tests/SortStagingTests.cpp
                src/utils/ThreadPool.cpp
            LIBS
                test_search_obj
                test_utils_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_fileindex_obj
                test_gui_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        add_findhelper_test(incremental_search_state_tests
            SOURCES
                tests/IncrementalSearchStateTests.cpp
                src/ui/IncrementalSearchState.cpp
            LIBS test_utils_obj
        )

        add_findhelper_test(folder_size_aggregator_tests
            SOURCES
                tests/FolderSizeAggregatorTests.cpp
            LIBS
                test_helpers_obj
                test_search_obj
                test_utils_obj
                test_fileindex_obj
                test_path_obj
                test_string_utils_obj
                test_core_obj
                test_thread_utils_obj
            MSVC_LTCG
        )

        add_findhelper_test(folder_crawler_tests
            SOURCES
                tests/FolderCrawlerTests.cpp
                src/crawler/FolderCrawler.cpp
            LIBS
                test_helpers_obj
                test_utils_obj
                test_core_obj
                test_path_obj
                test_string_utils_obj
                test_fileindex_obj
                test_search_obj
                test_thread_utils_obj
            INCLUDES ${_imgui_include_dir}
            MSVC_LTCG
        )

        # Comprehensive coverage test executable
        if(ENABLE_COVERAGE_ALL_SOURCES)
            set(COVERAGE_ALL_SOURCES
                src/core/CommandLineArgs.cpp
                src/index/FileIndex.cpp
                src/index/FileIndexMaintenance.cpp
                src/index/PathRecomputer.cpp
                src/index/FileIndexStorage.cpp
                src/index/RemoveIndexedSubtree.cpp
                src/index/SystemPathFilter.cpp
                src/crawler/IndexOperations.cpp
                src/path/DirectoryResolver.cpp
                src/path/PathOperations.cpp
                src/search/ParallelSearchEngine.cpp
                src/search/SearchContext.cpp
                src/search/SearchContextBuilder.cpp
                src/search/SearchThreadPoolManager.cpp
                src/path/PathBuilder.cpp
                src/path/PathStorage.cpp
                src/index/LazyAttributeLoader.cpp
                src/index/IndexFromFilePopulator.cpp
                src/utils/LoadBalancingStrategy.cpp
                src/path/PathPatternMatcher.cpp
                src/path/PathUtils.cpp
                src/search/SearchStatisticsCollector.cpp
                src/search/SearchThreadPool.cpp
                src/search/SearchWorker.cpp
                src/core/Settings.cpp
                src/filters/SizeFilterUtils.cpp
                src/utils/ThreadPool.cpp
                src/utils/ExceptionHandling.cpp
                src/utils/CpuFeatures.cpp
                src/utils/StringSearchAVX2.cpp
                src/utils/StringSearchNEON.cpp
                src/api/GeminiApiUtils.cpp
                ${PLATFORM_THREAD_UTILS_SRC}
                ${PLATFORM_STRING_UTILS_SRC}
                ${GEMINI_API_HTTP_SRC}
                tests/CoverageAllSourcesMain.cpp
            )

            # Windows MFT metadata reader (compiled into the Windows app target).
            if(WIN32)
                list(APPEND COVERAGE_ALL_SOURCES
                    src/index/mft/MftMetadataReader.cpp
                )
            endif()

            add_findhelper_test(coverage_all_sources
                SOURCES ${COVERAGE_ALL_SOURCES}
                STATUS_SUFFIX " (comprehensive coverage)"
            )
            # GeminiApiHttp_mac.mm needs Foundation; Linux HTTP uses Threads + libcurl.
            link_unix_network_libs(coverage_all_sources)
        endif()

        # Set 5-minute timeout for all registered tests (prevents hanging test runs across all platforms).
        # TESTS is a directory property (from add_test()), not a global property.
        get_property(_all_tests DIRECTORY PROPERTY TESTS)
        if(_all_tests)
            set_tests_properties(${_all_tests} PROPERTIES TIMEOUT 300)
            list(LENGTH _all_tests _num_tests)
            message(STATUS "  ${_num_tests} tests registered for CTest (timeout: 300s)")
        else()
            message(WARNING "  No tests found to configure")
        endif()

        # Sanitizers and coverage are applied automatically:
        #   - Test executables: via add_findhelper_test() -> configure_test_target()
        #   - Object libraries: via configure_obj_library()
        # Status messages
        if(ENABLE_ASAN)
            message(STATUS "  Address Sanitizer enabled for all test targets")
        endif()
        if(ENABLE_TSAN)
            message(STATUS "  Thread Sanitizer enabled for all test targets (macOS/Linux)")
        endif()
        if(ENABLE_COVERAGE)
            message(STATUS "  Code coverage enabled for all test targets")
        endif()

        # Build ctest command with optional verbose flags
        if(TEST_EXTRA_VERBOSE)
            set(CTEST_COMMAND_ARGS "--output-on-failure" "--extra-verbose")
            set(RUN_TESTS_COMMENT "Running all unit tests (extra verbose mode)...")
        elseif(TEST_VERBOSE_OUTPUT)
            set(CTEST_COMMAND_ARGS "--output-on-failure" "--verbose")
            set(RUN_TESTS_COMMENT "Running all unit tests (verbose mode)...")
        else()
            set(CTEST_COMMAND_ARGS "--output-on-failure")
            set(RUN_TESTS_COMMENT "Running all unit tests...")
        endif()

        # Single source of truth: scripts/test_targets.txt (one name per line).
        # build_tests_macos.sh and generate_coverage_macos.sh read the same file.
        file(STRINGS "${CMAKE_SOURCE_DIR}/scripts/test_targets.txt" TEST_TARGETS)
        set(TEST_DEPENDENCIES "")
        foreach(t ${TEST_TARGETS})
            if(TARGET ${t})
                list(APPEND TEST_DEPENDENCIES ${t})
            endif()
        endforeach()
        if(ENABLE_COVERAGE_ALL_SOURCES AND TARGET coverage_all_sources)
            list(APPEND TEST_DEPENDENCIES coverage_all_sources)
        endif()

        # DEPENDS on add_custom_target() only tracks files, not other CMake targets.
        # add_dependencies() wires IDE/project ordering, but on the Visual Studio generator
        # the custom COMMAND can still start before find_helper or *_tests.exe is linked
        # (parallel MSBuild). Force synchronous --build steps inside run_tests_target.cmake
        # so ctest never runs early, and so CONFIG (Debug/Release) is explicit
        # (see docs/guides/testing/WINDOWS_TEST_EXECUTION_GUIDE.md).
        add_custom_target(build_tests
            COMMENT "Building all unit test executables..."
        )
        add_dependencies(build_tests ${TEST_DEPENDENCIES})

        # Pass CONFIG via -D so the nested build/ctest use the same configuration as
        # cmake --build --config <cfg> (avoids empty/$<CONFIG> pitfalls on some VS setups).
        set(_run_tests_verbose OFF)
        set(_run_tests_extra_verbose OFF)
        if(TEST_EXTRA_VERBOSE)
            set(_run_tests_extra_verbose ON)
        elseif(TEST_VERBOSE_OUTPUT)
            set(_run_tests_verbose ON)
        endif()
        add_custom_target(run_tests
            COMMAND ${CMAKE_COMMAND}
                "-DCONFIG=$<CONFIG>"
                "-DBINARY_DIR=${CMAKE_BINARY_DIR}"
                "-DCTEST_VERBOSE=${_run_tests_verbose}"
                "-DCTEST_EXTRA_VERBOSE=${_run_tests_extra_verbose}"
                -P "${CMAKE_SOURCE_DIR}/scripts/run_tests_target.cmake"
            WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
            COMMENT ${RUN_TESTS_COMMENT}
            USES_TERMINAL
            VERBATIM
        )
        add_dependencies(run_tests build_tests)

        message(STATUS "  Build tests only: cmake --build . --target build_tests")
        message(STATUS "  Run tests with: cmake --build . --target run_tests")

        if(TEST_EXTRA_VERBOSE)
            message(STATUS "  Extra verbose test output enabled (TEST_EXTRA_VERBOSE=ON)")
        elseif(TEST_VERBOSE_OUTPUT)
            message(STATUS "  Verbose test output enabled (TEST_VERBOSE_OUTPUT=ON)")
            message(STATUS "  Use -DTEST_EXTRA_VERBOSE=ON for even more detailed output")
        else()
            message(STATUS "  Use -DTEST_VERBOSE_OUTPUT=ON for detailed test output")
            message(STATUS "  Use -DTEST_EXTRA_VERBOSE=ON for extra verbose output")
        endif()
    else()
        message(WARNING "doctest.h not found at ${DOCTEST_DIR}/doctest/doctest.h")
        message(WARNING "  Run: git submodule update --init --recursive")
        message(WARNING "  Unit tests will not be built")
    endif()
else()
    message(STATUS "Unit tests disabled (use -DBUILD_TESTS=ON to enable)")
endif()
