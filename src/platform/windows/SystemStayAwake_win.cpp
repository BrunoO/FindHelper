#include "platform/SystemStayAwake.h"

#include "utils/Logger.h"

#include <windows.h>  // NOSONAR(cpp:S3806) - lowercase include path per project standard (AGENTS.md)

namespace system_stay_awake {

void BeginSession() {
  const EXECUTION_STATE state =
      ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED;
  if (SetThreadExecutionState(state) == 0) {
    LOG_WARNING_BUILD("SetThreadExecutionState failed: could not request system/display awake state");
  } else {
    LOG_IMPORTANT_BUILD("Windows: preventing idle sleep and display power-down while app runs (test build)");
  }
}

void EndSession() {
  (void)SetThreadExecutionState(ES_CONTINUOUS);
}

}  // namespace system_stay_awake
