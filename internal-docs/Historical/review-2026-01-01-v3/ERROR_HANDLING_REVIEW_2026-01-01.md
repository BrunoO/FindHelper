# Error Handling Review - 2026-01-01

## Reliability Score: 7/10

The application's error handling is generally robust, with good use of RAII for Windows handles (`VolumeHandle`) and structured exception handling in the main threads. However, there are some areas where error handling could be improved, particularly in the `UsnMonitor` class, where there is a risk of resource leaks on certain error paths.

## Findings

### 1. Potential Resource Leak in `UsnMonitor::ReaderThread` (Medium)

*   **Location**: `UsnMonitor.cpp`, `UsnMonitor::ReaderThread`
*   **Error Type**: Resource Cleanup (Handle Leaks)
*   **Failure Scenario**: If the call to `DeviceIoControl` to query the USN journal fails, the `init_promise_` is set to `false`, and the thread returns. However, the `VolumeHandle` is a local variable in the thread, and its destructor will be called, closing the handle. The problem is that the `UsnMonitor` object itself is not stopped, and the `Stop()` method will not be called, leaving the processor thread in a running state.
*   **Current Behavior**: The reader thread exits, but the processor thread continues to run, waiting indefinitely on the queue. The `UsnMonitor` object remains in a partially active state.
*   **Expected Behavior**: If a fatal error occurs in the reader thread, the entire `UsnMonitor` should be stopped gracefully, ensuring that all threads are joined and resources are released.
*   **Fix**: Instead of just returning from the `ReaderThread` on a fatal error, the thread should call `Stop()` on the `UsnMonitor` to trigger a clean shutdown.
    ```cpp
    // In UsnMonitor::ReaderThread()
    if (!DeviceIoControl(...)) {
      // ...
      monitoring_active_.store(false, std::memory_order_release);
      init_promise_.set_value(false);
      Stop(); // Trigger a full shutdown
      return;
    }
    ```
*   **Severity**: Medium

### 2. Unhandled Exception in `main` (Low)

*   **Location**: `main_win.cpp`
*   **Error Type**: Exception Safety
*   **Failure Scenario**: If an unhandled exception (e.g., `std::bad_alloc`) occurs during the application's execution, the program will terminate without any logging or user notification.
*   **Current Behavior**: The application crashes with a standard Windows error message.
*   **Expected Behavior**: All exceptions should be caught at the top level, logged, and a user-friendly error message should be displayed before the application terminates.
*   **Fix**: Wrap the call to `RunApplication` in `main` with a `try...catch` block that catches `std::exception` and other potential exceptions.
    ```cpp
    // In main()
    try {
      return RunApplication<AppBootstrapResult, WindowsBootstrapTraits>(argc, argv);
    } catch (const std::exception& e) {
      LOG_ERROR("Unhandled exception: " << e.what());
      // Display a message box to the user
      return 1;
    } catch (...) {
      LOG_ERROR("Unknown unhandled exception");
      // Display a message box to the user
      return 1;
    }
    ```
*   **Severity**: Low

## Logging Improvements

*   **Add More Context to Log Messages**: In `UsnMonitor.cpp`, when `DeviceIoControl` fails, the error is logged, but it would be more helpful to also log the volume path that was being accessed.
*   **Log Dropped Buffers More Consistently**: The `ReaderThread` logs a warning when the USN queue is full and a buffer is dropped, but it only does so every `kDropLogInterval` drops. It would be better to log the first drop immediately and then throttle subsequent messages.

## Recovery Recommendations

*   **Implement a "Restart Monitoring" Feature**: If a fatal error occurs in the `UsnMonitor`, the application should provide a way for the user to restart the monitoring without having to restart the entire application. This would improve the application's resilience to transient errors.

## Testing Suggestions

*   **Inject Errors into Windows API Calls**: Create a mock version of the Windows API functions (like `DeviceIoControl` and `CreateFile`) that can be configured to fail in specific ways. This would allow the error handling paths in `UsnMonitor` to be tested more thoroughly.
*   **Test with a Full USN Queue**: Write a test that deliberately fills up the USN queue to ensure that the "queue full" logic is working correctly and that no deadlocks or race conditions occur.
