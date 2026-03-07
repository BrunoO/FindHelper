# 2026-01-31 Path Pattern pp:**/imgui/**/*.cpp – Why It Might Find Nothing

## Your expectation

With the path pattern **`pp:**/imgui/**/*.cpp`** you expect to find all `.cpp` files under any folder named exactly **`imgui`**.

That interpretation of the pattern is correct: `**` = “zero or more path segments (including separators)”, so the pattern means “any path that contains a segment `imgui` and ends with `*.cpp`”.

---

## How the pattern is handled

1. **Prefix**  
   `pp:` is stripped; the pattern passed to the path matcher is **`**/imgui/**/*.cpp`**.

2. **Normalization**  
   `NormalizePattern()` in `PathPatternMatcher.cpp` turns every **`**/`** into **`**`** (the slash after `**` is removed). So the compiled pattern is effectively **`**imgui**/*.cpp`**:
   - `**` → any path prefix (e.g. `external/`)
   - `imgui` → literal segment `imgui`
   - `**` → any path (e.g. `/`)
   - `*` → any non-separator characters (e.g. `imgui`)
   - `.cpp` → literal `.cpp`

3. **Matching**  
   The path matcher receives the **full path** for each indexed entry (directory + filename + extension). Matching is **whole-path** (implicit `^pattern$`). So a path like `external/imgui/imgui.cpp` or `/Volumes/Data/project/external/imgui/backends/imgui_impl_glfw.cpp` does match **`**imgui**/*.cpp`**.

So the implementation of the pattern is consistent with “all .cpp files below a folder named exactly `imgui`”.

---

## Why `find` finds files but the app finds nothing

- **`find external -name '*.cpp' -print | grep imgui`**  
  This runs on the **live filesystem** under `external/`. It doesn’t use the app’s index.

- **App search with `pp:**/imgui/**/*.cpp`**  
  This runs only on the **index**: the set of paths that were **crawled and stored** when you indexed a volume/folder.

So:

- If the **indexed root** does **not** include the place where `external/imgui/` lives (e.g. you indexed only `src/` or another subtree), then the index **contains no path** with a segment `imgui`, and the path pattern correctly returns **no results**.
- The pattern is not wrong; the index simply doesn’t have any path under a folder named `imgui`.

---

## What to check

1. **Crawl / index root**  
   Which folder or volume did you choose when starting the index? It must be a parent of `external/` (or of wherever your `imgui` folder lives) for paths like `external/imgui/...` to be in the index.

2. **Paths actually in the index**  
   - Run a very broad path pattern (e.g. `pp:**` or `pp:***.cpp`) and see which paths appear.  
   - Or check in the UI what root was indexed and whether `external` is under it.

3. **Path format**  
   Indexed paths are built with `PathBuilder::BuildFullPath()` using the volume root and `path_utils::kPathSeparator` (e.g. `/` on macOS/Linux, `\` on Windows). The path matcher treats both `/` and `\` as separators, so **`pp:**/imgui/**/*.cpp`** works regardless of the stored separator.

---

## Summary

| Question | Answer |
|----------|--------|
| Does **`pp:**/imgui/**/*.cpp`** mean “all .cpp under a folder named `imgui`”? | Yes. |
| Is the pattern compiled and matched correctly? | Yes (full path, whole-path match, **`**/`** → **`**`**). |
| Why does the app find nothing while `find` finds many? | The app searches the **index**; `find` searches the **filesystem**. If the indexed root doesn’t include `external/` (or the parent of your `imgui` folder), there are no matching paths in the index. |
| What to do? | Index a root that includes the folder containing `imgui` (e.g. the repo root that contains `external/`), then run **`pp:**/imgui/**/*.cpp`** again. |
