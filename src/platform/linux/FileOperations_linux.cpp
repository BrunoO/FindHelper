/**
 * FileOperations_linux.cpp - Linux implementation of file operation utilities
 *
 * This file implements the Linux-specific file operation utilities declared in
 * FileOperations.h. All functions are synchronous and called from the UI thread,
 * using standard Linux utilities and APIs.
 *
 * Linux APIs/Tools used:
 * - xdg-open: For opening files with default applications and revealing in file manager
 * - xclip/xsel: For X11 clipboard operations
 * - wl-clipboard: For Wayland clipboard operations
 * - gio trash: For moving files to Trash (GNOME)
 * - GTK: For folder picker dialog
 *
 * THREADING MODEL:
 * - All functions must be called from the main thread (UI thread)
 * - Operations are synchronous and blocking, similar to Windows/macOS implementations
 *
 * STRING ENCODING:
 * - All functions accept UTF-8 strings (std::string)
 * - Paths are passed directly to system commands (which handle UTF-8)
 */

#include "platform/FileOperations.h"  // Common interface header

#include <array>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#include "utils/Logger.h"
#include "utils/PlatformTypes.h"
#include "utils/StringUtils.h"

namespace file_operations {

namespace internal {
  // Helper to check if a file exists at the given path
  bool FileExists(std::string_view path) {
    struct stat st = {};
    return (stat(std::string(path).c_str(), &st) == 0);
  }

  // Helper to escape shell special characters in a path
  // This is a simple implementation - escapes single quotes and wraps in quotes
  std::string EscapeShellPath(std::string_view path) {
    std::string escaped;
    escaped.reserve(path.length() + 2);
    escaped += '\'';
    for (const char c : path) {
      if (c == '\'') {
        // Escape single quote: ' -> '\''
        escaped += "'\\''";
      } else {
        escaped += c;
      }
    }
    escaped += '\'';
    return escaped;
  }

  // Helper to execute a command with arguments (avoids shell injection)
  // Returns true if command succeeded (exit code 0), false otherwise
  // Captures stderr and logs it on failure
  // arg can be nullptr for commands that only take a path (e.g., xdg-open)
  // arg is the first argument for commands like "gio open" (arg="open", path=file_path)
  //
  // Security: Uses execlp() with direct arguments (no shell interpretation), preventing command injection.
  // Path is validated by ValidatePath() before calling this function, ensuring no path traversal or null bytes.
  // Uses pipe() for stderr capture - no temp files or filesystem paths needed (eliminates temp directory attacks).
  bool ExecuteCommand(const char* program, const char* arg, const char* path) {
    // Security: Use pipe() instead of temp files to capture stderr
    // This eliminates all temp directory security concerns:
    // - No symlink attacks (no filesystem path)
    // - No TOCTOU race conditions (no file creation)
    // - No publicly writable directory issues (no temp directory used)
    std::array<int, 2> stderr_pipe = {};
    if (pipe(stderr_pipe.data()) == -1) {
      LOG_ERROR(std::string("Failed to create pipe for stderr capture: ") + program);
      return false;
    }

    const pid_t pid = fork();
    if (pid == -1) {
      close(stderr_pipe[0]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - std::array<int,2> pipe fds; index always 0 or 1
      close(stderr_pipe[1]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      LOG_ERROR(std::string("Failed to fork process for ") + program);
      return false;
    }

    if (pid == 0) {
      // Child process - execute the command
      // Redirect stderr to write end of pipe
      close(stderr_pipe[0]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - Close read end (not needed in child)
      dup2(stderr_pipe[1], STDERR_FILENO);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      close(stderr_pipe[1]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - Close original fd (STDERR_FILENO now points to pipe)

      if (arg != nullptr) {
        // Command with argument: e.g., "gio open /path/to/file"
        // Security: execlp() passes arguments directly to exec, no shell interpretation
        execlp(program, program, arg, path, nullptr);
      } else {
        // Command without argument: e.g., "xdg-open /path/to/file"
        // Security: execlp() passes arguments directly to exec, no shell interpretation
        execlp(program, program, path, nullptr);
      }
      // If execlp returns, it means there was an error (127 = exec failed, see exec(3))
      constexpr int kExecFailedExitCode = 127;
      _exit(kExecFailedExitCode);
    }

    // Parent process - close write end (child writes to it)
    close(stderr_pipe[1]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

    // Wait for child to complete
    int status = 0;  // Output parameter for waitpid
    if (waitpid(pid, &status, 0) == -1) {
      close(stderr_pipe[0]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      LOG_ERROR(std::string("Failed to wait for ") + program);
      return false;
    }

    // Check if command failed and log stderr if available
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      // Read stderr from pipe
      std::array<char, 256> buffer = {};
      const ssize_t bytes_read = read(stderr_pipe[0], buffer.data(), buffer.size() - 1);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
      close(stderr_pipe[0]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)

      if (bytes_read > 0) {
        buffer.at(static_cast<size_t>(bytes_read)) = '\0';
        // Truncate at first newline for cleaner log message
        if (char* newline = std::strchr(buffer.data(), '\n'); newline != nullptr) {
          *newline = '\0';
        }
        LOG_ERROR(std::string(program) + " failed: " + buffer.data());
      } else {
        LOG_ERROR(std::string(program) + " failed with exit code " + std::to_string(WEXITSTATUS(status)));
      }
      return false;
    }

    // Command succeeded - close read end of pipe
    close(stderr_pipe[0]);  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
    return true;
  }
} // namespace internal

void OpenFileDefault(std::string_view full_path) {
  bool path_valid = false;
  const std::string path = internal::ToValidatedPath(full_path, "OpenFileDefault", path_valid);
  if (!path_valid) {
    return;
  }
  LOG_INFO("Opening file: " + path);
  if (!internal::FileExists(path)) {
    LOG_ERROR("File does not exist: " + path);
    return;
  }
  if (internal::ExecuteCommand("xdg-open", nullptr, path.c_str())) {
    return;
  }
  if (internal::ExecuteCommand("gio", "open", path.c_str())) {
    return;
  }
  if (internal::ExecuteCommand("kde-open", nullptr, path.c_str())) {
    return;
  }
  if (internal::ExecuteCommand("exo-open", nullptr, path.c_str())) {
    return;
  }
  LOG_ERROR("Failed to open file with xdg-open, gio, kde-open, or exo-open: " + path);
}

void OpenParentFolder(std::string_view full_path) {
  bool path_valid = false;
  const std::string path = internal::ToValidatedPath(full_path, "OpenParentFolder", path_valid);
  if (!path_valid) {
    return;
  }
  LOG_INFO("Opening folder and selecting: " + path);
  if (!internal::FileExists(path)) {
    LOG_ERROR("File does not exist: " + path);
    return;
  }
  const std::filesystem::path file_path(path);
  const std::filesystem::path parent_dir = file_path.parent_path();
  if (parent_dir.empty()) {
    LOG_ERROR("Failed to get parent directory for: " + path);
    return;
  }
  const std::string parent_dir_str = parent_dir.string();
  if (!internal::ExecuteCommand("xdg-open", nullptr, parent_dir_str.c_str()) &&
      !internal::ExecuteCommand("gio", "open", parent_dir_str.c_str())) {
    LOG_ERROR("Failed to open parent folder: " + parent_dir_str);
  }
}

bool DeleteFileToRecycleBin(const std::string &full_path) {  // NOSONAR(cpp:S6010) - Parameter type must match cross-platform API in FileOperations.h (Windows implementation also uses std::string)
  // Input validation
  if (!internal::ValidatePath(full_path, "DeleteFileToRecycleBin")) {
    return false;
  }

  const std::filesystem::path file_path(full_path);
  LOG_INFO("Deleting file: " + full_path);

  // Check if file exists
  if (!internal::FileExists(full_path)) {
    LOG_ERROR("File does not exist: " + full_path);
    return false;
  }

  // Try gio trash first (GNOME, most common)
  // Use exec-family functions to avoid shell injection
  if (internal::ExecuteCommand("gio", "trash", full_path.c_str())) {
    return true;
  }

  // Fallback to manual trash implementation
  // Get XDG_DATA_HOME or default to ~/.local/share
  const char* xdg_data_home = std::getenv("XDG_DATA_HOME");  // NOLINT(concurrency-mt-unsafe) - getenv result used immediately; trash path from main/UI thread
  std::filesystem::path trash_base;
  if (xdg_data_home != nullptr) {
    trash_base = std::filesystem::path(xdg_data_home);
  } else {
    const char* home = std::getenv("HOME");  // NOLINT(concurrency-mt-unsafe) - getenv result used immediately; trash path from main/UI thread
    if (home == nullptr) {
      LOG_ERROR("Failed to get HOME directory for trash");
      return false;
    }
    trash_base = std::filesystem::path(home) / ".local" / "share";
  }

  const std::filesystem::path trash_files = trash_base / "Trash" / "files";
  const std::filesystem::path trash_info = trash_base / "Trash" / "info";

  // Create trash directories if they don't exist
  try {
    std::filesystem::create_directories(trash_files);
    std::filesystem::create_directories(trash_info);
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR("Failed to create trash directories: " + std::string(e.what()));
    return false;
  }

  // Get filename for trash
  const std::string filename = file_path.filename().string();

  // Handle filename conflicts (add number suffix if file exists in trash)
  std::filesystem::path trash_file_path = std::filesystem::path(trash_files) / filename;
  int counter = 1;
  while (std::filesystem::exists(trash_file_path)) {
    const std::filesystem::path& base_path = file_path;
    const std::string stem = base_path.stem().string();
    const std::string ext = base_path.extension().string();
    std::ostringstream new_name;
    new_name << stem << " " << counter << ext;
    trash_file_path = std::filesystem::path(trash_files) / new_name.str();
    counter++;
  }

  // Create .trashinfo file
  const std::filesystem::path info_file = trash_info / (trash_file_path.filename().string() + ".trashinfo");
  try {
    std::ofstream info(info_file);
    if (info.is_open()) {
      // Get current time in seconds since epoch
      const std::time_t now = std::time(nullptr);
      struct tm timeinfo = {};
      if (gmtime_r(&now, &timeinfo) == nullptr) {
        LOG_ERROR("gmtime_r failed for trashinfo DeletionDate");
        return false;
      }
      std::array<char, 32> date_str{};
      if (std::strftime(date_str.data(), date_str.size(), "%Y-%m-%dT%H:%M:%S", &timeinfo) == 0) {
        LOG_DEBUG("strftime failed for trashinfo; using fallback date");
        strcpy_safe(date_str.data(), date_str.size(), "1970-01-01T00:00:00");
      }

      // Get absolute path
      const std::filesystem::path abs_path = std::filesystem::absolute(full_path);
      const std::string abs_path_str = abs_path.string();

      // Write trashinfo file (FreeDesktop.org Trash specification)
      info << "[Trash Info]\n";
      info << "Path=" << abs_path_str << "\n";
      info << "DeletionDate=" << date_str.data() << "\n";
      info.close();
    } else {
      LOG_ERROR("Failed to create trashinfo file: " + info_file.string());
      return false;
    }
  } catch (const std::exception& e) {  // NOSONAR(cpp:S1181) - Catch-all safety net for filesystem operations (may throw various exception types)
    LOG_ERROR("Exception creating trashinfo file: " + std::string(e.what()));
    return false;
  }

  // Move file to trash
  try {
    std::filesystem::rename(full_path, trash_file_path);
    return true;
  } catch (const std::filesystem::filesystem_error& e) {
    LOG_ERROR("Failed to move file to trash: " + std::string(e.what()));
    // Clean up trashinfo file on failure
    std::filesystem::remove(info_file);  // info_file is already filesystem::path
    return false;
  }
}


} // namespace file_operations

