# Rationale for Futures Implementation Improvements

## Overview

This document outlines the rationale behind the recent changes to the `std::future`-based asynchronous attribute loading mechanism. The primary goals of these changes were to eliminate memory leaks, improve UI responsiveness, and make the sorting feature more robust, especially under rapid user input.

---

## The Problem with the Previous Implementation

The original implementation suffered from several issues, particularly when users performed rapid, consecutive sort operations (e.g., clicking different column headers in quick succession):

1.  **UI Freezes and Unresponsiveness**:
    - When a new sort was initiated while a previous one was still running, the `StartSortAttributeLoading` function would block the UI thread by calling `.wait()` on all existing futures.
    - This was intended to prevent memory corruption but resulted in a frozen UI, providing a poor user experience. For large result sets, this freeze could last for a noticeable duration.

2.  **Memory Leaks**:
    - The blocking cleanup mechanism was not foolproof. If a new search was triggered, the `GuiState` was cleared, but the futures from a previous sort operation might not have been properly waited for and cleaned up.
    - Abandoned `std::future` objects that are not handled (i.e., `.get()` or `.wait()` is not called) can lead to resource leaks, as the associated asynchronous tasks may hold onto resources indefinitely.

3.  **Race Conditions**:
    - There was a potential for race conditions where the results of an older, slower sort operation could overwrite the results of a newer one.
    - For example:
        1. User clicks "Size" (Sort A starts).
        2. Before Sort A finishes, the user clicks "Last Modified" (Sort B starts).
        3. Sort B finishes quickly and sorts the results.
        4. Sort A finally finishes and sorts the results again, overwriting the user's intended sort order.

---

## The New Implementation: A Non-Blocking, Cancellation-Based Approach

To address these issues, the implementation was refactored to use a non-blocking, cancellation-based approach with a generation counter.

### Key Components

1.  **`SortCancellationToken`**:
    - A simple struct holding a `std::shared_ptr<std::atomic<bool>>`.
    - This token is passed to each asynchronous task. Before performing any work, the task checks if the token has been canceled.
    - When a new sort operation begins, the token for the *previous* operation is marked as canceled, signaling all its running tasks to terminate early.

2.  **`SortGeneration`**:
    - A `uint64_t` counter (`sort_generation_`) is maintained in `GuiState`.
    - Each time a new sort is initiated, this counter is incremented. The new value uniquely identifies the new sort operation.
    - This generation number is captured by the completion logic.

### How it Works

1.  **Initiating a Sort (`StartSortAttributeLoading`)**:
    - **Non-Blocking with Safety**: The function waits briefly (max 10ms) for cancelled tasks to finish before clearing futures. This prevents use-after-free if results are replaced while tasks are running.
    - **Cancellation**: It immediately calls `.Cancel()` on the *current* `sort_cancellation_token_`, signaling all tasks from the previous sort to stop.
    - **Safe Future Cleanup**: It waits up to 10ms for cancelled futures to complete, then calls `.get()` to properly clean up resources. This ensures cancelled tasks exit before futures are destroyed.
    - **New Token and Generation**: After cleanup, it creates a new `SortCancellationToken` and increments the `sort_generation_` counter.
    - **Index-Based Access**: New tasks use index-based access to results instead of reference capture, with defensive bounds checking to prevent use-after-free.

2.  **Checking for Completion (`CheckSortAttributeLoadingAndSort`)**:
    - The function now accepts the `sort_generation` of the currently *expected* sort operation.
    - **Generation Check**: Before processing the results, it checks if the `sort_generation` passed to it matches the one currently stored in `GuiState`.
        - If they do not match, it means a newer sort has been initiated. The function immediately aborts, discarding the results of this now-outdated operation.
    - This prevents the race condition described above and ensures that only the results of the most recent user action are applied.

---

## Benefits of the New Implementation

1.  **Improved UI Responsiveness**:
    - The UI thread is no longer blocked waiting for previous futures to complete. The application remains fully responsive, even during rapid sorting of large result sets.

2.  **Elimination of Memory Leaks**:
    - By canceling old tasks, waiting briefly for them to finish, and calling `.get()` to clean up futures, we ensure proper resource cleanup. The brief wait (max 10ms) gives cancelled tasks time to see the cancellation flag and exit, preventing abandoned futures from holding resources indefinitely.

3.  **Robustness and Correctness**:
    - The generation counter (`SortGeneration`) guarantees that the UI state will always reflect the user's most recent action, preventing stale or incorrect results from overwriting the current state.

4.  **Memory Safety**:
    - Tasks use index-based access to results with defensive bounds checking, preventing use-after-free if the results vector is replaced. Multiple cancellation checks ensure tasks exit promptly when cancelled.
    
5.  **Balanced Approach**:
    - The cleanup logic balances safety and performance: a brief 10ms wait ensures cancelled tasks exit cleanly, while remaining non-blocking for the UI thread. This is much faster than the previous blocking approach while maintaining memory safety.

---

## Summary

The new implementation is a significant improvement over the previous one. It provides a more robust, efficient, and user-friendly sorting experience by replacing a blocking cleanup mechanism with a non-blocking, cancellation-based approach. This resolves the core issues of UI freezes and memory leaks while also preventing race conditions, leading to a more stable and reliable application.
