#pragma once

#ifdef _WIN32

#include <windows.h>  // NOSONAR(cpp:S3806) - Windows-only include, case doesn't matter on Windows filesystem

/**
 * @file ScopedHandle.h
 * @brief RAII wrapper for Windows HANDLE objects
 *
 * This class ensures that Windows HANDLEs are automatically closed when they
 * go out of scope, preventing resource leaks even if exceptions are thrown
 * or early returns occur.
 *
 * Usage:
 *   ScopedHandle hToken;
 *   if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
 *     return; // Handle automatically closed on return
 *   }
 *   // Use hToken...
 *   // Handle automatically closed when hToken goes out of scope
 *
 * Thread Safety:
 *   - Not thread-safe (HANDLEs are typically not shared across threads)
 *   - Each thread should have its own ScopedHandle instance
 *
 * Exception Safety:
 *   - Destructor is noexcept and handles exceptions internally
 *   - Safe to use in exception-throwing code paths
 */

class ScopedHandle {
public:
  /**
   * @brief Construct with an existing HANDLE or INVALID_HANDLE_VALUE
   * @param handle The HANDLE to manage (defaults to INVALID_HANDLE_VALUE)
   */
  explicit ScopedHandle(HANDLE handle = INVALID_HANDLE_VALUE) noexcept
      : handle_(handle) {}

  /**
   * @brief Destructor - automatically closes the handle
   *
   * The destructor is noexcept and catches any exceptions from CloseHandle()
   * to prevent std::terminate during stack unwinding.
   */
  ~ScopedHandle() noexcept {
    Reset();
  }

  // Non-copyable (HANDLEs should not be duplicated)
  ScopedHandle(const ScopedHandle &) = delete;
  ScopedHandle &operator=(const ScopedHandle &) = delete;

  // Movable (transfer ownership of HANDLE)
  ScopedHandle(ScopedHandle &&other) noexcept : handle_(other.handle_) {
    other.handle_ = INVALID_HANDLE_VALUE;
  }

  ScopedHandle &operator=(ScopedHandle &&other) noexcept {
    if (this != &other) {
      Reset();
      handle_ = other.handle_;
      other.handle_ = INVALID_HANDLE_VALUE;
    }
    return *this;
  }

  /**
   * @brief Get the underlying HANDLE value
   * @return The HANDLE value
   */
  HANDLE Get() const noexcept { return handle_; }

  /**
   * @brief Get a pointer to the HANDLE (for APIs that take HANDLE*)
   * @return Pointer to the internal HANDLE
   */
  HANDLE *GetAddressOf() noexcept { return &handle_; }

  /**
   * @brief Check if the handle is valid
   * @return true if handle is not INVALID_HANDLE_VALUE, false otherwise
   */
  bool IsValid() const noexcept {
    return handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr;
  }

  /**
   * @brief Explicit conversion to HANDLE for convenience
   * @return The HANDLE value
   */
  explicit operator HANDLE() const noexcept { return handle_; }

  /**
   * @brief Reset the handle (close current and set to INVALID_HANDLE_VALUE)
   */
  void Reset() noexcept {
    if (handle_ != INVALID_HANDLE_VALUE && handle_ != nullptr) {
      try {
        CloseHandle(handle_);
      } catch (...) {  // NOSONAR(cpp:S2738, cpp:S2486) - Destructors must not throw. Cannot log here (generic utility, Logger not included; preventing std::terminate is the only goal).
      }
      handle_ = INVALID_HANDLE_VALUE;
    }
  }

  /**
   * @brief Release ownership of the handle (returns handle and sets internal to INVALID_HANDLE_VALUE)
   * @return The HANDLE value (caller is now responsible for closing it)
   */
  HANDLE Release() noexcept {
    HANDLE temp = handle_;
    handle_ = INVALID_HANDLE_VALUE;
    return temp;
  }

private:
  HANDLE handle_;
};

#endif  // _WIN32

