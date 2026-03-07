# Plan: 4 Additional UI Themes (Popular Theme Inspiration)

**Date:** 2026-02-14  
**Status:** Plan  
**Scope:** Theme system extension, Settings, Settings UI, bootstrap  
**Authority:** All implementation must follow **AGENTS.md** and **docs/standards/CXX17_NAMING_CONVENTIONS.md**. This plan integrates the project’s strict constraints explicitly.

---

## 1. Goal

Provide **4 additional UI themes** inspired by widely used editor/IDE themes, while keeping the current look as the default. Users can choose a theme in **Settings > Appearance**; the choice is persisted and applied at startup and immediately when changed.

---

## 2. Current State

- **Theme.h / Theme.cpp**: Single theme (ImGui StyleColorsDark + project overrides). Semantic colors: `Surface*`, `FrameBg*`, `Border`, `Accent*`, `Text*`, `Success`, `Warning`, `Error`.
- **Usage**: `Theme::Apply(viewports_enabled)` is called from `AppBootstrap_win.cpp` and `AppBootstrap_linux.cpp` after ImGui init. UI code uses `Theme::Colors::*` for `PushStyleColor` (e.g. in UIRenderer, FilterPanel, ResultsTable).
- **Settings**: No theme setting yet; Settings.h already mentions future "themes" extension.
- **Appearance UI**: SettingsWindow has Font, Font Size, UI Scale, UI Mode under "Appearance"; no theme selector.

---

## 2.1 Pre-implementation review: Theme extraction gaps

Before adding multiple themes, UI code was reviewed to ensure all color/style usage either goes through Theme or is explicitly mapped. Summary:

### Already using Theme::Colors (no change needed)

| File | Usage |
|------|--------|
| **UIRenderer.cpp** | Surface, SurfaceHover, SurfaceActive for header/button; one `GetStyleColorVec4(ImGuiCol_TextDisabled)` (theme-derived after Apply). |
| **FilterPanel.cpp** | Accent, AccentHover, AccentActive, Surface*, Text, TextDim for buttons. |
| **ResultsTable.cpp** | SurfaceHover (with alpha for row bg), Accent, Text, TextDisabled, Error. |
| **Theme.cpp** | Sets FrameBg*, Header*, Button*, Border from Colors; BorderShadow = transparent. |

**Note:** ResultsTable.cpp line 259 has a comment "Using Theme::Colors::Background as base" but Theme does not define `Background`; the code actually uses SurfaceHover with alpha. The comment is misleading and can be updated to "SurfaceHover" or removed.

### Hardcoded colors that should use Theme

| File | Current value | Semantic mapping |
|------|----------------|------------------|
| **SearchHelpWindow.cpp** | WindowBg 0.13, 0.13, 0.18; header 0.4, 1.0, 0.4 (green) | Add **WindowBg** (tool window); use **Success** for section headers. |
| **MetricsWindow.cpp** | WindowBg 0.13, 0.13, 0.18; orange (1, 0.5, 0), yellow (1, 0.8, 0), red (1, 0, 0) | **WindowBg**; **Warning** (orange/yellow); **Error** (red). |
| **SettingsWindow.cpp** | WindowBg 0.13, 0.13, 0.18; error 1, 0.3, 0.3; warning 1, 0.8, 0.4 / 1, 0.8, 0; status success 0.6, 0.9, 0.6 / fail 1, 0.5, 0.5 | **WindowBg**; **Error**; **Warning**; **Success** / **Error** for save status. |
| **Popups.cpp** | Green 0.4, 1, 0.4; red 1, 0.4, 0.4; gray 0.8, 0.8, 0.8 | **Success**, **Error**, **TextDim** (or keep neutral gray as TextDim). |
| **SearchInputs.cpp** | Primary button 0.2, 0.45, 0.75 etc.; success 0.3, 1, 0.3; error 1, 0.3, 0.3; ghost button transparent/hover 0.3/0.4, text 0.7 | **Accent, AccentHover, AccentActive**; **Success**; **Error**; ghost = Surface* with alpha or **TextDisabled** for text. |
| **EmptyState.cpp** | Yellow 1, 0.8, 0; dim 0.75; disabled 0.5 | **Warning**; **TextDim**; **TextDisabled**. |
| **StoppingState.cpp** | White 1, 1, 1 | **Text**. |
| **StatusBar.cpp** | Yellow, red, green, gray, cyan, orange (building/inactive/active/partial/searching) | **Warning**, **Error**, **Success**, **TextDim**; optionally add **Info** (cyan) and **StatusPartial** (orange) to Theme, or keep status colors as local constants if they should not follow theme. |

### Recommended Theme additions before multi-theme

1. **WindowBg** (or **ToolWindowBg**)  
   - Single semantic color for tool/modal windows (Search Help, Metrics, Settings).  
   - Current value: (0.13F, 0.13F, 0.18F, 1.0F).  
   - When implementing five themes, each palette sets its own WindowBg so tool windows stay consistent with the chosen theme.

2. **Fix misleading comment**  
   - In ResultsTable.cpp, change the comment that references `Theme::Colors::Background` to describe SurfaceHover (or remove the line).

3. **Optional: SectionHeader**  
   - Green (0.4, 1, 0.4) used in SearchHelp and Popups for section titles. Can use **Success** as-is to avoid a new semantic.

4. **Optional: Status colors**  
   - StatusBar uses yellow/red/green/gray/cyan/orange. For theme coherence, add **Info** (e.g. cyan for “searching”) and **StatusPartial** (orange for “displayed: N”) to Theme so status bar respects the palette; otherwise keep them as file-local constants.

### Action items (extract before or with multi-theme)

- Add **WindowBg** to Theme.h (and to each theme palette when implementing five themes). Replace hardcoded WindowBg in SearchHelpWindow.cpp, MetricsWindow.cpp, SettingsWindow.cpp with `Theme::Colors::WindowBg`.
- Replace all Success/Warning/Error-looking literals with Theme::Colors::Success, Warning, Error (Popups, SettingsWindow, SearchInputs, EmptyState, MetricsWindow).
- Replace primary button colors in SearchInputs.cpp with Theme::Colors::Accent, AccentHover, AccentActive.
- Replace ghost-button and dim text in SearchInputs.cpp and EmptyState.cpp with Theme::Colors (Surface with alpha or TextDim/TextDisabled); StoppingState white with Theme::Colors::Text.
- Update ResultsTable.cpp comment (Background → SurfaceHover).
- Decide whether StatusBar colors come from Theme (add Info/StatusPartial) or remain local; document in Theme.h.

Once these are done, every theme-aware UI element will go through Theme, and the four new themes can define all semantic colors (including WindowBg and optional status colors) per palette.

---

## 3. Theme Lineup (5 Total: 1 Existing + 4 New)

| ID (internal)   | Display name   | Inspiration     | Brief palette description                    |
|-----------------|---------------|------------------|----------------------------------------------|
| `default_dark`  | Default Dark  | Current (ImGui) | Gray-blacks, blue accent (unchanged)         |
| `dracula`       | Dracula       | Dracula Theme    | Dark purple-gray bg, pink/purple accent       |
| `nord`          | Nord          | Nord             | Cold blue-gray bg, frost blue accent         |
| `one_dark`      | One Dark      | Atom One Dark    | Warm dark gray, orange/blue accents          |
| `gruvbox`       | Gruvbox Dark  | Gruvbox          | Warm brown/dark bg, soft orange accent       |

All are dark themes to match current app style and reduce contrast issues.

---

## 4. Design Decisions

### 4.1 Theme identification

- **Storage**: `std::string themeId` in `AppSettings` (e.g. `"default_dark"`, `"dracula"`), same pattern as `loadBalancingStrategy` / `fontFamily`.
- **Validation**: On load, if `themeId` is not one of the five known IDs, fall back to `"default_dark"` and optionally log.

### 4.2 Theme application and current colors

- **Apply**: `Theme::Apply(theme_id, viewports_enabled)` (or `Apply(std::string_view theme_id, bool viewports_enabled)`). Replaces current `Apply(bool)`.
- **Current colors for UI**: UI code uses `Theme::Colors::Accent`, etc. Today those are fixed constants. Two options:
  - **A** – Keep a single `Theme::Colors` that is **set** by `Apply(theme_id)` to the active palette (e.g. copy from a per-theme table), and UI keeps using `Theme::Colors::*`. Requires `Colors` to be non-const (runtime values) or a getter like `Theme::GetColors()` returning the active palette.
  - **B** – Define one struct/namespace per theme (e.g. `ThemePalette::DefaultDark`, `ThemePalette::Dracula`) and have `Theme::GetColors()` return `const ThemePalette&` for the current theme; `Apply()` sets current theme and pushes that palette into ImGui.

Recommended: **B** – Keep semantic names (Surface, Accent, …), define a **ThemePalette** (or per-theme struct) with the same members; one instance per theme. `Theme::Apply(theme_id)` applies that palette to ImGui and sets the “current” palette; `Theme::CurrentColors()` returns `const ThemePalette&` so existing call sites become `Theme::CurrentColors().Accent` (or we keep a shorthand `Theme::Colors` as an alias to current). To minimize code churn, we can keep **`Theme::Colors`** as the name of the **current** palette (e.g. static reference or getter), so existing `Theme::Colors::Accent` still works if we make `Colors` a getter that returns the current palette. So:

- Introduce a **palette struct** (e.g. `ThemePalette`) with: Surface, SurfaceHover, SurfaceActive, FrameBg, FrameBgHover, FrameBgActive, Border, Accent, AccentHover, AccentActive, Text, TextDim, TextDisabled, Success, Warning, Error.
- Each theme defines a constant `ThemePalette` (or namespace with constexpr ImVec4s).
- `Theme::Apply(theme_id)`:
  - Looks up palette for `theme_id`.
  - Writes that palette into ImGui style colors (same mapping as today).
  - Sets internal “current palette” pointer/reference.
- `Theme::Colors` continues to expose the same semantic names: either as a **reference** to the current palette (so `Theme::Colors::Accent` is `Theme::CurrentPalette().Accent`) or as a static struct that `Apply()` overwrites. Simplest for minimal call-site changes: **current palette** is a static `const ThemePalette*` and `Theme::Colors` is a wrapper or alias that forwards to that (e.g. `Theme::Colors` is the struct name and we document that it reflects current theme after `Apply()`). So: one **ThemePalette** type, five static instances (DefaultDark, Dracula, Nord, OneDark, Gruvbox), `Apply(id)` sets `s_current = &PaletteFor(id)` and copies into ImGui; `GetCurrentPalette()` returns `*s_current`. Then we need call sites to use `Theme::GetCurrentPalette().Accent` unless we keep a global or inline wrapper. Easiest: **Theme::Colors** becomes a **static const ThemePalette&** that returns the current palette (e.g. `return *s_current;`), so `Theme::Colors::Accent` becomes a getter. But C++ doesn’t allow a static struct with mutable “current” and same member names as getters. So either:
  - **Option 1**: All call sites change to `Theme::GetCurrentPalette().Accent` (or `Theme::Current().Accent`).
  - **Option 2**: Keep `Theme::Colors` as a **namespace** with **inline getters** (e.g. `inline const ImVec4& Accent() { return GetCurrentPalette().Accent; }`). Then `Theme::Colors::Accent` becomes `Theme::Colors::Accent()` — call sites need `()`.
  - **Option 3**: Keep `Theme::Colors` as a struct with **static** members that are **updated by Apply()** (so they’re not constexpr but static storage). So we have one copy of the “current” palette in `Theme::Colors`; `Apply()` copies the selected theme into `Theme::Colors`. No API change for call sites: `Theme::Colors::Accent` stays and is an lvalue that Apply() overwrites. This is the smallest-change option.

**Recommendation:** **Option 3** – `Theme::Colors` remains a struct with the same member names; its members are **writable** (non-const) and are updated by `Theme::Apply(theme_id)`. So we don’t add a separate “ThemePalette” type for the API; we keep one global “current” set of colors in `Theme::Colors` and Apply() copies the chosen theme into it and into ImGui. Internally we have five constant palettes (e.g. in an anonymous namespace or in Theme.cpp) and Apply() does a single copy into `Theme::Colors` and into ImGui. Call sites remain unchanged: `Theme::Colors::Accent`, etc.

### 4.3 Style (rounding, borders)

- Keep one shared **SetupStyle(viewports_enabled)** (rounding, FrameBorderSize, etc.). If we want theme-specific style later, we can add optional overrides per theme; for this plan, style is global.

### 4.4 Bootstrap and settings

- **Startup**: After ImGui init, call `Theme::Apply(settings.themeId, viewports_enabled)`. If `themeId` is empty or unknown, use `"default_dark"`.
- **Persistence**: Add `themeId` to `AppSettings` (default `"default_dark"`). In Settings.cpp, in LoadBasicSettings (or a small LoadAppearanceExt) load `themeId` with validation; in SaveSettings write `j["themeId"] = settings.themeId`.
- **Settings UI**: In **Appearance**, add a **Theme** combo (same pattern as Font): labels "Default Dark", "Dracula", "Nord", "One Dark", "Gruvbox"; values `default_dark`, `dracula`, `nord`, `one_dark`, `gruvbox`. On change: update `settings.themeId` and call `Theme::Apply(settings.themeId, viewports_enabled)` so the theme updates immediately. We need `viewports_enabled` in the settings flow: either pass it from Application (which has ImGui io) or re-detect from `ImGui::GetIO().ConfigFlags` when applying theme in Settings.

---

## 4.5 Strict constraints (AGENTS.md and project rules)

Implementation of this plan **must** comply with the following. Treat this as a mandatory checklist before committing theme-related changes.

### Language and standard

- **C++17 only**: No C++20 (e.g. no `std::bit_cast`). No use of features from older standards that the project has deprecated.
- **Float literals**: Use uppercase `F` suffix (e.g. `0.13F`, `1.0F`), not `f`, to satisfy clang-tidy and project rule.

### Naming and style

- **Naming conventions**: Follow **docs/standards/CXX17_NAMING_CONVENTIONS.md**. Types/functions `PascalCase`; locals/params `snake_case`; members `snake_case_`; constants `kPascalCase`; type aliases `PascalCase`.
- **Settings JSON/camelCase**: `themeId` in `AppSettings` and in JSON is camelCase for the settings API; use the same NOLINT and comment pattern as existing fields (e.g. `fontFamily`, `loadBalancingStrategy`).
- **If/else (S3972)**: No `} if (` on one line; put `if` on a new line or use `else if`.

### Parameters and types

- **Read-only parameters**: Use `const T&` or `std::string_view` for parameters that are not modified. Prefer `std::string_view` for theme ID (e.g. `Apply(std::string_view theme_id, bool viewports_enabled)`).
- **Parameter count**: No function with more than 7 parameters; group into structs if needed.
- **Unused parameters**: Remove or mark with `[[maybe_unused]]`; no bare unused params.

### Code quality

- **Nesting depth**: Maximum 3 levels; use early returns and guard clauses.
- **Cognitive complexity**: Keep functions under 25; extract helpers if needed (e.g. theme lookup, palette copy).
- **No void\***: Use concrete types or templates; theme IDs are `std::string` / `std::string_view`, not `void*`.
- **RAII**: No `malloc`/`free`/`new`/`delete`; use standard containers and smart pointers.
- **Const correctness**: Mark const any member functions and parameters that do not modify state.
- **Exceptions**: Handle meaningfully (log or rethrow); no empty catch blocks (S2486, S108).
- **C-style arrays**: Do not introduce C-style arrays; use `std::array` or `std::vector` (e.g. theme ID allowlist as `std::array<std::string_view, 5>` or similar).
- **Comment `#endif`**: Every `#endif` must have a matching comment (e.g. `#endif  // _WIN32`), two spaces before `//`.
- **Include order**: All `#include` at top of file; system then project; use lowercase `<windows.h>` if any Windows include is added (S954, S3806).

### Windows-specific (when touching code that includes Windows.h)

- **min/max**: Use `(std::min)` and `(std::max)` with parentheses to avoid Windows macro expansion.
- **Lambdas in templates**: Use explicit capture lists only; no `[&]` or `[=]` in template code (MSVC).

### Platform and build

- **Cross-platform**: Theme logic is platform-agnostic. Do not add theme behavior inside `#ifdef _WIN32` / `#ifdef __APPLE__` / `#ifdef __linux__` unless abstracted (e.g. platform-specific defaults live in one place). Theme application runs on all three platforms.
- **No backward compatibility**: The project does not require backward compatibility. Support only the five theme IDs; no legacy ID mapping or deprecated theme names.
- **macOS validation**: On macOS, any change to C++ sources, headers, or CMake **must** be validated by running **`scripts/build_tests_macos.sh`** (the only allowed build/test entrypoint on macOS). Document in the PR or commit whether the script was run and that it passed.
- **CMake**: If no new source files are added, do not modify CMakeLists.txt. If theme tests or new files are added, mirror existing patterns and do not change PGO or conditional logic; follow "Modifying CMakeLists.txt Safely" in AGENTS.md.

### ImGui and UI

- **Main thread**: All ImGui calls (including Theme::Apply and reading Theme::Colors) occur on the main thread only.
- **Popup rules**: If any theme-related UI uses popups (e.g. theme preview), follow AGENTS.md popup rules: same window context for OpenPopup/BeginPopupModal, SetNextWindowPos every frame before BeginPopupModal, close button outside CollapsingHeader with SetItemDefaultFocus, CloseCurrentPopup inside BeginPopupModal block, consistent popup IDs.

### Sonar and NOLINT

- **NOSONAR**: When suppressing a Sonar rule, put the comment on the **same line** as the issue (per user rule). Justify in a short comment.
- **No inline `#` in .clang-tidy**: If .clang-tidy is edited, do not put `#` comments on the same line as check names in YAML values; use separate comment blocks.

### Design principles (YAGNI, SOLID, KISS, DRY)

- **YAGNI**: Implement only the five themes and themeId; no “future theme format” or extra extension points unless justified.
- **Single responsibility**: Theme module applies palette and exposes current colors; Settings load/save themeId; UI renders combo and calls Apply. Do not mix concerns.
- **DRY**: One palette struct type; one Apply path; shared validation allowlist for themeId.
- **KISS**: Prefer Option 3 (Theme::Colors as writable static struct updated by Apply) to avoid API churn and extra getters.

Before submitting theme-related changes, run through the "Questions to Ask Yourself" in AGENTS.md and confirm the above constraints are met.

---

## 5. Implementation Tasks

### 5.1 Theme palettes (Theme.h / Theme.cpp)

1. **Palette type**: Define an internal struct (e.g. `ThemePalette`) in Theme.cpp (anonymous namespace) or in Theme.h with the same semantic fields as current `Theme::Colors`: Surface, SurfaceHover, SurfaceActive, FrameBg, FrameBgHover, FrameBgActive, Border, Accent, AccentHover, AccentActive, Text, TextDim, TextDisabled, Success, Warning, Error. **Constraints:** Naming PascalCase; use `using` alias if the type is exposed (AGENTS.md type aliases).
2. **Five palettes**: Define constant palettes for Default Dark (current values), Dracula, Nord, One Dark, Gruvbox. Use canonical hex/RGB values from the respective theme specs (see references below). **Constraints:** All float literals with uppercase `F` suffix (e.g. `0.13F`).
3. **Theme::Colors**: Keep the same member names; change from `static constexpr ImVec4` to storage that Apply() can write (e.g. static struct with non-const members, or a single static ThemePalette that Apply() overwrites). Apply(theme_id) copies the selected palette into Theme::Colors and into ImGui (current SetupColors() logic, but driven from the selected palette).
4. **Theme::Apply(std::string_view theme_id, bool viewports_enabled)**:
   - Map `theme_id` to one of the five palettes (default to default_dark if unknown). Use `std::string_view` for theme_id (AGENTS.md); keep nesting ≤3 and complexity ≤25 (extract lookup to a helper if needed).
   - Copy that palette into `Theme::Colors` (and/or into ImGui).
   - Call existing SetupColors() logic (using the selected palette) and SetupStyle(viewports_enabled).
5. **Backward compatibility**: Keep a single-argument overload `Apply(bool viewports_enabled)` that calls `Apply("default_dark", viewports_enabled)` for call sites that don’t pass theme yet, until bootstrap is updated. (Project has no backward-compat requirement; this is only for minimal call-site change during rollout.)

### 5.2 Settings

6. **AppSettings**: Add `std::string themeId = "default_dark";` with the same NOLINT/camelCase comment pattern as other JSON-backed fields (e.g. `fontFamily`, `loadBalancingStrategy`) per Settings.h style.
7. **Load**: In Settings.cpp, in the function that loads basic/appearance settings, add: if `j.contains("themeId")` and is string, validate against the five allowed values (use a single allowlist, e.g. `std::array<std::string_view, 5>` or equivalent—no C-style arrays); if valid, set `out.themeId`; else keep default and optionally log. **Constraints:** Const correctness; early return to avoid deep nesting.
8. **Save**: In the SaveSettings path, add `j["themeId"] = settings.themeId`.

### 5.3 Bootstrap

9. **Windows**: In AppBootstrap_win.cpp where Theme::Apply is called, pass `app_settings.themeId` and viewports flag: `Theme::Apply(app_settings.themeId, viewports_enabled)`.
10. **Linux**: Same in AppBootstrap_linux.cpp.
11. **macOS**: Same in AppBootstrap_mac.mm if it calls Theme::Apply.

### 5.4 Settings UI

12. **Appearance section**: Add a **Theme** combo before or after Font. Labels: "Default Dark", "Dracula", "Nord", "One Dark", "Gruvbox". Values: `default_dark`, `dracula`, `nord`, `one_dark`, `gruvbox`. Store selected value in `settings.themeId`. Use the same Combo pattern as Font (see RenderAppearanceSettings); keep nesting and cognitive complexity within limits.
13. **Immediate apply**: When the user changes the theme in the combo, call `Theme::Apply(settings.themeId, viewports_enabled)`. Obtain viewports_enabled from `ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable` so no extra parameter is required in the render path. **Constraint:** All ImGui and Theme calls on main thread (already guaranteed by Settings window render path).

### 5.5 Tests and docs

14. **Unit test**: Optional: test that unknown `themeId` falls back to default_dark and that all five theme IDs apply without crash; optional: snapshot ImGui color for one slot after Apply to detect regressions.
15. **Docs**: Update any “appearance” or “theme” mention in docs (e.g. Settings.h comment, README) to note that five themes are available.
16. **macOS validation (required)**: On macOS, after any C++/header/CMake change for this feature, run **`scripts/build_tests_macos.sh`** and ensure it passes. This is the only allowed build/test entrypoint on macOS (AGENTS.md). State in the PR or commit that the script was run and passed.

---

## 6. Palette Reference (Inspiration)

Values below are for implementation reference; exact ImVec4 (0–1 float) should be derived from these.

- **Default Dark**: Current Theme.cpp values (gray-blacks, ImGui blue accent).
- **Dracula**: Background #282a36, foreground #f8f8f2, accent #bd93f9 (purple), pink #ff79c6. [Dracula Theme](https://draculatheme.com/).
- **Nord**: Background #2e3440, #3b4252; accent #88c0d0, #81a1c1. [Nord](https://www.nordtheme.com/).
- **One Dark**: Background #282c34, #21252b; accent #61afef (blue), #e5c07b (yellow/orange). [Atom One Dark](https://github.com/atom/one-dark-syntax).
- **Gruvbox**: Background #282828, #3c3836; accent #fe8019 (orange), #b8bb26 (green). [Gruvbox](https://github.com/morhetz/gruvbox).

Ensure sufficient contrast for Text/TextDim/TextDisabled on Surface and FrameBg; Success/Warning/Error remain readable on dark backgrounds.

---

## 7. File Checklist

| File | Change |
|------|--------|
| `src/ui/Theme.h` | Add Apply(theme_id, viewports_enabled); Colors become writable (current palette); optional GetThemeIds() for UI. |
| `src/ui/Theme.cpp` | Define five ThemePalette constants; Apply(id) maps id → palette, copies to Colors and ImGui; keep SetupStyle. |
| `src/core/Settings.h` | Add `std::string themeId = "default_dark";`. |
| `src/core/Settings.cpp` | Load/save themeId with validation (allowlist of five). |
| `src/platform/windows/AppBootstrap_win.cpp` | Call Theme::Apply(settings.themeId, ...). |
| `src/platform/linux/AppBootstrap_linux.cpp` | Call Theme::Apply(settings.themeId, ...). |
| `src/platform/macos/AppBootstrap_mac.mm` | Call Theme::Apply(settings.themeId, ...) if applicable. |
| `src/ui/SettingsWindow.cpp` | Add Theme combo in RenderAppearanceSettings; on change set themeId and Theme::Apply(..., viewports). |

---

## 8. Risks and Mitigations

- **Contrast / accessibility**: Validate text-on-background contrast for each theme (especially TextDim, TextDisabled). Mitigation: use WCAG-friendly values from the official theme repos.
- **ImGui color slots**: Apply() must set all ImGui slots that SetupColors() currently sets (FrameBg, Header, Button, Border, etc.); no new slots needed.
- **Thread safety**: Theme::Apply and reading Theme::Colors are on the main thread only (ImGui). No extra locking.

---

## 9. Out of Scope (Later)

- Light themes.
- User-customizable accent color.
- Per-window or per-panel theme overrides.
- Theme-specific rounding/border style (can be added later if needed).

---

## 10. Summary

- Add **themeId** to settings (default `"default_dark"`) with load/save and validation.
- Refactor **Theme** so five constant palettes exist and **Apply(theme_id, viewports_enabled)** applies the chosen palette to ImGui and to **Theme::Colors** (current palette); keep **Theme::Colors** as the API so existing call sites stay unchanged.
- Bootstrap calls **Theme::Apply(settings.themeId, viewports_enabled)** after ImGui init.
- Settings **Appearance** gets a **Theme** combo; changing it updates **themeId** and calls **Theme::Apply** for immediate feedback.

This yields four additional themes (Dracula, Nord, One Dark, Gruvbox) plus the existing default dark, with minimal API change and clear persistence and UX.
