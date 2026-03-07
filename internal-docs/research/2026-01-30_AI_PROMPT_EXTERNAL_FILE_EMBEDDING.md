# 2026-01-30 — Extracting AI Prompt to External File with Single-Executable Embedding

## Goal

- Move the AI search-config prompt out of `GeminiApiUtils.cpp` into a separate text file for easier editing.
- Keep a **single executable** (no runtime dependency on external files); embed the prompt at build time.
- Support **all platforms**: Windows, macOS, Linux.

## Current State

- The prompt lives in `BuildSearchConfigPrompt()` in `src/api/GeminiApiUtils.cpp` (approx. lines 216–331).
- It is built from a long raw string literal with interpolated values: OS name, home path, index root, desktop path, downloads path, and user description.
- Any change requires editing C++ and recompiling.

## Requirements

| Requirement | Notes |
|-------------|--------|
| Easier to modify | Non-developers can edit a `.txt` (or `.md`) file. |
| Single executable | No external prompt file required at runtime; embed at build time. |
| Cross-platform | Same approach on Windows, macOS, Linux. |
| No new runtime deps | No extra libraries; only build-time tooling. |

## Approach: Build-Time Embedding via Generated Source

**Idea:** Keep the prompt as a **template file** (e.g. `resources/prompts/search_config_prompt.txt`) with placeholders. At **configure or build time**, generate a small C++ file that defines the template as a `const char[]` (or `std::string_view`). That generated file is compiled like any other source, so the prompt is embedded in the executable. No linker tricks, no platform-specific resource systems.

### Placeholders

The current prompt has dynamic parts. Replace them with placeholders so the whole text lives in one file:

| Placeholder       | Current source              |
|-------------------|-----------------------------|
| `{{OS}}`          | `os_name`                   |
| `{{HOME}}`        | `path_utils::GetUserHomePath()` |
| `{{INDEX_ROOT}}`  | `path_utils::GetDefaultVolumeRootPath()` |
| `{{DESKTOP}}`     | `path_utils::GetDesktopPath()` |
| `{{DOWNLOADS}}`   | `path_utils::GetDownloadsPath()` |
| `{{USER_QUERY}}`  | User description + “Response (JSON only…)” line |

At runtime, `BuildSearchConfigPrompt()` (or a helper) loads the embedded template and replaces these placeholders; no file I/O.

### Embedding the Template in C++

Two practical options:

1. **Raw string literal**  
   Generate something like:
   ```cpp
   namespace gemini_api_utils {
   const char kSearchConfigPromptTemplate[] = R"PROMPT(
   ... template content ...
   )PROMPT";
   }
   ```
   Use a delimiter (e.g. `PROMPT` or `GEMINI_TEMPLATE`) that does **not** appear inside the template. The only restriction is that the template must not contain the exact sequence `)PROMPT"` (or whatever delimiter is chosen). The current prompt text does not, so this is safe.

2. **Hex/byte array**  
   Convert the file to a `const unsigned char[]` and expose length + pointer. No escaping issues, but the source is unreadable and tooling is slightly more involved.

**Recommendation:** Use a raw string literal in generated code. No escaping of quotes or newlines; only choose a delimiter that does not appear in the template.

## Build-Time Generation (Cross-Platform)

All three platforms can use the same strategy: **generate a `.cpp` from the `.txt` during the build**, then add that `.cpp` to the target. No `xxd`, no `objcopy`, no Windows `.rc` for this resource.

### Option A: Python script + `add_custom_command` (recommended)

- **Input:** `resources/prompts/search_config_prompt.txt`
- **Output:** e.g. `src/api/generated/SearchConfigPromptTemplate.cpp` (or a file under `build/`).
- **Script:** A small Python script that:
  1. Reads the template file.
  2. Wraps it in `R"DELIM(...)DELIM"` and writes a `.cpp` that defines `kSearchConfigPromptTemplate`.
- **CMake:** `add_custom_command(OUTPUT ... COMMAND python3 ...)` and list the output in the target’s sources. When the `.txt` changes, the `.cpp` is regenerated and recompiled.
- **Platforms:** Works on Windows, macOS, and Linux as long as Python 3 is available (which the project already uses for scripts).

### Option B: Pure CMake

- Use `file(READ ...)` to read the template and `string(REPLACE ...)` to escape backslashes and double quotes, then write a `.cpp` with a normal C string (e.g. `"line1\nline2\n"`).
- **Pros:** No Python dependency at build time.  
- **Cons:** CMake string handling is brittle (semicolons, quotes, backslashes, newlines); easy to get wrong and hard to debug. Less recommended unless the project must avoid Python at configure/build.

### Option C: External tools (xxd, objcopy, ld -b binary)

- **xxd -i:** Not standard on Windows; would require bundling or documenting it.  
- **objcopy / ld -b binary:** Common on Linux, possible on macOS with GNU binutils; not standard on Windows MSVC.  
- **Conclusion:** Not suitable for a “works on all supported platforms” requirement without extra portability layers.

### Recommendation

Use **Option A (Python + add_custom_command)** so that:

1. The prompt is in one text file under `resources/prompts/`.
2. A script generates a single `.cpp` with the template in a raw string literal.
3. That `.cpp` is compiled into the existing target (e.g. the main app and any test that links the same code). No new libraries, no runtime file loading.
4. The same CMake and script run on Windows, macOS, and Linux.

## “At Link Time” vs “Single Executable”

The goal was to “integrate at link time” and “generate a single executable.” Here we integrate at **compile time** (generated source is just another translation unit). The result is the same: one executable with the prompt inside it; no separate resource file at runtime. So this approach satisfies the distribution and simplicity requirements.

## Implementation Outline

1. **Add template file**  
   `resources/prompts/search_config_prompt.txt` with the current prompt text and placeholders `{{OS}}`, `{{HOME}}`, `{{INDEX_ROOT}}`, `{{DESKTOP}}`, `{{DOWNLOADS}}`, `{{USER_QUERY}}`.

2. **Add generator script**  
   e.g. `scripts/embed_prompt_template.py`: reads the `.txt`, writes a `.cpp` with `kSearchConfigPromptTemplate` using `R"DELIM(...)DELIM"`.

3. **CMake**  
   - `add_custom_command(OUTPUT ... SearchConfigPromptTemplate.cpp COMMAND ...)`  
   - Add the output to `APP_SOURCES` (and any other targets that use `BuildSearchConfigPrompt`).

4. **C++ changes**  
   - Declare `extern const char kSearchConfigPromptTemplate[];` (or in a small header) and use it in `BuildSearchConfigPrompt()`.
   - Implement placeholder replacement (e.g. a small loop or repeated `string::replace` for each placeholder) and return the final string.

5. **Tests**  
   - Ensure build and tests pass on Windows, macOS, and Linux; prompt content is unchanged except for being loaded from the embedded template.

## Summary

| Aspect | Conclusion |
|--------|------------|
| Extract prompt to separate file | Yes: e.g. `resources/prompts/search_config_prompt.txt` with placeholders. |
| Single executable | Yes: template is embedded via a generated `.cpp` compiled into the app. |
| Cross-platform | Yes: same CMake + Python approach on Windows, macOS, Linux. |
| Ease of modification | Yes: edit the `.txt` (or `.md`) and rebuild; no C++ for content changes. |

Recommended approach: **build-time embedding using a Python script that generates a C++ file with a raw string literal**, and **placeholder substitution at runtime** for the dynamic parts. No linker-specific or platform-specific embedding (no `.rc`, `objcopy`, or `ld -b binary`) is required.
