#pragma once

/**
 * @file SystemStayAwake.h
 * @brief Ask the OS to avoid idle sleep / display power-down while the session is active.
 *
 * Windows: SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED).
 * Other platforms: no-op (future: optional macOS IOPMAssertion / Linux idle inhibition).
 */

namespace system_stay_awake {

void BeginSession();
void EndSession();

}  // namespace system_stay_awake
