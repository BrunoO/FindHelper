#include "platform/windows/CrashHandler_win.h"

#include <array>
#include <cstdio>
#include <string>

#include <windows.h>  // NOSONAR(cpp:S3806) - lowercase include path per AGENTS.md; SDK on-disk names vary
#include <dbghelp.h>  // NOSONAR(cpp:S3806)

namespace {

LONG WriteMinidump(EXCEPTION_POINTERS* exception_pointers) {
  std::string module_path(MAX_PATH, '\0');
  DWORD module_path_len = GetModuleFileNameA(nullptr, module_path.data(), MAX_PATH);
  if (module_path_len == 0U || module_path_len >= MAX_PATH) {
    return EXCEPTION_EXECUTE_HANDLER;
  }
  module_path.resize(module_path_len);

  const std::string& exe_path = module_path;
  const size_t slash_pos = exe_path.find_last_of("\\/");
  const std::string exe_dir = (slash_pos == std::string::npos) ? "." : exe_path.substr(0, slash_pos);
  const std::string dump_dir = exe_dir + "\\crash-dumps";
  (void)CreateDirectoryA(dump_dir.c_str(), nullptr);

  SYSTEMTIME local_time{};
  GetLocalTime(&local_time);

  std::array<char, 128> dump_name{};
  if (const int name_len = std::snprintf(
          dump_name.data(), dump_name.size(),
          "FindHelper-crash-%04u%02u%02u-%02u%02u%02u-%lu.dmp",
          static_cast<unsigned>(local_time.wYear),
          static_cast<unsigned>(local_time.wMonth),
          static_cast<unsigned>(local_time.wDay),
          static_cast<unsigned>(local_time.wHour),
          static_cast<unsigned>(local_time.wMinute),
          static_cast<unsigned>(local_time.wSecond),
          GetCurrentProcessId());
      name_len <= 0 || static_cast<size_t>(name_len) >= dump_name.size()) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  const std::string dump_path = dump_dir + "\\" + std::string(dump_name.data());
  HANDLE dump_file = CreateFileA(
      dump_path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (dump_file == INVALID_HANDLE_VALUE) {
    return EXCEPTION_EXECUTE_HANDLER;
  }

  MINIDUMP_EXCEPTION_INFORMATION exception_info{};
  exception_info.ThreadId = GetCurrentThreadId();
  exception_info.ExceptionPointers = exception_pointers;
  exception_info.ClientPointers = FALSE;

  BOOL dump_result = MiniDumpWriteDump(
      GetCurrentProcess(), GetCurrentProcessId(), dump_file, MiniDumpWithThreadInfo,
      exception_pointers != nullptr ? &exception_info : nullptr, nullptr, nullptr);
  if (dump_result == FALSE) {
    dump_result = MiniDumpWriteDump(
        GetCurrentProcess(), GetCurrentProcessId(), dump_file, MiniDumpNormal,
        exception_pointers != nullptr ? &exception_info : nullptr, nullptr, nullptr);
  }
  (void)dump_result;
  CloseHandle(dump_file);

  return EXCEPTION_EXECUTE_HANDLER;
}

LONG WINAPI UnhandledExceptionFilterCallback(EXCEPTION_POINTERS* exception_pointers) {
  return WriteMinidump(exception_pointers);
}

}  // namespace

namespace crash_handler {

void InstallCrashHandler() {
  (void)SetUnhandledExceptionFilter(UnhandledExceptionFilterCallback);
}

}  // namespace crash_handler
