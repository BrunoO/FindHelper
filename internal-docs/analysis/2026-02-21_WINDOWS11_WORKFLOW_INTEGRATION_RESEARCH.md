# Research: FindHelper as an Essential Part of Windows 11 User Workflows

**Date:** 2026-02-21  
**Purpose:** Explore ways for FindHelper to become an essential part of Windows 11 user workflows **without PowerToys**. Focus on Run, Start, File Explorer, context menus, shortcuts, and system integration.  
**References:** Microsoft Learn (shell, context menu, App Paths, Jump List, hotkeys), Windows 11 UX patterns.

---

## 1. Summary: integration vectors

| Vector | What the user gets | Implementation effort |
|--------|--------------------|------------------------|
| **Run + Start** | Type “FindHelper” or “find” from Run/Start to launch | Low: installer + App Paths + Start Menu shortcut |
| **Custom URL protocol** | `findhelper:search?q=...` from Run, browser, scripts | Low: registry + app parses URI + CLI initial query |
| **Folder context menu** | Right‑click folder → “Search with FindHelper” (in this folder) | Low: registry verb + `--crawl-folder="%1"` (or `--search-in=`) |
| **Send To** | Right‑click folder → Send to → FindHelper (search in folder) | Low: shortcut in `shell:sendto` |
| **Global hotkey** | e.g. Ctrl+Shift+F from anywhere → bring up FindHelper | Medium: `RegisterHotKey` + `WM_HOTKEY` in app (Windows) |
| **Jump List** | Right‑click taskbar icon → “Search here” / “Open FindHelper” | Medium: App User Model ID + `ICustomDestinationList` / `AddUserTasks` |
| **Startup (optional)** | FindHelper starts with Windows for “always ready” search | Low: Startup folder or Task Scheduler (user choice) |
| **Drag‑and‑drop** | Drag folder onto FindHelper (or shortcut) → search in folder | Low: app handles dropped path in `argv` or DDE/drop target |
| **Pin to Quick Access** | From result table: pin the selected file or folder to Explorer’s Quick Access | Low: Shell verb `pintohome` on the result path (works for both files and folders on Windows 11) |

Higher‑effort options (Windows Search provider, File Explorer search box integration) are noted in section 5 but not recommended as first steps.

---

## 2. Run dialog and Start menu

### 2.1 Goal

User presses **Win+R** (or types in Start), enters **FindHelper** (or **find**), and the app launches—without needing the full path or PATH.

### 2.2 App Paths (registry)

- **Location:** `HKEY_LOCAL_MACHINE\SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths` (or `HKEY_CURRENT_USER\...\App Paths` for per‑user).
- **Subkey:** e.g. `FindHelper.exe` (name user types in Run).
- **Default value:** Full path to `FindHelper.exe`.
- **Optional:** `Path` value = directory to add to PATH when the app runs (e.g. for dependencies).
- **Note:** Run and `ShellExecute` use App Paths; **cmd.exe** does not (it uses real PATH only).

### 2.3 Start menu and search

- Shortcuts in **Start Menu Programs** are indexed and searchable. Put a shortcut in:
  - **Per‑user:** `%APPDATA%\Microsoft\Windows\Start Menu\Programs`
  - **All users:** `C:\ProgramData\Microsoft\Windows\Start Menu\Programs`
- Same shortcut can be **pinned to Start** or **pinned to taskbar** by the user.
- Installer (or setup script) should: (1) install exe to e.g. `%ProgramFiles%\FindHelper\`, (2) add App Paths entry for `FindHelper.exe`, (3) create Start Menu shortcut.

### 2.4 Optional: launch with query

If the app supports an initial search (e.g. `--search=...` or positional arg), a **second shortcut** (e.g. “FindHelper (search)”) could run `FindHelper.exe "%1"` or use a protocol (see below). Run dialog does not easily pass typed text to an app; the main gain is “run by name” and Start search.

---

## 3. Custom URL protocol (findhelper:)

### 3.1 Goal

User or script invokes `findhelper:search?q=my+query` (from Run, browser, or link) and FindHelper opens with that query pre‑filled (and optionally runs search).

### 3.2 Registry (conceptual)

```
HKEY_CLASSES_ROOT\findhelper
  (Default)    = "URL:FindHelper Search"
  URL Protocol = ""
  DefaultIcon
    (Default)  = "C:\...\FindHelper.exe,1"
  shell\open\command
    (Default)  = "C:\...\FindHelper.exe" "%1"
```

- **URL Protocol** (empty string) is required so Windows treats it as a protocol handler.
- **%1** = full URI passed to the app as first argument.

### 3.3 App behavior

- In `ParseCommandLineArgs` (or early startup): if `argv[1]` looks like `findhelper:...`, parse the URI (e.g. take `q` from query string), set **initial search query** and optionally trigger search when the index is ready.
- Requires **CLI initial search** support (see previous research doc); protocol and CLI can share the same “initial query” path.

### 3.4 Use cases

- Run: user types `findhelper:search?q=report.pdf`.
- Scripts/batch: `start findhelper:search?q=%1`.
- Documentation or shortcuts: link that opens FindHelper with a predefined query.

---

## 4. File Explorer integration

### 4.1 “Search with FindHelper” on folders (context menu)

- **Mechanism:** Add a **static verb** under the **Directory** (folder) type or under `*\Directory\shell` so the verb appears when the user right‑clicks a **folder**.
- **Registry (simplified):**
  - Under `HKEY_CLASSES_ROOT\Directory\shell\FindHelperSearch` (or under `HKEY_CLASSES_ROOT\*\Directory\shell\...` depending on desired scope):
    - `(Default)` = "Search with FindHelper"
    - `command\(Default)` = `"C:\...\FindHelper.exe" "--crawl-folder=%1"` (or a dedicated `--search-in=%1` if you add it).
- **Windows 11:** New context menu may hide legacy verbs under “Show more options”; to show the verb in the main menu you may need a **COM context menu handler** (more work). For “essential workflow,” the classic menu (or “Show more options”) is often enough.
- **Optional:** Restrict to “folder” only by using `Directory` / `Drive` and not `*` so it doesn’t appear on files (or add a separate verb for “Search folder containing this file”).

### 4.2 Send To

- **Folder:** `shell:sendto` → `%APPDATA%\Microsoft\Windows\SendTo`.
- **Action:** Add a **shortcut** to `FindHelper.exe` (or to a wrapper that passes the folder). When the target is a **folder**, the shortcut can use:
  - `"C:\...\FindHelper.exe" "--crawl-folder=%1"` — but Send To passes the **dropped item**; for a shortcut, the “argument” is typically the path of the sent item. So the executable should accept the path as first argument when launched via Send To (Explorer passes the path as argument).
- **Implementation:** Ensure FindHelper accepts a single path argument (folder) and treats it as `--crawl-folder=<path>` or `--search-in=<path>`. Then the Send To shortcut can be: Target = `FindHelper.exe`, “Start in” = folder of exe; Explorer will pass the sent folder path as argument. So: **one path argument = “search in this folder”** (same as context menu).

### 4.3 Drag‑and‑drop onto FindHelper

- User drags a **folder** onto FindHelper.exe (or its shortcut). Windows can launch the exe with the dropped path as argument (depending on how the shortcut/drop is configured). Supporting a single **positional argument** as “folder to search in” (or “initial path”) makes both Send To and drag‑and‑drop consistent.

### 4.4 Pin to Quick Access from the result table

- **Goal:** When a **folder or file** is shown in the result table, the user can pin it to **Quick Access** (File Explorer’s left pane: “Pinned” / “Favorites”) with one action—without opening Explorer or the full context menu.
- **Works for both files and folders:** Windows 11 (build 22557+) supports pinning **files** as well as **folders** to Quick Access. Pinned folders appear under “Folders”; pinned files under “Pinned files” / “Favorites”. The same Shell verb is used for both.
- **Mechanism:** Invoke the Shell verb **`pintohome`** on the item’s full path. Use the Shell.Application COM API (or IShellFolder/IShellItem and the verb) so the item is the **actual result**: for a file use the file path; for a folder use the folder path. That way the user pins exactly what they see in the table.
- **Implementation options:**
  1. **Dedicated UI:** A “Pin to Quick access” action in the result row (e.g. icon button in the path column, or first item in a small “quick actions” menu). Call a new helper e.g. `PinToQuickAccess(hwnd, path)` that resolves the path to a shell item and invokes `pintohome`.
  2. **Already available via context menu:** The existing right‑click **Explorer context menu** (ShowContextMenu) already includes “Pin to Quick access” when the user right‑clicks a result. So the feature already works for both files and folders; a **dedicated shortcut** (button or keyboard) just makes it more visible and one‑click.
- **Unpin:** The verb **`unpinfromhome`** removes the item from Quick Access; the same helper can support “Unpin from Quick access” when the item is already pinned (e.g. show different label or icon, or always offer both and let the shell toggle).
- **Caveats:** No official documented API; use Shell verbs. On some builds the verb can be slow or hang when invoked programmatically; if so, invoke asynchronously or show a brief “Pinning…” state. Quick Access data is stored in `%APPDATA%\...\AutomaticDestinations`; do not edit that file directly.

---

## 5. Global hotkey (keyboard shortcut)

### 5.1 Goal

User presses a key combination (e.g. **Ctrl+Shift+F**) from any app and FindHelper opens or comes to the foreground.

### 5.2 Mechanism (Windows)

- **RegisterHotKey(HWND, id, MOD_CONTROL | MOD_SHIFT, VK_F)** (or similar). When the key is pressed, Windows sends **WM_HOTKEY** to the window.
- **Requirements:** A valid **HWND** (you already have one via GLFW: `glfwGetWin32Window(window)`). Register at startup (after window is created), unregister on shutdown. On **WM_HOTKEY**, show the window and bring it to the front (e.g. `SetForegroundWindow`, restore if minimized).
- **Single instance (optional):** If the app is already running, the hotkey can **activate the existing window** instead of starting a second process (e.g. use a named mutex or a small launcher that sends a message to the running instance).

### 5.3 UX

- Make the hotkey **configurable** in settings (store modifier + key) so it doesn’t conflict with other apps. Default could be Ctrl+Shift+F (“F” for Find).

---

## 6. Jump List (taskbar right‑click)

### 6.1 Goal

When the user right‑clicks the FindHelper icon on the taskbar (or Start), a **Jump List** shows tasks such as “Open FindHelper” and “Search in folder…” (or “Search here” with a fixed folder).

### 6.2 Mechanism

- **App User Model ID:** Set via **SetCurrentProcessExplicitAppUserModelID** (or in the shortcut’s AppUserModel.ID and AppUserModel.RelaunchCommand) so Windows groups the app and shows a custom Jump List.
- **Custom tasks:** Use **ICustomDestinationList** and **AddUserTasks** with **IShellLink** objects. Each task has a title, icon, and **command line** (e.g. `FindHelper.exe` or `FindHelper.exe "--search-in=C:\Users\..."`). Tasks are static (same for all users) and always visible when the app is pinned.
- **Recent/pinned destinations (optional):** You can add “destinations” (e.g. recent search folders) so users can pin them for quick “Search here” from the Jump List. That requires maintaining a list of recent folders and updating the Jump List (more code).
- **C++:** Implement only on Windows; link with shell32 and use COM (ICustomDestinationList, IShellLink, IObjectCollection, etc.). Many examples exist (e.g. “ICustomDestinationList AddUserTasks” on Microsoft Learn).

### 6.3 Suggested tasks

- “Open FindHelper” → launch exe (no args).
- “Search in default folder” → launch with `--search-in=<user’s default crawl folder>` if you add that.
- “Search in Downloads” → e.g. `--crawl-folder=%USERPROFILE%\Downloads` (or similar).

---

## 7. Startup (optional “always ready”)

### 7.1 Options

- **Startup folder:** Shortcut in `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup` (user can add/remove).
- **Task Scheduler:** Task with trigger “At log on” (user or installer can create it). More control (e.g. run only when user is idle, or delay).
- **Settings > Apps > Startup:** Apps that register for the Startup list appear there; that typically requires a specific registry key or UWP-style registration.

### 7.2 Recommendation

- Prefer **user choice**: document how to add FindHelper to Startup (or provide an option in the app: “Start FindHelper when Windows starts” that creates/removes the Startup shortcut). Avoid forcing startup by default; “essential workflow” is about being **easy to launch and use**, not necessarily always running.

---

## 8. App changes that enable these workflows

| Feature | Purpose |
|--------|--------|
| **CLI initial search** | `--search=query` and/or first positional arg: pre‑fill search box, optionally run when index ready. Enables protocol, Run shortcuts, and consistent “open with query” behavior. |
| **CLI “search in folder”** | One positional argument = folder path → treat as `--crawl-folder` or new `--search-in=<path>`: open app and (optionally) set crawl folder and run search. Enables context menu, Send To, drag‑and‑drop. |
| **URI parsing** | If first arg is `findhelper:...`, parse and set initial query (and optionally folder from path in URI). |
| **Global hotkey (Windows)** | RegisterHotKey in main window; on WM_HOTKEY show/restore window. Optional: single instance so hotkey activates existing window. |
| **Jump List (Windows)** | Set AppUserModelID; use ICustomDestinationList + AddUserTasks for “Open FindHelper” and “Search in &lt;folder&gt;”. |
| **Installer / setup script** | Install exe; add App Paths; create Start Menu shortcut; optionally register `findhelper:` protocol and “Search with FindHelper” context menu verb; optionally add Send To shortcut. |

---

## 9. What to avoid (for now)

- **Windows Search index provider:** Integrating FindHelper’s index into the system index (so FindHelper results appear in Start/File Explorer search) requires implementing a **search protocol handler** and possibly a **connector** (Win32 Search APIs, COM). High effort and complexity; not necessary to be “essential” in daily workflow.
- **File Explorer search box custom provider:** The “search provider” extension is mainly for **web** search (and EU‑specific). There is no supported, lightweight way to replace or extend the **file** search in the Explorer search box with FindHelper.
- **Over‑integrating:** Too many context menu items or automatic behaviors can feel intrusive. Prefer a few, clear entry points: Run/Start, folder “Search with FindHelper,” optional hotkey, optional protocol.

---

## 10. Priority order (suggested)

1. **Run + Start:** App Paths + Start Menu shortcut (installer or script). **No app code change** if install path is fixed; only installer/script.
2. **CLI “search in folder”:** One positional arg = folder → `--crawl-folder` / `--search-in`. Unlocks context menu, Send To, drag‑and‑drop.
3. **Folder context menu:** Registry verb “Search with FindHelper” for `Directory`, command = `FindHelper.exe "--crawl-folder=%1"`.
4. **Send To shortcut:** Shortcut in `shell:sendto` pointing to FindHelper; Explorer passes folder path as argument (same as above).
5. **CLI initial search:** `--search=...` and/or positional query; pre‑fill and optionally run. Unlocks protocol and better shortcuts.
6. **Custom URL protocol:** Register `findhelper:` and parse in app.
7. **Global hotkey:** RegisterHotKey + WM_HOTKEY; show/restore window (optional single instance).
8. **Jump List:** AppUserModelID + custom tasks (“Open FindHelper”, “Search in Downloads”, etc.).
9. **Startup:** Optional, user‑controlled (document or in‑app option).

---

## 11. References

- **Context menu / static verbs:** [Extending Shortcut Menus](https://learn.microsoft.com/en-us/windows/win32/shell/context), [Creating Shortcut Menu Handlers](https://learn.microsoft.com/en-us/windows/win32/shell/context-menu-handlers)
- **App Paths:** [How the App Paths Registry Key Makes Windows Both Faster and Safer](https://helgeklein.com/blog/how-the-app-paths-registry-key-makes-windows-both-fafer-and-safer), HKLM/HKCU `SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths`
- **URI protocol:** [Registering an Application to a URI Scheme](https://learn.microsoft.com/en-us/previous-versions/windows/internet-explorer/ie-developer/platform-apis/aa767914(v=vs.85))
- **Send To:** `shell:sendto` → `%APPDATA%\Microsoft\Windows\SendTo`
- **Global hotkey:** [RegisterHotKey (winuser.h)](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerhotkey)
- **Jump List:** [ICustomDestinationList::AddUserTasks](https://learn.microsoft.com/en-us/windows/win32/api/shobjidl_core/nf-shobjidl_core-icustomdestinationlist-addusertasks), [Custom Jump List Sample](https://learn.microsoft.com/en-us/windows/win32/shell/samples-customjumplist)
- **Start menu / search:** Shortcuts in `%APPDATA%\Microsoft\Windows\Start Menu\Programs` and `ProgramData\...\Start Menu\Programs`
- **Everything “Search in folder” (inspiration):** [Voidtools forum – Right click Search Everything context menu](https://www.voidtools.com/forum/viewtopic.php?t=11894)
- **Pin to Quick Access:** Shell verb `pintohome` (and `unpinfromhome`); [Programmatically add folders to Quick Access](https://allthings.how/programmatically-add-folders-to-the-windows-10-11-quick-access-panel-in-explorer/). Windows 11 supports pinning files and folders.

---

## 12. Summary table

| Workflow | How user invokes it | Main enabler |
|----------|---------------------|---------------|
| Launch from Run/Start | Win+R or Start → type “FindHelper” | App Paths + Start Menu shortcut |
| Open with query | Run: `findhelper:search?q=foo` or shortcut | URL protocol + CLI initial search |
| Search in folder (Explorer) | Right‑click folder → “Search with FindHelper” | Registry verb + CLI folder arg |
| Search in folder (Send To) | Right‑click folder → Send to → FindHelper | Shortcut in SendTo + CLI folder arg |
| Search in folder (drag) | Drag folder onto FindHelper | CLI folder arg (drop as argument) |
| Keyboard | Ctrl+Shift+F (or configurable) | RegisterHotKey + WM_HOTKEY |
| Taskbar / Start pin | Right‑click icon → Jump List tasks | AppUserModelID + ICustomDestinationList |
| Start with Windows | (Optional) | Startup folder or Task Scheduler |
| Pin to Quick Access (from table) | Pin selected file or folder from result table to Quick Access | Shell verb `pintohome` (works for files and folders); optional dedicated button/shortcut |

Together, these make FindHelper **easy to discover, launch, and use from the main Windows 11 entry points** without depending on PowerToys.
