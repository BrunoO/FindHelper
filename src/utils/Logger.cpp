#include "utils/Logger.h"

#include "utils/StringUtils.h"

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

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Must be first: provides types for Windows/Shell/PSAPI headers
#include <direct.h>
#include <psapi.h>    // NOLINT(llvm-include-order) - must follow windows.h on MSVC
#include <shlobj.h>   // NOLINT(llvm-include-order) - must follow windows.h on MSVC
#else
#include <climits>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#include <mach/mach.h>
#endif  // __APPLE__
#endif  // _WIN32

namespace {
#ifndef _WIN32
// Security: Use 0700 (rwx------) to restrict log directory to owner only
constexpr mode_t kDirectoryPermissions = 0700;
// Security: Use 0600 (rw-------) to restrict log file to owner only
constexpr mode_t kLogFilePermissions = 0600;
#endif  // _WIN32
}  // namespace

Logger &Logger::Instance() {
  static Logger instance;  // NOSONAR(cpp:S6018) - Function-local static is correct pattern for singleton
  return instance;
}

void Logger::SetMinLogLevel(LogLevel level) {
#ifdef LOGGING_ENABLED
  const std::scoped_lock lock{mutex_};
  min_log_level_ = level;
#endif  // LOGGING_ENABLED
}

Logger::LogLevel Logger::GetMinLogLevel() const {
#ifdef LOGGING_ENABLED
  return min_log_level_;
#else
  return LogLevel::LOG_IMPORTANT;
#endif  // LOGGING_ENABLED
}

void Logger::SetFlushAfterLog(bool flush) {
#ifdef LOGGING_ENABLED
  const std::scoped_lock lock{mutex_};
  flush_after_log_ = flush;
#endif  // LOGGING_ENABLED
}

void Logger::SetMaxLogFileSizeMB(size_t max_size_mb) {
#ifdef LOGGING_ENABLED
  const std::scoped_lock lock{mutex_};
  max_log_file_size_mb_ = max_size_mb;
#endif  // LOGGING_ENABLED
}

void Logger::SetLogMemoryOnErrorWarning(bool enable) {
#ifdef LOGGING_ENABLED
  const std::scoped_lock lock{mutex_};
  log_memory_on_error_warning_ = enable;
#endif  // LOGGING_ENABLED
}

void Logger::LogBestEffortFatal(std::string_view message) {
#ifdef LOGGING_ENABLED
  try {
    if (const std::unique_lock lock{mutex_, std::try_to_lock};
        lock.owns_lock() && log_file_.is_open()) {
      const auto now = std::chrono::system_clock::now();
      log_file_ << FormatTimestamp(now) << " [IMPORTANT] " << message << '\n';
      log_file_.flush();
    }
  } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486) - Fatal path must not throw
  }
#endif  // LOGGING_ENABLED
}

size_t Logger::GetPrivateMemoryBytes() const {
#ifdef LOGGING_ENABLED
#ifdef _WIN32
  PROCESS_MEMORY_COUNTERS_EX pmc = {0};
  pmc.cb = sizeof(pmc);
  if (GetProcessMemoryInfo(GetCurrentProcess(),
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),  // NOSONAR(cpp:S3630)
                           sizeof(pmc))) {
    return pmc.PrivateUsage;
  }
#elif defined(__APPLE__)
  task_basic_info_data_t task_info;
  mach_msg_type_number_t size = TASK_BASIC_INFO_COUNT;
  if (const kern_return_t result = ::task_info(mach_task_self(), TASK_BASIC_INFO,
                                     reinterpret_cast<task_info_t>(&task_info), &size); result == KERN_SUCCESS) {  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) NOSONAR(cpp:S3630)
    return task_info.resident_size;
  }
#elif defined(__linux__)
  if (std::ifstream status_file("/proc/self/status"); status_file.is_open()) {
    std::string line;
    bool exit_loop = false;
    while (!exit_loop && std::getline(status_file, line)) {
      if (line.compare(0, 6, "VmRSS:") != 0) {
        continue;
      }
      size_t pos = 6;
      while (pos < line.size() && (line[pos] == ' ' || line[pos] == '\t')) {
        ++pos;
      }
      if (pos >= line.size() || std::isdigit(static_cast<unsigned char>(line[pos])) == 0) {
        exit_loop = true;
        continue;
      }
      const char* start = line.c_str() + pos;
      char* end = nullptr;
      if (const unsigned long value_kb = std::strtoul(start, &end, 10);
          end != start && value_kb > 0) {
        return value_kb * 1024ULL;
      }
      exit_loop = true;
    }
  }
#endif  // _WIN32 / __APPLE__ / __linux__
#endif  // LOGGING_ENABLED
  return 0;
}

std::string Logger::FormatMemory(size_t bytes) const {
  return ::FormatMemory(bytes);
}

void Logger::LogMemory(std::string_view context) {  // NOLINT(readability-make-member-function-const)
#ifdef LOGGING_ENABLED
  if (LogLevel::LOG_INFO < min_log_level_) {
    return;
  }
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

std::string Logger::GetLogFilePath() const {
#ifdef LOGGING_ENABLED
  const std::scoped_lock lock{mutex_};
  return log_file_path_;
#else
  return {};
#endif  // LOGGING_ENABLED
}

void Logger::Log(LogLevel level, const std::string &message) {  // NOLINT(readability-make-member-function-const)
#ifdef LOGGING_ENABLED
  if (level < min_log_level_) {
    return;
  }

  try {
    const std::scoped_lock lock{mutex_};

    if (max_log_file_size_mb_ > 0 && log_file_.is_open()) {
      CheckAndRotateLogFile();
    }

    if (!log_file_.is_open() && !TryOpenLogFile()) {
      return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::string timestamp = FormatTimestamp(now);

    log_file_ << timestamp << " [" << LevelToString(level) << "] " << message;

    if (log_memory_on_error_warning_ &&
        (level == LogLevel::LOG_ERROR || level == LogLevel::LOG_IMPORTANT || level == LogLevel::LOG_WARNING)) {
      const size_t memory_bytes = GetPrivateMemoryBytes();
      if (memory_bytes > 0) {
        log_file_ << " [Memory: " << FormatMemory(memory_bytes) << "]";
      }
    }

    log_file_ << '\n';

    if (flush_after_log_) {
      log_file_.flush();
    }

    // NOLINTNEXTLINE(readability-redundant-nested-if)
    if (!log_file_.good()) {
      log_file_.clear();
      log_file_.close();
      if (!TryOpenLogFile()) {
        if (static bool logged_failure = false; !logged_failure) {
          std::cerr << "[Logger] ERROR: Log file write failed and file could not be reopened. Logging disabled." << '\n';
          logged_failure = true;
        }
        return;
      }
    }
  } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486)
  }
#endif  // LOGGING_ENABLED
}

std::string Logger::FormatTimestamp(const std::chrono::system_clock::time_point& now) const {
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

std::string Logger::ExtractFilename(std::string_view path) const {
  if (const size_t last_sep = path.find_last_of("\\/"); last_sep != std::string_view::npos) {
    return std::string(path.substr(last_sep + 1));
  }
  return std::string(path);
}

void Logger::ReportDirectoryError(std::string_view dir_path, int error_code) const {
  std::cerr << "[Logger] Warning: Failed to create log directory: "
            << dir_path << " (errno: " << error_code << ")" << '\n';
}

bool Logger::CreateDirectoryIfNeeded(std::string_view dir_path) const {
  const std::string dir_path_str(dir_path);
  int mkdir_result = 0;
#ifdef _WIN32
  mkdir_result = _mkdir(dir_path_str.c_str());
#else
  mkdir_result = mkdir(dir_path_str.c_str(), kDirectoryPermissions);
#endif  // _WIN32
  if (mkdir_result != 0 && errno != EEXIST) {
    ReportDirectoryError(dir_path, errno);
  }
  return true;
}

// NOLINTNEXTLINE(cppcoreguidelines-pro-type-member-init)
Logger::Logger() {
#ifdef LOGGING_ENABLED
#ifdef _WIN32
  bool consoleAttached = AttachConsole(ATTACH_PARENT_PROCESS);
#ifndef NDEBUG
  if (!consoleAttached && AllocConsole()) {
    consoleAttached = true;
    SetConsoleTitleA("The FindHelper Experiment - Debug Console");
  }
#endif  // !NDEBUG

  if (consoleAttached) {
    FILE* pCout = nullptr;
    FILE* pCerr = nullptr;
    freopen_s(&pCout, "CONOUT$", "w", stdout);
    freopen_s(&pCerr, "CONOUT$", "w", stderr);
    if (pCout != nullptr) {
      setvbuf(pCout, nullptr, _IONBF, 0);
    }
    if (pCerr != nullptr) {
      setvbuf(pCerr, nullptr, _IONBF, 0);
    }
  }
#endif  // _WIN32
  executable_name_ = GetExecutableName();
  session_id_ = BuildSessionId();

  if (TryOpenLogFile()) {
    std::string session_msg = "=== Logging session started ===";
    if (!executable_name_.empty()) {
      session_msg += " [Executable: " + executable_name_ + "]";
    }
    Log(LogLevel::LOG_IMPORTANT, session_msg);
    Log(LogLevel::LOG_IMPORTANT, "Log file: " + log_file_path_);
    LogMemory("session start");
  }
#endif  // LOGGING_ENABLED
}

Logger::~Logger() {
#ifdef LOGGING_ENABLED
  if (log_file_.is_open()) {
    try {
      LogMemory("session end");
      std::string session_msg = "=== Logging session ended ===";
      if (!executable_name_.empty()) {
        session_msg += " [Executable: " + executable_name_ + "]";
      }
      Log(LogLevel::LOG_IMPORTANT, session_msg);
      log_file_.flush();
    } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486)
    }
    try {
      log_file_.close();
    } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486)
    }
  }
#endif  // LOGGING_ENABLED
}

std::string Logger::GetDefaultLogDirectory() const {
#ifdef _WIN32
  char temp_path[MAX_PATH];  // NOSONAR(cpp:S5945)
  DWORD result = GetEnvironmentVariableA("TEMP", temp_path, MAX_PATH);
  if (result > 0 && result < MAX_PATH) {
    return std::string(temp_path);
  }

  const DWORD len = GetTempPathA(MAX_PATH, temp_path);
  if (len > 0 && len < MAX_PATH) {
    if (temp_path[len - 1] == '\\' || temp_path[len - 1] == '/') {
      temp_path[len - 1] = '\0';
    }
    return std::string(temp_path);
  }

  return "C:\\temp";
#else
  if (const char* xdg_cache = std::getenv("XDG_CACHE_HOME"); xdg_cache != nullptr && xdg_cache[0] != '\0') {  // NOLINT(concurrency-mt-unsafe)
    return {xdg_cache};
  }

  if (const char* home = std::getenv("HOME"); home != nullptr && home[0] != '\0') {  // NOLINT(concurrency-mt-unsafe)
    return std::string(home) + "/.cache";
  }

  return "/tmp";  // NOSONAR(cpp:S5443)
#endif  // _WIN32
}

bool Logger::TryOpenLogFile() {
  const std::string log_dir = GetDefaultLogDirectory();
  const std::string log_filename = BuildLogFileName();
#ifdef _WIN32
  std::string log_file = log_dir + "\\" + log_filename;
#else
  std::string log_file = log_dir + "/" + log_filename;
#endif  // _WIN32

  (void)CreateDirectoryIfNeeded(log_dir);

  log_file_.open(log_file, std::ios::app);
  if (!log_file_.is_open()) {
    log_file = log_filename;
    log_file_.open(log_file, std::ios::app);
  }

  if (log_file_.is_open()) {
    log_file_path_ = log_file;
#ifndef _WIN32
    chmod(log_file.c_str(), kLogFilePermissions);
#endif  // _WIN32
  }

  return log_file_.is_open();
}

void Logger::CheckAndRotateLogFile() {  // NOLINT(readability-make-member-function-const)
  if (!log_file_.is_open()) {
    return;
  }

  try {
    log_file_.flush();

    const std::streampos current_pos = log_file_.tellp();
    if (current_pos < 0) {
      return;
    }

    const size_t current_size_mb = static_cast<size_t>(current_pos) / logger_constants::kBytesPerMB;

    if (current_size_mb >= max_log_file_size_mb_) {
      const std::string current_log_file = log_file_path_;
      log_file_.close();

      const std::string backup_file = current_log_file + ".old";

      (void)std::remove(backup_file.c_str());  // NOLINT(cert-err33-c)

      if (std::rename(current_log_file.c_str(), backup_file.c_str()) != 0) {
        (void)std::fprintf(stderr, "[Logger] Log rotation rename failed: %s -> %s\n",
                           current_log_file.c_str(), backup_file.c_str());  // NOLINT(cert-err33-c)
      }

      log_file_.open(current_log_file, std::ios::app);
      if (log_file_.is_open()) {
        const auto now = std::chrono::system_clock::now();
        const std::string timestamp = FormatTimestamp(now);
        log_file_ << timestamp << " [INFO] === Log file rotated ===" << '\n';
        if (flush_after_log_) {
          log_file_.flush();
        }
      }
    }
  } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486)
  }
}

std::string Logger::LevelToString(LogLevel level) const {
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

std::string Logger::GetExecutableName() const {
#ifdef _WIN32
  char module_path[MAX_PATH];  // NOSONAR(cpp:S5945)
  if (DWORD result = GetModuleFileNameA(nullptr, module_path, MAX_PATH); result > 0 && result < MAX_PATH) {
    return ExtractFilename(std::string(module_path));
  }
#else
#ifdef __APPLE__
  char path[PATH_MAX];  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) NOSONAR(cpp:S5945)
  if (uint32_t size = sizeof(path); _NSGetExecutablePath(path, &size) == 0) {  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
    return ExtractFilename(path);  // NOLINT(cppcoreguidelines-pro-bounds-array-to-pointer-decay,hicpp-no-array-decay)
  }
#else  // Linux
  char path[PATH_MAX];  // NOLINT(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) NOSONAR(cpp:S5945)
  if (ssize_t count = readlink("/proc/self/exe", path, sizeof(path)); count != -1) {
    std::string exePath(path, count);
    return ExtractFilename(exePath);
  }
#endif  // __APPLE__
#endif  // _WIN32
  return "";
}

std::string Logger::BuildLogFileName() const {
  constexpr std::string_view kDefaultExecutableName = "FindHelper";
  const std::string_view executable_name = executable_name_.empty()
                                           ? kDefaultExecutableName
                                           : std::string_view(executable_name_);
  return std::string(executable_name) + "_" + session_id_ + ".log";
}

std::string Logger::BuildSessionId() const {
  const auto now = std::chrono::system_clock::now();
  const auto now_time_t = std::chrono::system_clock::to_time_t(now);

  std::tm tm_buf{};
#ifdef _WIN32
  localtime_s(&tm_buf, &now_time_t);
#else
  localtime_r(&now_time_t, &tm_buf);
#endif  // _WIN32

  std::ostringstream oss;
  oss << std::put_time(&tm_buf, "%Y%m%d_%H%M%S")
      << "_pid" << GetCurrentProcessId();
  return {oss.str()};
}

std::uint64_t Logger::GetCurrentProcessId() {
#ifdef _WIN32
  return static_cast<std::uint64_t>(::GetCurrentProcessId());
#else
  return static_cast<std::uint64_t>(::getpid());
#endif  // _WIN32
}

void LogBenchmarkDuration(std::string_view operation_name,
                          uint64_t elapsed_microseconds) {
  try {
    const double milliseconds = static_cast<double>(elapsed_microseconds) / 1000.0;
    std::ostringstream oss;
    oss << operation_name << " completed in ";
    if (milliseconds >= static_cast<double>(logger_constants::kMillisecondsPerSecond)) {
      const double seconds =
          milliseconds / static_cast<double>(logger_constants::kMillisecondsPerSecond);
      oss << std::fixed << std::setprecision(3) << seconds << " seconds";
    } else {
      oss << std::fixed << std::setprecision(1) << milliseconds << " ms";
    }
    LOG_IMPORTANT(oss.str());
  } catch (...) {  // NOLINT(bugprone-empty-catch) NOSONAR(cpp:S2738, cpp:S2486)
  }
}
