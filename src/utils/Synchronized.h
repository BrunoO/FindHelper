#pragma once

#include <mutex>
#include <type_traits>
#include <utility>

/**
 * @file Synchronized.h
 * @brief Thread-safe encapsulation container inspired by Rust's std::sync::Mutex<T>.
 *
 * In idiomatic Rust, a mutex owns the data it protects (Mutex<T>). In standard C++,
 * the mutex and data are typically declared as separate members, allowing accidental
 * unsynchronized access.
 *
 * Synchronized<T> encapsulates the data within the mutex itself. Access is only
 * possible through an RAII LockGuard or a WithLock functional callback.
 */

namespace utils {

template <typename T, typename MutexType = std::mutex>
class Synchronized {
 public:
  Synchronized() = default;

  template <typename Arg1, typename... Args,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<Arg1>, Synchronized>>>
  explicit Synchronized(Arg1&& arg1, Args&&... args)
      : data_(std::forward<Arg1>(arg1), std::forward<Args>(args)...) {}

  Synchronized(const Synchronized&) = delete;
  Synchronized& operator=(const Synchronized&) = delete;
  Synchronized(Synchronized&&) = delete;
  Synchronized& operator=(Synchronized&&) = delete;
  ~Synchronized() = default;

  /**
   * @brief RAII guard providing scoped exclusive access to the underlying data.
   */
  class LockGuard {
   public:
    LockGuard(MutexType& mtx, T& data) : lock_(mtx), data_(data) {}
    LockGuard(const LockGuard&) = delete;
    LockGuard& operator=(const LockGuard&) = delete;
    LockGuard(LockGuard&&) noexcept = default;
    LockGuard& operator=(LockGuard&&) = delete;
    ~LockGuard() = default;

    [[nodiscard]] T* operator->() noexcept { return &data_; }
    [[nodiscard]] const T* operator->() const noexcept { return &data_; }
    [[nodiscard]] T& operator*() noexcept { return data_; }
    [[nodiscard]] const T& operator*() const noexcept { return data_; }
    [[nodiscard]] T& Get() noexcept { return data_; }
    [[nodiscard]] const T& Get() const noexcept { return data_; }
    [[nodiscard]] std::unique_lock<MutexType>& GetLock() noexcept { return lock_; }

   private:
    std::unique_lock<MutexType> lock_;
    T& data_;
  };

  /**
   * @brief RAII guard providing scoped read-only access for a const Synchronized.
   */
  class ConstLockGuard {
   public:
    ConstLockGuard(MutexType& mtx, const T& data) : lock_(mtx), data_(data) {}
    ConstLockGuard(const ConstLockGuard&) = delete;
    ConstLockGuard& operator=(const ConstLockGuard&) = delete;
    ConstLockGuard(ConstLockGuard&&) noexcept = default;
    ConstLockGuard& operator=(ConstLockGuard&&) = delete;
    ~ConstLockGuard() = default;

    [[nodiscard]] const T* operator->() const noexcept { return &data_; }
    [[nodiscard]] const T& operator*() const noexcept { return data_; }
    [[nodiscard]] const T& Get() const noexcept { return data_; }
    [[nodiscard]] std::unique_lock<MutexType>& GetLock() noexcept { return lock_; }

   private:
    std::unique_lock<MutexType> lock_;
    const T& data_;
  };

  /**
   * @brief Acquire exclusive access to the underlying data.
   */
  [[nodiscard]] LockGuard Lock() {
    return LockGuard(mutex_, data_);
  }

  /**
   * @brief Acquire read-only access to the underlying data on a const instance.
   */
  [[nodiscard]] ConstLockGuard Lock() const {
    return ConstLockGuard(mutex_, data_);
  }

  /**
   * @brief Execute a callable with exclusive access to the data.
   *
   * @param func Callable accepting T&.
   * @return Result of invoking func(data_).
   */
  template <typename Func>
  decltype(auto) WithLock(Func&& func) {
    const std::scoped_lock lock(mutex_);
    return std::forward<Func>(func)(data_);
  }

  /**
   * @brief Execute a callable with read-only access to the data.
   *
   * @param func Callable accepting const T&.
   * @return Result of invoking func(data_).
   */
  template <typename Func>
  decltype(auto) WithLock(Func&& func) const {
    const std::scoped_lock lock(mutex_);
    return std::forward<Func>(func)(data_);
  }

 private:
  mutable MutexType mutex_;
  T data_;
};

}  // namespace utils
