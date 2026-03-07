## UX Review – Settings Window (2026-01-14)

**Last Updated**: 2026-01-14  
**Scope**: `ui/SettingsWindow.cpp` behavior and layout  
**Source Prompt**: `docs/prompts/ux-review.md` applied to Settings window only  

---

## Summary

- **UX Health Score (Settings window)**: **7.5 / 10**  
- **Key Improvements Implemented**:
  - Unified **explicit Save semantics** (Close now truly discards unsaved changes).
  - Clear **section structure**: Appearance, Index Configuration, Log File.
  - All performance options grouped under **“Advanced performance tuning”**.
  - Better **index/recrawl UX** (editable path, validation, behavior summary, warning).
  - Inline **save success/failure feedback**.

All findings from the initial Settings-specific review are now addressed for this iteration; a few are partially addressed with room for future UX polish if needed.

---

## Findings and Status

### 1. Mixed “Save vs Auto-Save” Semantics

- **Original Issue**:  
  - Some settings (crawl folder, recrawl options) called `SaveSettings` immediately, while others only saved on the main **Save** button.  
  - Users could not predict what “Close” would keep or discard.
- **Resolution**: ✅ **Completed**  
  - Introduced a **working copy** (`working_settings`) inside `SettingsWindow::Render`.  
  - All UI controls now edit `working_settings` only.  
  - On **Save**:
    - `working_settings` is copied back into the real `settings`.  
    - `SaveSettings(settings)` is called once.  
  - On **Close** (or window X):
    - `*p_open` is set to false and the working copy is discarded; no persistence occurs.
- **Notes**:
  - This makes Save vs Close behavior predictable and aligns with common desktop app patterns.

### 2. No Explicit Save Confirmation or Error Feedback

- **Original Issue**:  
  - After pressing Save, there was no visible confirmation or error message in the UI (only logs).
- **Resolution**: ✅ **Completed**  
  - Added inline status message area under the explanatory text:
    - On success: greenish text “Settings saved successfully.”  
    - On failure: reddish text “Failed to save settings. See log file for details.”  
  - The window only closes automatically when Save succeeds.

### 3. Index Configuration Visibility and Clarity (Win/macOS/Linux)

- **Original Issue**:  
  - On Windows with admin rights, folder selection could be hidden, so index configuration options were not discoverable.  
  - The folder path was read-only, and recrawl behavior wasn’t summarized.
- **Resolution**: ✅ **Completed**  
  - Index Configuration is now **always visible** on all platforms.  
  - Windows-specific copy explains behavior:
    - Non-admin: guidance for folder-based indexing.  
    - Admin: note that USN monitoring may be active and folder indexing is an alternative.
  - “Folder to Index” is now:
    - An **editable InputText** bound to `settings.crawlFolder`.  
    - Has a `Browse…` button (FolderBrowser) and **Copy Path** button (uses clipboard utils).
  - Recrawl section now includes:
    - A short **behavior summary** (“Recrawl will run roughly every N minutes…”) based on current settings.  
    - A **warning** when interval ≤ 2 minutes and idle requirement is off.

### 4. Cognitive Load in Performance Settings

- **Original Issue**:  
  - All tuning options (strategy, thread pool size, dynamic chunk size, hybrid percent) were visible at once, potentially overwhelming users.
- **Resolution**: ✅ **Completed**  
  - Entire performance area is now under a single collapsing header:
    - **“Advanced performance tuning”** (`ImGui::CollapsingHeader`, default-open).  
  - Inside the header:
    - Load Balancing Strategy, Thread Pool Size, Dynamic Chunk Size, Hybrid Initial Work % with detailed tooltips.
    - Short recommendation line: “Recommended: Hybrid strategy with auto thread pool size.”
  - Collapsing the header hides all performance controls, reducing visual noise for users who don’t need tuning.

### 5. Recrawl Controls – Relationships and Risk Communication

- **Original Issue**:  
  - Recrawl interval, idle requirement, and idle threshold were individually labeled but their combined behavior wasn’t obvious.  
  - No warning for heavy configurations.
- **Resolution**: ✅ **Completed**  
  - Added a **summary line** under the recrawl controls:
    - With idle requirement: “Recrawl will run roughly every N minutes, after the system has been idle for M minutes.”  
    - Without idle requirement: “Recrawl will run roughly every N minutes, even when the system is active.”  
  - Added a **warning** line when `recrawlIntervalMinutes <= 2` and `recrawlRequireIdle == false`:
    - “Frequent recrawls without idle requirement may increase background CPU and disk usage.”

### 6. Section Hierarchy and Visual Organization

- **Original Issue**:  
  - Section headers were just `ImGui::Text` + `Separator` scattered inline, making the hierarchy weaker and inconsistent.
- **Resolution**: ✅ **Completed**  
  - Introduced a shared helper `RenderSectionHeader(const char* label)` that:
    - Adds spacing, prints the label, adds a `Separator`, and spacing again.  
  - Applied to:
    - `Appearance`  
    - `Index Configuration`  
    - `Log File`  
  - Performance section label was removed; the **collapsing header** now serves as the primary visual affordance.

### 7. Keyboard Workflows & Hints

- **Original Issue**:  
  - Keyboard navigation existed via ImGui defaults, but there was no discoverable hint in the Settings window.
- **Resolution**: ✅ **Completed (basic)**  
  - Added a small hint under the Save/Close explanation:
    - “Keyboard: Use Tab/Shift+Tab to move focus, Enter/Space to activate the focused button.”  
  - This documents the current behavior without adding new global shortcuts.
- **Potential Future Work**:  
  - Global shortcuts for opening/closing Settings are already handled at the application level; if extended, they could be mentioned here.

### 8. Log File Section as Error-Recovery Aid

- **Original Issue**:  
  - Log File section was useful but under-explained; users might not connect it with troubleshooting.
- **Resolution**: ⚪ **Partially Addressed**  
  - Current behavior (unchanged) already shows:
    - Log file path (wrapped) and a “Copy Path to Clipboard” button (disabled when no GLFW window).  
  - The UX change in this iteration focused on save error messaging (previous finding #2).  
  - **Future improvement idea** (not yet implemented):
    - Add a short explanatory line like “Use this log file when diagnosing indexing or settings issues.”

### 9. Documentation & Review Alignment

- **Original Issue**:  
  - Earlier UX review (`UX_REVIEW_2026-01-04.md`) flagged the Settings window as lacking visual organization (Issue #12).
- **Resolution**: ✅ **Completed**  
  - `docs/ui-ux/reviews/UX_REVIEW_2026-01-04.md` updated:
    - Issue #12 now marked as **COMPLETED** with notes pointing to the new Settings organization and advanced performance header.  
  - This document (`UX_REVIEW_2026-01-14_SettingsWindow.md`) records:
    - The dedicated Settings-window review.  
    - Implementation status for each finding.

---

## Status Table

| # | Finding (Settings Window)                                         | Status        | Notes                                                   |
|---|-------------------------------------------------------------------|--------------|---------------------------------------------------------|
| 1 | Mixed Save vs Auto-Save semantics                                 | ✅ Completed  | Working copy + explicit Save/Close semantics           |
| 2 | No explicit save success/error feedback                           | ✅ Completed  | Inline colored status messages after Save              |
| 3 | Index configuration visibility & clarity (Win/macOS/Linux)       | ✅ Completed  | Always visible, better copy, editable path, summary    |
| 4 | Performance settings cognitive load                               | ✅ Completed  | All under “Advanced performance tuning” header         |
| 5 | Recrawl relationships & risk communication                        | ✅ Completed  | Summary text + frequent-recrawl warning                |
| 6 | Weak section hierarchy / visual organization                      | ✅ Completed  | Shared section header helper + clear groupings         |
| 7 | Keyboard workflows not surfaced                                   | ✅ Completed  | Basic keyboard usage hint text                         |
| 8 | Log File section under-leveraged for recovery                     | ◻ Planned     | Behavior unchanged; consider explanatory copy later    |
| 9 | Docs alignment with previous UX review (Issue #12)                | ✅ Completed  | Older review updated; this new doc added               |

---

## Notes for Future Iterations

1. **Hardware / system info in Settings**  
   - Original review suggested surfacing core count or similar in performance settings.  
   - Current design keeps Settings focused and defers system telemetry to Metrics; revisit if users request it.

2. **Deeper accessibility work**  
   - High contrast themes, more robust keyboard shortcuts, and potential screen-reader strategies (within ImGui limits) are out of scope for this round but remain desirable.

3. **Onboarding and inline help**  
   - If users still struggle with performance or recrawl settings, consider inline “Learn more” links or a short help popup from the Settings window.

