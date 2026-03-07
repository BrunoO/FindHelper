/**
 * FileOperations_mac.cpp - macOS implementation of file operation utilities
 *
 * This file implements the macOS-specific file operation utilities declared in
 * FileOperations.h. All functions are synchronous and called from the UI thread,
 * using NSWorkspace and NSPasteboard APIs to perform file operations.
 *
 * macOS APIs used:
 * - NSWorkspace: For opening files with default applications and revealing files in Finder
 * - NSPasteboard: For clipboard operations
 * - NSFileManager: For file deletion to Trash
 *
 * THREADING MODEL:
 * - All functions must be called from the main thread (UI thread) as NSWorkspace
 *   and NSPasteboard operations are not thread-safe.
 * - Operations are synchronous and blocking, similar to Windows implementation.
 *
 * STRING ENCODING:
 * - All functions accept UTF-8 strings (std::string) and convert to NSString
 *   internally using NSString::stringWithUTF8String.
 */

#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

#include "platform/FileOperations.h"  // Common interface header
#include "utils/ClipboardUtils.h"
#include "utils/Logger.h"
#include "utils/PlatformTypes.h"

#include <string>

namespace file_operations {

namespace internal {
  // Helper to convert std::string_view (UTF-8) to NSString
  // Returns autoreleased NSString, or nil on failure
  NSString* StringToNSString(std::string_view utf8_str) {
    if (utf8_str.empty()) {
      return nil;
    }
    // Convert to std::string for c_str() (NSString requires null-terminated string)
    std::string str(utf8_str);
    return [NSString stringWithUTF8String:str.c_str()];
  }

  // Helper to check if a file exists at the given path
  bool FileExists(std::string_view path) {
    NSString* ns_path = StringToNSString(path);
    if (!ns_path) {
      return false;
    }
    return [[NSFileManager defaultManager] fileExistsAtPath:ns_path];
  }
} // namespace internal

void OpenFileDefault(std::string_view full_path) {
  const std::string path(full_path);
  if (!internal::ValidatePath(path, "OpenFileDefault")) {
    return;
  }
  LOG_INFO("Opening file: " + path);
  NSString* ns_path = internal::StringToNSString(path);
  if (!ns_path) {
    LOG_ERROR("Failed to convert path to NSString: " + path);
    return;
  }
  if (!internal::FileExists(path)) {
    LOG_ERROR("File does not exist: " + path);
    return;
  }
  NSURL* file_url = [NSURL fileURLWithPath:ns_path];
  if (!file_url) {
    LOG_ERROR("Failed to create NSURL from path: " + path);
    return;
  }
  NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
  BOOL success = [workspace openURL:file_url];
  if (!success) {
    LOG_ERROR("Failed to open file: " + path);
  }
}

void OpenParentFolder(std::string_view full_path) {
  const std::string path(full_path);
  if (!internal::ValidatePath(path, "OpenParentFolder")) {
    return;
  }
  LOG_INFO("Opening folder and selecting: " + path);
  NSString* ns_path = internal::StringToNSString(path);
  if (!ns_path) {
    LOG_ERROR("Failed to convert path to NSString: " + path);
    return;
  }
  if (!internal::FileExists(path)) {
    LOG_ERROR("File does not exist: " + path);
    return;
  }
  NSURL* file_url = [NSURL fileURLWithPath:ns_path];
  if (!file_url) {
    LOG_ERROR("Failed to create NSURL from path: " + path);
    return;
  }
  NSURL* parent_url = [file_url URLByDeletingLastPathComponent];
  if (!parent_url) {
    LOG_ERROR("Failed to get parent directory for: " + path);
    return;
  }

  // Use NSWorkspace to reveal the file in Finder
  NSWorkspace* workspace = [NSWorkspace sharedWorkspace];
  NSArray* file_urls = @[file_url];
  [workspace activateFileViewerSelectingURLs:file_urls];
}

void CopyPathToClipboard(struct GLFWwindow* window, std::string_view full_path) {
  const std::string path(full_path);
  if (!internal::ValidatePath(path, "CopyPathToClipboard")) {
    return;
  }
  if (clipboard_utils::SetClipboardText(window, path)) {
    LOG_INFO("Copied path to clipboard: " + path);
  } else {
    LOG_ERROR("Failed to copy path to clipboard: " + path);
  }
}

bool DeleteFileToRecycleBin(const std::string &full_path) {
  // Input validation
  if (!internal::ValidatePath(full_path, "DeleteFileToRecycleBin")) {
    return false;
  }

  LOG_INFO("Deleting file: " + full_path);

  NSString* ns_path = internal::StringToNSString(full_path);
  if (!ns_path) {
    LOG_ERROR("Failed to convert path to NSString: " + full_path);
    return false;
  }

  // Check if file exists
  if (!internal::FileExists(full_path)) {
    LOG_ERROR("File does not exist: " + full_path);
    return false;
  }

  // Convert to NSURL
  NSURL* file_url = [NSURL fileURLWithPath:ns_path];
  if (!file_url) {
    LOG_ERROR("Failed to create NSURL from path: " + full_path);
    return false;
  }

  // Use NSFileManager to move file to Trash
  NSFileManager* file_manager = [NSFileManager defaultManager];
  NSError* error = nil;
  BOOL success = [file_manager trashItemAtURL:file_url
                              resultingItemURL:nil
                                         error:&error];

  if (!success) {
    NSString* error_msg = error ? [error localizedDescription] : @"Unknown error";
    LOG_ERROR("Failed to delete file: " + full_path + " Error: " +
              std::string([error_msg UTF8String]));
    return false;
  }

  return true;
}


} // namespace file_operations

