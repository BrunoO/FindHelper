# Regex and Glob Search Implementation

I have successfully implemented **Regex** and **Glob** (wildcard) search capabilities for the Find Helper application.

## Changes

### 1. New Regex Library (`SimpleRegex.h`)
I created a lightweight, header-only library `SimpleRegex.h` that provides:
*   **`RegExMatch`**: A simple regex matcher based on Rob Pike's implementation. Supports `.` (any char), `*` (repetition), `^` (start), and `$` (end).
*   **`GlobMatch`**: A standard wildcard matcher. Supports `*` (any sequence) and `?` (single char).

### 2. Search Worker Integration (`SearchWorker.cpp`)
I updated `SearchWorker.cpp` to use a heuristic approach for selecting the matching algorithm:
*   **Regex**: Triggered if the search term starts with `re:`.
*   **Glob**: Triggered if the search term contains `*` or `?`.
*   **Standard**: Default substring match (optimized).

This logic is applied to **both** "Item name contains" and "Path contains" fields.

### 3. GUI Updates (`main_gui.cpp`)
I added a **Search Syntax Help** feature:
*   Added `(?)` markers next to the "Item name contains" and "Path contains" input fields.
*   Clicking these markers opens a modal popup explaining the syntax for Standard, Glob, and Regex searches.

### 4. Zero File Size Fix (`SearchWorker.cpp`)
I resolved an issue where file sizes were often displayed as 0 B.
*   **Cause**: During initial index population, files are often indexed before their parent directories. This prevents the full path from being computed immediately, causing `GetFileSize` to fail (return 0) during insertion.
*   **Fix**: Modified `SearchWorker` to lazily fetch the file size from disk if the cached size is 0. This ensures correct file sizes are displayed in search results without slowing down the initial indexing process.

### 5. Unicode Path Support (`StringUtils.h`)
I resolved an issue where file sizes were 0 for files with non-ASCII characters (common in OneDrive paths).
*   **Cause**: The application was using the ANSI version of `GetFileAttributesExA`, which fails for paths containing characters not in the system code page.
*   **Fix**: Updated `GetFileSize` to convert UTF-8 paths to UTF-16 (Wide Strings) and use `GetFileAttributesExW`. This ensures correct file access for all paths, including international characters and deep OneDrive structures.

### 6. Long Path Support (`main_gui.cpp`)
I enabled support for Windows long paths (> 260 characters).
*   **Cause**: The application was using `MAX_PATH` (260 char) buffers for environment variable retrieval, which would truncate long paths.
*   **Fix**: Replaced all `MAX_PATH` buffers with 32767-character buffers (the Windows long path limit). This ensures the Quick Filter buttons (Desktop, Downloads, Current User) work correctly even with deeply nested OneDrive folders or long usernames.

## Verification

### Automated Tests
I created a temporary test file `test_regex.cpp` to verify the correctness of `SimpleRegex.h`.
*   Verified Regex patterns: `^...$`, `.*`, etc.
*   Verified Glob patterns: `*.cpp`, `test?.txt`.
*   All tests passed.

### Manual Verification Plan
1.  **Build the application**.
2.  **Test Standard Search**: Type `log` -> Should find `changelog.txt`.
3.  **Test Glob Search**: Type `*.cpp` -> Should find all C++ source files.
4.  **Test Regex Search**: Type `re:^main` -> Should find files starting with `main`.
5.  **Test Path Search**: Type `*test*` in the Path field -> Should find files in folders containing "test".
6.  **Test Help Popup**: Click the `(?)` button -> Should show the help dialog.
7.  **Test File Sizes**: Search for files and verify that the "Size" column displays correct values (e.g., "1.2 KB") instead of "0 B".
