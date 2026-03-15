#pragma once

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

#include "path/PathUtils.h"
#include "utils/StringUtils.h"

#ifdef _WIN32
#include <direct.h>
#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <shlobj.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#include <psapi.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem
#else
#include <sys/stat.h>  // For mkdir on Unix/macOS
#include <unistd.h>    // For access() on Unix/macOS
#ifdef __APPLE__
#include <mach-o/dyld.h>  // For _NSGetExecutablePath on macOS
#include <mach/mach.h>    // For task_info on macOS
#endif  // __APPLE__
#include <climits>     // For PATH_MAX
#endif  // _WIN32

// Conditional compilation: Enable logging in all builds
// In Release builds, only IMPORTANT and ERROR level logs are enabled (set via minLogLevel_)
#define LOGGING_ENABLED

// Logger constants namespace
namespace logger_constants {
  constexpr int kMillisecondsPerSecond = 1000;
  constexpr size_t kBytesPerMB = 1024ULL * 1024ULL;
  constexpr size_t kDefaultMaxLogFileSizeMB = 10;
#ifndef _WIN32
  // Security: Use 0700 (rwx------) to restrict log directory to owner only
  // Log files may contain file paths and other sensitive information
  constexpr mode_t kDirectoryPermissions = 0700;
  // Security: Use 0600 (rw-------) to restrict log file to owner only
  constexpr mode_t kLogFilePermissions = 0600;
#endif  // _WIN32
} // namespace logger_constants

// to improve with https://www.cppstories.com/2021/stream-logger/

class Logger {
public:
  enum class LogLevel : std::uint8_t { // NOLINT(performance-enum-size)
    LOG_DEBUG,     // NOLINT(readability-identifier-naming)
    LOG_INFO,      // NOLINT(readability-identifier-naming)
    LOG_WARNING,   // NOLINT(readability-identifier-naming)
    LOG_IMPORTANT, // NOLINT(readability-identifier-naming)
    LOG_ERROR      // NOLINT(readability-identifier-naming)
  };

  // Get singleton instance
  static Logger &Instance() {
    static Logger instance;  // NOSONAR(cpp:S6018) - Function-local static is correct pattern for singleton, inline not allowed in block scope
    return instance;
  }

  // Set minimum log level (only messages at or above this level will be logged)
  void SetMinLogLevel(LogLevel level) {
#ifdef LOGGING_ENABLED
    const std::scoped_lock lock{mutex_};
    min_log_level_ = level;
#endif  // LOGGING_ENABLED
  }

  // Get current minimum log level
  [[nodiscard]] LogLevel GetMinLogLevel() const {
#ifdef LOGGING_ENABLED
    return min_log_level_;
#else
    return LogLevel::LOG_IMPORTANT; // Return important level in release builds
#endif  // LOGGING_ENABLED
  }

  // Set whether to flush after each log entry (default: true)
  void SetFlushAfterLog(bool flush) {
#ifdef LOGGING_ENABLED
    const std::scoped_lock lock{mutex_};
    flush_after_log_ = flush;
#endif  // LOGGING_ENABLED
  }

  // Set maximum log file size in MB (0 = no limit, default: 10MB)
  void SetMaxLogFileSizeMB(size_t max_size_mb) {
#ifdef LOGGING_ENABLED
    const std::scoped_lock lock{mutex_};
    max_log_file_size_mb_ = max_size_mb;
#endif  // LOGGING_ENABLED
  }

  // Enable/disable automatic memory logging on ERROR, IMPORTANT, and WARNING levels (default: true)
  void SetLogMemoryOnErrorWarning(bool enable) {
#ifdef LOGGING_ENABLED
    const std::scoped_lock lock{mutex_};
    log_memory_on_error_warning_ = enable;
#endif  // LOGGING_ENABLED
  }

  // Get current process private memory usage in bytes
  // Returns 0 if unable to retrieve (e.g., if API fails)
  // Cross-platform: Supports Windows, macOS, and Linux
  [[nodiscard]] size_t GetPrivateMemoryBytes() const {
#ifdef LOGGING_ENABLED
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX pmc = {0};
    pmc.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(),
                             reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),  // NOSONAR(cpp:S3630) - Windows API requires reinterpret_cast to convert PROCESS_MEMORY_COUNTERS_EX* to PROCESS_MEMORY_COUNTERS*
                             sizeof(pmc))) {
      return pmc.PrivateUsage;
    }
#elif defined(__APPLE__)
    // macOS: Use mach_task_basic_info to get resident memory size
    task_basic_info_data_t task_info;
    mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
    if (const kern_return_t result = ::task_info(mach_task_self(), TASK_BASIC_INFO,
                                       reinterpret_cast<task_info_t>(&task_info), &size); result == KERN_SUCCESS) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630) - macOS API task_info requires reinterpret_cast to convert task_basic_info_data_t* to task_info_t
      // Return resident memory size (physical memory currently used)
      return task_info.resident_size;
    }
#elif defined(__linux__)
    // Linux: Read VmRSS (resident set size) from /proc/self/status; value is in kB
    std::ifstream status_file("/proc/self/status");
    if (status_file.is_open()) {
      std::string line;
      while (std::getline(status_file, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
          size_t pos = 6;
          while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
            ++pos;
          }
          unsigned long value_kb = 0;
          if (pos < line.size() && std::isdigit(static_cast<unsigned char>(line[pos])) != 0) {
            const char* start = line.c_str() + pos;
            char* end = nullptr;
            value_kb = std::strtoul(start, &end, 10);
            if (end != start && value_kb > 0) {
              return static_cast<size_t>(value_kb) * 1024ULL;
            }
          }
          break;
        }
      }
    }
#endif  // _WIN32 / __APPLE__ / __linux__
#endif  // LOGGING_ENABLED
    return 0;
  }

  // Format memory bytes into human-readable string (e.g., "15.3 MB")
  // Delegates to StringUtils::FormatMemory for better separation of concerns
  [[nodiscard]] std::string FormatMemory(size_t bytes) const {
    return ::FormatMemory(bytes);  // Call free function from StringUtils (global namespace)
  }

  // Explicitly log current memory usage
  void LogMemory(std::string_view context = "") { // NOLINT(readability-make-member-function-const)
#ifdef LOGGING_ENABLED
    const size_t memory_bytes = GetPrivateMemoryBytes();
    if (memory_bytes > 0) {
      std::string message = "Memory usage";
      if (!context.empty()) {
        message += " (" + std::string(context) + ")";
      }
      message += ": " + FormatMemory(memory_bytes);
      Log(LogLevel::LOG_INFO, message);
    }
#endif  // LOGGING_ENABLED
  }

  // Get current log file path (empty string if logging is disabled or file not open)
  [[nodiscard]] std::string GetLogFilePath() const {
#ifdef LOGGING_ENABLED
    const std::scoped_lock lock{mutex_};
    return log_file_path_;
#else
    return std::string();
#endif  // LOGGING_ENABLED
  }

  // Log a message with specified level
  void Log(LogLevel level, const std::string &message) { // NOLINT(readability-make-member-function-const)
#ifdef LOGGING_ENABLED
    // Early exit if level is below minimum
    if (level < min_log_level_) {
      return;
    }

    try {
      const std::scoped_lock lock{mutex_};

      // Check if we need to rotate log file
      if (max_log_file_size_mb_ > 0 && log_file_.is_open()) {
        CheckAndRotateLogFile();
      }

      if (!log_file_.is_open() && !TryOpenLogFile()) {
        // Try to reopen the log file
        return; // Silently fail if file couldn't be opened
      }

      // Get current timestamp and format it
      const auto now = std::chrono::system_clock::now();
      const std::string timestamp = FormatTimestamp(now);

      // Format: [YYYY-MM-DD HH:MM:SS.mmm] [LEVEL] message [Memory: X MB]
      log_file_ << timestamp << " [" << LevelToString(level) << "] " << message;

      // Optionally append memory info for ERROR, IMPORTANT, and WARNING levels
      if (log_memory_on_error_warning_ &&
          (level == LogLevel::LOG_ERROR || level == LogLevel::LOG_IMPORTANT || level == LogLevel::LOG_WARNING)) {
        const size_t memory_bytes = GetPrivateMemoryBytes();
        if (memory_bytes > 0) {
          log_file_ << " [Memory: " << FormatMemory(memory_bytes) << "]";
        }
      }

      log_file_ << '\n';

      if (flush_after_log_) {
        log_file_.flush(); // Ensure immediate write
      }

      // Check if write operations succeeded (important for detecting silent failures)
      // On Windows, file writes can fail silently (e.g., permissions, disk full, file locked)
      if (!log_file_.good()) {
        // Write failed - clear error state and try to reopen the file
        log_file_.clear(); // Clear error flags
        log_file_.close();
        if (!TryOpenLogFile()) {
          // File couldn't be reopened - logging is now disabled
          // Output to stderr so user knows logging has failed (only once to avoid spam)
          if (static bool logged_failure = false; !logged_failure) {  // Static variable in init-statement persists across calls (function-local static)
            std::cerr << "[Logger] ERROR: Log file write failed and file could not be reopened. Logging disabled." << '\n';
            logged_failure = true;
          }
          return;
        }
        // File reopened successfully - next log call will use the reopened file
        // Note: We don't retry the current message to avoid recursion and potential infinite loops
      }
    } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Silently ignore exceptions during logging to prevent exceptions from propagating (especially important when called from destructor). Logging failures are not critical - application can continue without logging
    }
#endif  // LOGGING_ENABLED
  }

  // Delete copy constructor and assignment operator (singleton pattern)
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
  // Delete move constructor and assignment operator (singleton pattern)
  Logger(Logger &&) = delete;
  Logger &operator=(Logger &&) = delete;

private:
  // Helper function to format timestamp (extracted to eliminate duplication)
  [[nodiscard]] std::string FormatTimestamp(const std::chrono::system_clock::time_point& now) const {
    const auto now_time_t = std::chrono::system_clock::to_time_t(now);
    const auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now.time_since_epoch()) %
                  logger_constants::kMillisecondsPerSecond;

    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &now_time_t);
#else
    localtime_r(&now_time_t, &tm_buf);
#endif  // _WIN32

    std::ostringstream oss;
    oss << "[" << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S") << "."
        << std::setfill('0') << std::setw(3) << now_ms.count() << "]";
    return oss.str();
  }

  // Helper function to extract filename from path (extracted to eliminate duplication)
  [[nodiscard]] std::string ExtractFilename(std::string_view path) const {
    if (const size_t last_sep = path.find_last_of("\\/"); last_sep != std::string_view::npos) {
      return std::string(path.substr(last_sep + 1));
    }
    return std::string(path);
  }

  // Helper function to report directory creation error (extracted to eliminate duplication)
  void ReportDirectoryError(std::string_view dir_path, int error_code) const {
    std::cerr << "[Logger] Warning: Failed to create log directory: "
              << dir_path << " (errno: " << error_code << ")" << '\n';
  }

  // Helper function to create directory with improved error handling
  [[nodiscard]] bool CreateDirectoryIfNeeded(std::string_view dir_path) const {
    const std::string dir_path_str(dir_path);
    int mkdir_result = 0;
#ifdef _WIN32
    mkdir_result = _mkdir(dir_path_str.c_str());
#else
    // On Unix/macOS, use mkdir with appropriate permissions
    mkdir_result = mkdir(dir_path_str.c_str(), logger_constants::kDirectoryPermissions);
#endif  // _WIN32
    if (mkdir_result != 0 && errno != EEXIST) {
      // Check if error is because directory already exists (errno == EEXIST)
      // If directory already exists, that's fine - continue
      // Otherwise, log the error but still try to open the file
      // Directory creation failed for a reason other than "already exists"
      // Log to stderr since logger might not be initialized yet
      ReportDirectoryError(dir_path, errno);
    }
    return true; // Return true to allow file open attempt even if mkdir failed
  }

  Logger() {
#ifdef LOGGING_ENABLED
#ifdef _WIN32
    // Try to attach to parent console (e.g., PowerShell), or allocate a new one
    bool consoleAttached = false;
    if (AttachConsole(ATTACH_PARENT_PROCESS)) {
      consoleAttached = true;
    } else if (AllocConsole()) {
      consoleAttached = true;
      SetConsoleTitleA("The FindHelper Experiment - Debug Console");
    }

    if (consoleAttached) {
      // Redirect stdout and stderr to the console
      FILE* pCout;
      FILE* pCerr;
#ifdef _WIN32
      freopen_s(&pCout, "CONOUT$", "w", stdout);
      freopen_s(&pCerr, "CONOUT$", "w", stderr);
#else
      // On Unix/macOS, stdout/stderr are already connected to terminal
      pCout = stdout;
      pCerr = stderr;
#endif  // _WIN32
      // Disable buffering for immediate output
      if (pCout) setvbuf(pCout, nullptr, _IONBF, 0);
      if (pCerr) setvbuf(pCerr, nullptr, _IONBF, 0);
    }
#endif  // _WIN32
    // Get executable name
    executable_name_ = GetExecutableName();

    if (TryOpenLogFile()) {
      // Log file path is written to the log file itself, no need for console output
      // (Console output causes Windows to allocate a console window for GUI applications)
      std::string session_msg = "=== Logging session started ===";
      if (!executable_name_.empty()) {
        session_msg += " [Executable: " + executable_name_ + "]";
      }
      Log(LogLevel::LOG_IMPORTANT, session_msg);
      Log(LogLevel::LOG_IMPORTANT, "Log file: " + log_file_path_);
      // Log initial memory usage
      LogMemory("session start");
    } else {
      // Log error to log file if possible, but don't use std::cerr (avoids console allocation)
      // If log file can't be opened, there's no way to report the error anyway
    }
#endif  // LOGGING_ENABLED
  }

  ~Logger() {
#ifdef LOGGING_ENABLED
    if (log_file_.is_open()) {
      try {
        // Log final memory usage before session ends
        LogMemory("session end");
        std::string session_msg = "=== Logging session ended ===";
        if (!executable_name_.empty()) {
          session_msg += " [Executable: " + executable_name_ + "]";
        }
        Log(LogLevel::LOG_IMPORTANT, session_msg);
        log_file_.flush();
      } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Destructors must not throw exceptions - silently ignore logging errors during shutdown to prevent undefined behavior
      }
      try {
        log_file_.close();
      } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Destructors must not throw exceptions - silently ignore close errors during shutdown to prevent undefined behavior
      }
    }
#endif  // LOGGING_ENABLED
  }

  // Get default log directory (tries TEMP env var, then <volume>\\temp, then current directory)
  // Security: On Unix-like systems, prefers user-specific cache directory to avoid
  // publicly writable /tmp directory, which could expose sensitive log information
  [[nodiscard]] std::string GetDefaultLogDirectory() const {
#ifdef _WIN32
    // Try TEMP environment variable first
    char temp_path[MAX_PATH];  // NOSONAR(cpp:S5945) - Windows API GetEnvironmentVariableA requires C-style char array
    DWORD result = GetEnvironmentVariableA("TEMP", temp_path, MAX_PATH);
    if (result > 0 && result < MAX_PATH) {
      return std::string(temp_path);
    }

    // Try TMP environment variable
    result = GetEnvironmentVariableA("TMP", temp_path, MAX_PATH);
    if (result > 0 && result < MAX_PATH) {
      return std::string(temp_path);
    }

    // Fallback to <volume>/temp using shared volume root constant
    return path_utils::JoinPath(path_utils::GetDefaultVolumeRootPath(), "temp");
#else
    // Security: Prefer user-specific cache directory to avoid publicly writable /tmp
    // Try XDG_CACHE_HOME first (standard on Linux)
    if (const char* xdg_cache = std::getenv("XDG_CACHE_HOME"); xdg_cache != nullptr && xdg_cache[0] != '\0') {  // NOLINT(concurrency-mt-unsafe) - C++17 init-statement; GetDefaultLogDirectory only called under mutex_
      return {xdg_cache};
    }

    // Try $HOME/.cache (fallback for XDG_CACHE_HOME)
    if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {  // NOLINT(concurrency-mt-unsafe) - C++17 init-statement; GetDefaultLogDirectory only called under mutex_
      return std::string(home) + "/.cache";
    }

    // Last resort: use /tmp (publicly writable, but better than failing)
    // Note: Log file permissions are set to 0600 to restrict access
    // /tmp is only used as last resort when $XDG_CACHE_HOME and $HOME are unavailable.
    // In practice, $HOME is almost always available on Unix-like systems, so this path is rarely taken.
    // File permissions (0600) are set after creation to restrict access. The risk is acceptable because
    // logging must not fail silently - better to log to /tmp with restrictive permissions than to fail.
    return "/tmp";  // NOSONAR(cpp:S5443) - Last resort fallback when user-specific directories unavailable
#endif  // _WIN32
  }

  // Try to open the log file, creating directory if needed
  bool TryOpenLogFile() {
    const std::string log_dir = GetDefaultLogDirectory();
#ifdef _WIN32
    std::string log_file = log_dir + "\\FindHelper.log";
#else
    std::string log_file = log_dir + "/FindHelper.log";
#endif  // _WIN32

    // Create directory if it doesn't exist (with improved error handling)
    (void)CreateDirectoryIfNeeded(log_dir);

    log_file_.open(log_file, std::ios::app);
    if (!log_file_.is_open()) {
      // Try current directory as fallback
      log_file = "monitor.log";
      log_file_.open(log_file, std::ios::app);
    }

    if (log_file_.is_open()) {
      log_file_path_ = log_file;
#ifndef _WIN32
      // Security: Set restrictive permissions on log file (owner read/write only)
      // This prevents other users from reading log files that may contain sensitive information
      // Note: chmod() may fail silently if file is on a filesystem that doesn't support permissions
      // (e.g., some network filesystems), but that's acceptable - we try our best
      chmod(log_file.c_str(), logger_constants::kLogFilePermissions);
#endif  // _WIN32
    }

    return log_file_.is_open();
  }

  // Check log file size and rotate if necessary
  void CheckAndRotateLogFile() { // NOLINT(readability-make-member-function-const)
    if (!log_file_.is_open()) {
      return;
    }

    try {
      // Flush to ensure we get accurate file size
      log_file_.flush();

      // Get current file position (which is the file size for append mode)
      const std::streampos current_pos = log_file_.tellp();
      if (current_pos < 0) {
        return; // Couldn't determine file size
      }

      const size_t current_size_mb = static_cast<size_t>(current_pos) / logger_constants::kBytesPerMB;

      if (current_size_mb >= max_log_file_size_mb_) {
        // Use the actual log file path (not hardcoded filename)
        const std::string current_log_file = log_file_path_;
        log_file_.close();

        // Create backup filename by appending .old
        const std::string backup_file = current_log_file + ".old";

        // Remove old backup if it exists (ignore return: file may not exist)
        (void)std::remove(backup_file.c_str());  // NOLINT(cert-err33-c) - Best-effort; missing backup is OK

        // Rename current log to backup
        if (std::rename(current_log_file.c_str(), backup_file.c_str()) != 0) {
          // Cannot call Log() here (we are inside Log() and hold the mutex); report to stderr
          (void)std::fprintf(stderr, "[Logger] Log rotation rename failed: %s -> %s\n",
                             current_log_file.c_str(), backup_file.c_str());  // NOLINT(cert-err33-c) - Best-effort report; ignore fprintf return
        }

        // Open new log file (same path as before)
        log_file_.open(current_log_file, std::ios::app);
        if (log_file_.is_open()) {
          // log_file_path_ already contains the correct path, no need to update
          // Note: We can't call Log() here because we're already inside Log() and holding the mutex
          // Instead, write directly to avoid deadlock
          const auto now = std::chrono::system_clock::now();
          const std::string timestamp = FormatTimestamp(now);
          log_file_ << timestamp << " [INFO] === Log file rotated ===" << '\n';
          if (flush_after_log_) {
            log_file_.flush();
          }
        }
      }
    } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Silently ignore exceptions during log rotation to prevent exceptions from propagating (especially important when called from destructor). Log rotation failure is not critical - logging can continue without rotation
    }
  }

  [[nodiscard]] inline std::string LevelToString(LogLevel level) const {
    switch (level) {
    case LogLevel::LOG_DEBUG:
      return "DEBUG";
    case LogLevel::LOG_INFO:
      return "INFO";
    case LogLevel::LOG_WARNING:
      return "WARNING";
    case LogLevel::LOG_IMPORTANT:
      return "IMPORTANT";
    case LogLevel::LOG_ERROR:
      return "ERROR";
    default:
      return "UNKNOWN";
    }
  }

  // Get executable name (platform-specific)
  [[nodiscard]] std::string GetExecutableName() const {
#ifdef _WIN32
    char module_path[MAX_PATH];  // NOSONAR(cpp:S5945) - Windows API GetModuleFileNameA requires C-style char array
    if (DWORD result = GetModuleFileNameA(nullptr, module_path, MAX_PATH); result > 0 && result < MAX_PATH) {
      // Extract just the filename from the full path
      return ExtractFilename(std::string(module_path));
    }
#else
#ifdef __APPLE__
    // macOS: Use _NSGetExecutablePath
    char path[PATH_MAX];  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) NOSONAR(cpp:S5945) - macOS API _NSGetExecutablePath requires C-style buffer
    if (uint32_t size = sizeof(path); _NSGetExecutablePath(path, &size) == 0) {  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - macOS API requires C-style buffer
      // Extract just the filename from the full path
      return ExtractFilename(std::string(path));  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay) - path is a C-style buffer
    }
#else  // Linux
    char path[PATH_MAX];  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) NOSONAR(cpp:S5945) - Linux API readlink requires C-style buffer
    if (ssize_t count = readlink("/proc/self/exe", path, sizeof(path)); count != -1) {
        std::string exePath(path, count);
        return ExtractFilename(exePath);
    }
#endif  // __APPLE__
#endif  // _WIN32
    return "";  // Return empty string if unable to get executable name
  }

  mutable std::mutex mutex_; // NOLINT(readability-identifier-naming)  // Mutable to allow locking in const methods
  std::ofstream log_file_; // NOLINT(readability-identifier-naming)
  std::string log_file_path_; // NOLINT(readability-identifier-naming)
#ifdef NDEBUG
  LogLevel min_log_level_ = LogLevel::LOG_IMPORTANT; // NOLINT(readability-identifier-naming)
#else
  LogLevel min_log_level_ = LogLevel::LOG_DEBUG; // NOLINT(readability-identifier-naming)
#endif  // NDEBUG
  bool flush_after_log_ = true; // NOLINT(readability-identifier-naming)
  size_t max_log_file_size_mb_ = logger_constants::kDefaultMaxLogFileSizeMB; // NOLINT(readability-identifier-naming)
  bool log_memory_on_error_warning_ = true; // NOLINT(readability-identifier-naming)
  std::string executable_name_; // NOLINT(readability-identifier-naming)  // Cached executable name
};

// Convenience macros for logging
// In Release builds, only IMPORTANT and ERROR macros are enabled (zero overhead for others)
// In Debug builds, all macros are enabled
#ifdef NDEBUG
  // Release builds: Only IMPORTANT and ERROR logging enabled (zero overhead for others)
  #define LOG_DEBUG(msg) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO(msg) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING(msg) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT(msg) Logger::Instance().Log(Logger::LogLevel::LOG_IMPORTANT, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR(msg) Logger::Instance().Log(Logger::LogLevel::LOG_ERROR, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
#else
  // Debug builds: All logging enabled
  #define LOG_DEBUG(msg) Logger::Instance().Log(Logger::LogLevel::LOG_DEBUG, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO(msg) Logger::Instance().Log(Logger::LogLevel::LOG_INFO, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING(msg) Logger::Instance().Log(Logger::LogLevel::LOG_WARNING, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT(msg) Logger::Instance().Log(Logger::LogLevel::LOG_IMPORTANT, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR(msg) Logger::Instance().Log(Logger::LogLevel::LOG_ERROR, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
#endif  // NDEBUG

// Convenience macros for building log messages
// Usage: LOG_INFO_BUILD("Message: " << value);
// In Release builds, only IMPORTANT and ERROR macros are enabled (zero overhead for others)

// Helper macro to build log message and call log function (eliminates duplication)
#define LOG_BUILD_HELPER(expr, log_func) do { /* NOLINT(cppcoreguidelines-macro-usage) */ \
  std::ostringstream _oss; /* NOLINT(misc-const-correctness) - _oss modified by << */ \
  _oss << expr; /* NOLINT(bugprone-macro-parentheses) */ \
  log_func(_oss.str()); /* NOLINT(bugprone-macro-parentheses) */ \
} while(0)

// Helper macro to build log message, call log function and return the string (eliminates duplication)
#define LOG_BUILD_AND_GET_HELPER(expr, log_func) [&]() -> std::string { /* NOLINT(cppcoreguidelines-macro-usage) NOSONAR(cpp:S3608) - Macro needs [&] to capture all variables in any context */ \
  std::ostringstream _oss; /* NOLINT(misc-const-correctness) - _oss modified by << */ \
  _oss << expr; /* NOLINT(bugprone-macro-parentheses) */ \
  const std::string _msg = _oss.str(); \
  log_func(_msg); \
  return _msg; \
}()

#ifdef NDEBUG
  // Release builds: Only IMPORTANT and ERROR logging enabled (zero overhead for others)
  #define LOG_DEBUG_BUILD(expr) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD(expr) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING_BUILD(expr) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)

  // Macros that both log and return the message string
  #define LOG_ERROR_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD_AND_GET(expr) std::string("") // NOLINT(cppcoreguidelines-macro-usage)
#else
  // Debug builds: All logging enabled
  #define LOG_DEBUG_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_DEBUG) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_INFO) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_WARNING) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)

  // Macros that both log and return the message string
  #define LOG_ERROR_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_INFO) // NOLINT(cppcoreguidelines-macro-usage)
#endif  // NDEBUG

// RAII-based execution timer
// Only active in Debug builds (disabled in Release for zero overhead)
class ScopedTimer {  // NOSONAR(cpp:S3624) - Default copy/move is correct (simple RAII timer, members are trivially copyable). No copy/move needed in Release builds (empty class)
public:
  // Default copy/move operations (simple RAII timer, members are trivially copyable)
  ScopedTimer(const ScopedTimer&) = default;
  ScopedTimer& operator=(const ScopedTimer&) = default;
  ScopedTimer(ScopedTimer&&) = default;
  ScopedTimer& operator=(ScopedTimer&&) = default;
#ifdef NDEBUG
  // Release builds: Timer does nothing (zero overhead)
  explicit ScopedTimer(const std::string & /* operation_name */) {}  // NOLINT(hicpp-named-parameter,readability-named-parameter) - intentionally unused in release
#else
  // Debug builds: Timer is active
  explicit ScopedTimer(std::string operation_name)
      : operation_name_(std::move(operation_name)) {
    start_time_ = std::chrono::high_resolution_clock::now();
  }

  ~ScopedTimer() {
    try {
      const auto end_time = std::chrono::high_resolution_clock::now();
      const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
          end_time - start_time_);

      std::ostringstream oss;
      oss << operation_name_ << " completed in ";

      if (duration.count() >= logger_constants::kMillisecondsPerSecond) {
        const double seconds =
            static_cast<double>(duration.count()) / static_cast<double>(logger_constants::kMillisecondsPerSecond);
        oss << std::fixed << std::setprecision(2) << seconds << " seconds";
      } else {
        oss << duration.count() << " ms";
      }

      LOG_INFO(oss.str());
    } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Destructors must not throw exceptions - silently ignore logging errors during shutdown to prevent undefined behavior
    }
  }

private:
  std::string operation_name_;  // NOLINT(readability-identifier-naming)
  std::chrono::high_resolution_clock::time_point start_time_;  // NOLINT(readability-identifier-naming)
#endif  // NDEBUG
};
