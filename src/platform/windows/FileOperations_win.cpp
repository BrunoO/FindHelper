/**
 * FileOperations.cpp - Implementation of Windows Shell file operation utilities
 *
 * This file implements the file operation utilities declared in FileOperations.h.
 * All functions are synchronous and called from the UI thread, using Windows Shell
 * APIs to perform file operations. See FileOperations.h for detailed documentation
 * on threading model, design trade-offs, and future improvements.
 */

#include "platform/FileOperations.h"

#ifdef _WIN32
#include <windows.h>  // NOSONAR(cpp:S3806) - Must be first: provides types for Shell headers
#include <shellapi.h>
#include <shlobj_core.h>  // NOSONAR(cpp:S3806) - After windows.h
#endif  // _WIN32

#include <vector>

#include "utils/Logger.h"
#include "utils/LoggingUtils.h"
#include "utils/PlatformTypes.h"
#include "utils/StringUtils.h"

namespace file_operations {

namespace internal {
  // ShellExecuteW returns values <= 32 on error
  // Common error codes: 0 (out of memory), 2 (file not found), 3 (path not found)
  // Reference: https://docs.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shellexecutew
  constexpr INT_PTR kShellExecuteErrorThreshold = 32;

  // Helper to check if ShellExecuteW result indicates an error
  bool IsShellExecuteError(HINSTANCE h_inst) {
    return reinterpret_cast<INT_PTR>(h_inst) <= kShellExecuteErrorThreshold;
  }

  // SHFileOperationW error codes (from Windows Shell API)
  // Reference: https://docs.microsoft.com/en-us/windows/win32/api/shellapi/nf-shellapi-shfileoperationw
  constexpr int kErrorSourceDestSame = 0x71;
  constexpr int kErrorMultipleFiles = 0x72;
  constexpr int kErrorDestIsSubtree = 0x73;
  constexpr int kErrorAnsiUnicodeMismatch = 0x74;
  constexpr int kErrorPathTooLong = 0x75;
  constexpr int kErrorPathNotFound = 0x76;
  constexpr int kErrorDestReadOnly = 0x78;
  constexpr int kErrorOperationCancelled = 0x79;
  constexpr int kErrorOutOfMemory = 0x7A;
  constexpr int kErrorInvalidPath = 0x7C;

  const char* GetFileOperationErrorString(int error_code) {
    switch (error_code) {
      case kErrorSourceDestSame: return "Source and destination are the same";
      case kErrorMultipleFiles: return "Multiple files specified";
      case kErrorDestIsSubtree: return "Destination is a subtree of source";
      case kErrorAnsiUnicodeMismatch: return "Ansi/Unicode mismatch";
      case kErrorPathTooLong: return "Path too long";
      case kErrorPathNotFound: return "Path not found";
      case kErrorDestReadOnly: return "Destination is read-only";
      case kErrorOperationCancelled: return "Operation cancelled";
      case kErrorOutOfMemory: return "Out of memory";
      case kErrorInvalidPath: return "Invalid path";
      default: return "Unknown error";
    }
  }
} // namespace internal

void OpenFileDefault(std::string_view full_path) {
  bool path_valid = false;
  const std::string path = internal::ToValidatedPath(full_path, "OpenFileDefault", path_valid);
  if (!path_valid) {
    return;
  }
  if (!internal::IsPathSafe(path)) {
    LOG_ERROR_BUILD("OpenFileDefault: Rejected potentially unsafe path: " << path);
    return;
  }
  LOG_INFO_BUILD("Opening file: " << path);
  const std::wstring wide_path = Utf8ToWide(path);

  // Validate conversion result
  if (wide_path.empty() && !full_path.empty()) {
    LOG_ERROR_BUILD("OpenFileDefault failed: Failed to convert path to wide string. Path: "
                    << path
                    << ". This may indicate invalid UTF-8 encoding.");
    return;
  }

  HINSTANCE h_result = ShellExecuteW(nullptr, L"open", wide_path.c_str(), nullptr,
                                     nullptr, SW_SHOWNORMAL);
  // Check if it failed (returns value <= 32 on error)
  if (internal::IsShellExecuteError(h_result)) {
    // Try with "edit" verb for documents
    HINSTANCE h_result_edit = ShellExecuteW(nullptr, L"edit", wide_path.c_str(), nullptr, nullptr,
                                             SW_SHOWNORMAL);
    if (internal::IsShellExecuteError(h_result_edit)) {
      auto open_err = reinterpret_cast<INT_PTR>(h_result);
      auto edit_err = reinterpret_cast<INT_PTR>(h_result_edit);
      LOG_ERROR_BUILD("OpenFileDefault failed: Failed to open file with both 'open' and 'edit' verbs. Path: "
                      << path
                      << ", Open error code: " << open_err
                      << ", Edit error code: " << edit_err);
    }
  }
}

void OpenParentFolder(std::string_view full_path) {
  bool path_valid = false;
  const std::string path = internal::ToValidatedPath(full_path, "OpenParentFolder", path_valid);
  if (!path_valid) {
    return;
  }
  LOG_INFO("Opening folder and selecting: " + path);
  const std::wstring wide_path = Utf8ToWide(path);
  if (wide_path.empty() && !path.empty()) {
    LOG_ERROR("OpenParentFolder failed: Failed to convert path to wide string. Path: " + path);
    return;
  }

  PIDLIST_ABSOLUTE pidl = ILCreateFromPathW(wide_path.c_str());
  if (pidl) {
    HRESULT hr = SHOpenFolderAndSelectItems(pidl, 0, nullptr, 0);
    if (FAILED(hr)) {
      const std::string context_message = std::string("Path: ") + path;
      logging_utils::LogHResultError("SHOpenFolderAndSelectItems", context_message, hr);
    }
    ILFree(pidl);
  } else {
    DWORD err = GetLastError();
    const std::string context_message = std::string("Path: ") + path;
    logging_utils::LogWindowsApiError("ILCreateFromPathW", context_message, err);
  }
}

bool DeleteFileToRecycleBin(const std::string &full_path) {
  // Input validation
  if (!internal::ValidatePath(full_path, "DeleteFileToRecycleBin")) {
    return false;
  }

  LOG_INFO("Deleting file: " + full_path);
  const std::wstring wide_path = Utf8ToWide(full_path);

  // Validate conversion result
  if (wide_path.empty() && !full_path.empty()) {
    LOG_ERROR("DeleteFileToRecycleBin failed: Failed to convert path to wide string. Path: " + full_path);
    return false;
  }

  SHFILEOPSTRUCTW file_op{};
  file_op.hwnd = nullptr;
  file_op.wFunc = FO_DELETE;
  // Double-null terminated string is required
  std::vector<wchar_t> path_buffer;
  path_buffer.resize(wide_path.size() + 2); // Resize to include space for 2 null terminators
  std::copy(wide_path.begin(), wide_path.end(), path_buffer.begin());  // NOLINT(llvm-use-ranges) - C++17; std::ranges requires C++20
  path_buffer[wide_path.size()] = L'\0';      // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access) - vector resized to wide_path.size()+2 above
  path_buffer[wide_path.size() + 1] = L'\0';  // NOLINT(cppcoreguidelines-pro-bounds-avoid-unchecked-container-access)
  file_op.pFrom = path_buffer.data();
  file_op.pTo = nullptr;  // Not used for delete operation
  file_op.fFlags = FOF_ALLOWUNDO | FOF_NOCONFIRMATION | FOF_SILENT;

  int result = SHFileOperationW(&file_op);
  if (result == 0) {
    return true;
  }
  const char* error_msg = internal::GetFileOperationErrorString(result);
  LOG_ERROR("Failed to delete file: " + full_path +
            " Error: " + std::to_string(result) + " (" + error_msg + ")");
  return false;
}

} // namespace file_operations
