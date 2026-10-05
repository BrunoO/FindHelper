#pragma once

#include <chrono>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

// Conditional compilation: Enable logging in all builds
// In Release builds, only IMPORTANT and ERROR level logs are enabled (set via minLogLevel_)
#define LOGGING_ENABLED

// Logger constants namespace
namespace logger_constants {
  constexpr int kMillisecondsPerSecond = 1000;
  constexpr int kMicrosecondsPerMillisecond = 1000;
  constexpr size_t kBytesPerMB = 1024ULL * 1024ULL;
  constexpr size_t kDefaultMaxLogFileSizeMB = 10;
} // namespace logger_constants

class Logger {
public:
  enum class LogLevel : std::uint8_t { // NOLINT(performance-enum-size)
    LOG_DEBUG,     // NOLINT(readability-identifier-naming)
    LOG_INFO,      // NOLINT(readability-identifier-naming)
    LOG_WARNING,   // NOLINT(readability-identifier-naming)
    LOG_IMPORTANT, // NOLINT(readability-identifier-naming)
    LOG_ERROR,     // NOLINT(readability-identifier-naming)
  };

  // Get singleton instance
  static Logger &Instance();

  // Set minimum log level (only messages at or above this level will be logged)
  void SetMinLogLevel(LogLevel level);

  // Get current minimum log level
  [[nodiscard]] LogLevel GetMinLogLevel() const;

  // Set whether to flush after each log entry (default: true)
  void SetFlushAfterLog(bool flush);

  // Set maximum log file size in MB (0 = no limit, default: 10MB)
  void SetMaxLogFileSizeMB(size_t max_size_mb);

  // Enable/disable automatic memory logging on ERROR, IMPORTANT, and WARNING levels (default: true)
  void SetLogMemoryOnErrorWarning(bool enable);

  /**
   * @brief Best-effort IMPORTANT log for fatal crash / terminate paths.
   */
  void LogBestEffortFatal(std::string_view message);

  // Get current process private memory usage in bytes
  [[nodiscard]] size_t GetPrivateMemoryBytes() const;

  // Format memory bytes into human-readable string (e.g., "15.3 MB")
  [[nodiscard]] std::string FormatMemory(size_t bytes) const;

  // Explicitly log current memory usage
  void LogMemory(std::string_view context = "");

  // Get current log file path (empty string if logging is disabled or file not open)
  [[nodiscard]] std::string GetLogFilePath() const;

  // Log a message with specified level. Takes string_view to avoid a heap
  // allocation at the call site; the message is streamed synchronously under
  // the log mutex, so no lifetime is retained after return.
  void Log(LogLevel level, std::string_view message);

  // Delete copy constructor and assignment operator (singleton pattern)
  Logger(const Logger &) = delete;
  Logger &operator=(const Logger &) = delete;
  // Delete move constructor and assignment operator (singleton pattern)
  Logger(Logger &&) = delete;
  Logger &operator=(Logger &&) = delete;

private:
  Logger();
  ~Logger();

  [[nodiscard]] std::string FormatTimestamp(const std::chrono::system_clock::time_point& now) const;
  [[nodiscard]] std::string ExtractFilename(std::string_view path) const;
  void ReportDirectoryError(std::string_view dir_path, int error_code) const;
  [[nodiscard]] bool CreateDirectoryIfNeeded(std::string_view dir_path) const;
  [[nodiscard]] std::string GetDefaultLogDirectory() const;
  bool TryOpenLogFile();
  void CheckAndRotateLogFile();
  [[nodiscard]] std::string LevelToString(LogLevel level) const;
  [[nodiscard]] std::string GetExecutableName() const;
  [[nodiscard]] std::string BuildLogFileName() const;
  [[nodiscard]] std::string BuildSessionId() const;
  [[nodiscard]] static std::uint64_t GetCurrentProcessId();

  mutable std::mutex mutex_; // NOLINT(readability-identifier-naming)
  std::ofstream log_file_; // NOLINT(readability-identifier-naming)
  std::string log_file_path_; // NOLINT(readability-identifier-naming)
  std::string session_id_; // NOLINT(readability-identifier-naming)
#ifdef NDEBUG
  LogLevel min_log_level_ = LogLevel::LOG_IMPORTANT; // NOLINT(readability-identifier-naming)
#else
  LogLevel min_log_level_ = LogLevel::LOG_DEBUG; // NOLINT(readability-identifier-naming)
#endif  // NDEBUG
  bool flush_after_log_ = true; // NOLINT(readability-identifier-naming)
  size_t max_log_file_size_mb_ = logger_constants::kDefaultMaxLogFileSizeMB; // NOLINT(readability-identifier-naming)
  bool log_memory_on_error_warning_ = true; // NOLINT(readability-identifier-naming)
  std::string executable_name_; // NOLINT(readability-identifier-naming)
};

// Convenience macros for logging
#ifdef NDEBUG
  #define LOG_DEBUG(msg) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO(msg) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING(msg) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT(msg) Logger::Instance().Log(Logger::LogLevel::LOG_IMPORTANT, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR(msg) Logger::Instance().Log(Logger::LogLevel::LOG_ERROR, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
#else
  #define LOG_DEBUG(msg) Logger::Instance().Log(Logger::LogLevel::LOG_DEBUG, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO(msg) Logger::Instance().Log(Logger::LogLevel::LOG_INFO, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING(msg) Logger::Instance().Log(Logger::LogLevel::LOG_WARNING, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT(msg) Logger::Instance().Log(Logger::LogLevel::LOG_IMPORTANT, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR(msg) Logger::Instance().Log(Logger::LogLevel::LOG_ERROR, (msg)) // NOLINT(cppcoreguidelines-macro-usage)
#endif  // NDEBUG

// Convenience macros for building log messages
#define LOG_BUILD_HELPER(expr, log_func) do { /* NOLINT(cppcoreguidelines-macro-usage) */ \
  std::ostringstream _oss; /* NOLINT(misc-const-correctness) - _oss modified by << */ \
  _oss << expr; /* NOLINT(bugprone-macro-parentheses) */ \
  log_func(_oss.str()); /* NOLINT(bugprone-macro-parentheses) */ \
} while(0)

#define LOG_BUILD_AND_GET_HELPER(expr, log_func) [&]() -> std::string { /* NOLINT(cppcoreguidelines-macro-usage) NOSONAR(cpp:S3608) */ \
  std::ostringstream _oss; /* NOLINT(misc-const-correctness) - _oss modified by << */ \
  _oss << expr; /* NOLINT(bugprone-macro-parentheses) */ \
  const std::string _msg = _oss.str(); \
  log_func(_msg); \
  return _msg; \
}()

#ifdef NDEBUG
  #define LOG_DEBUG_BUILD(expr) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD(expr) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING_BUILD(expr) ((void)0) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)

  #define LOG_ERROR_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD_AND_GET(expr) std::string("") // NOLINT(cppcoreguidelines-macro-usage)
#else
  #define LOG_DEBUG_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_DEBUG) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_INFO) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_WARNING_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_WARNING) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_ERROR_BUILD(expr) LOG_BUILD_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)

  #define LOG_ERROR_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_ERROR) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_IMPORTANT_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_IMPORTANT) // NOLINT(cppcoreguidelines-macro-usage)
  #define LOG_INFO_BUILD_AND_GET(expr) LOG_BUILD_AND_GET_HELPER(expr, LOG_INFO) // NOLINT(cppcoreguidelines-macro-usage)
#endif  // NDEBUG

// RAII-based execution timer
class ScopedTimer {  // NOSONAR(cpp:S3624)
public:
  ScopedTimer(const ScopedTimer&) = default;
  ScopedTimer& operator=(const ScopedTimer&) = default;
  ScopedTimer(ScopedTimer&&) = default;
  ScopedTimer& operator=(ScopedTimer&&) = default;
#ifdef NDEBUG
  explicit ScopedTimer(const std::string & /* operation_name */) {}  // NOLINT(hicpp-named-parameter,readability-named-parameter)
#else
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
    } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486)
    }
  }

private:
  std::string operation_name_;  // NOLINT(readability-identifier-naming)
  std::chrono::high_resolution_clock::time_point start_time_;  // NOLINT(readability-identifier-naming)
#endif  // NDEBUG
};

void LogBenchmarkDuration(std::string_view operation_name,
                          uint64_t elapsed_microseconds);

// RAII-based benchmark timer active in both Debug and Release builds.
class ScopedBenchmarkTimer {  // NOSONAR(cpp:S3624)
public:
  ScopedBenchmarkTimer(const ScopedBenchmarkTimer&) = default;
  ScopedBenchmarkTimer& operator=(const ScopedBenchmarkTimer&) = default;
  ScopedBenchmarkTimer(ScopedBenchmarkTimer&&) = default;
  ScopedBenchmarkTimer& operator=(ScopedBenchmarkTimer&&) = default;

  explicit ScopedBenchmarkTimer(std::string operation_name)
      : operation_name_(std::move(operation_name)) {}

  ~ScopedBenchmarkTimer() {
    LogBenchmarkDuration(operation_name_, ElapsedMicroseconds());
  }

  [[nodiscard]] uint64_t ElapsedMicroseconds() const {
    const auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::high_resolution_clock::now() - start_time_);
    return static_cast<uint64_t>(duration.count());
  }

  [[nodiscard]] std::string_view OperationName() const { return operation_name_; }

 private:
  std::string operation_name_;  // NOLINT(readability-identifier-naming)
  std::chrono::high_resolution_clock::time_point start_time_ =  // NOLINT(readability-identifier-naming)
      std::chrono::high_resolution_clock::now();
};
