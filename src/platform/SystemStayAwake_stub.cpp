#include "platform/SystemStayAwake.h"

namespace system_stay_awake {

void BeginSession() {
  /* Non-Windows build: sleep/display stay-awake is not implemented here;
     Windows uses SetThreadExecutionState in SystemStayAwake_win.cpp. */
}

void EndSession() {
  /* Non-Windows build: no session to tear down (BeginSession is a no-op). */
}

}  // namespace system_stay_awake
