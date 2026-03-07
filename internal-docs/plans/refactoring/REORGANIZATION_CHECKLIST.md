# Project Reorganization Checklist

**Quick Reference:** Use this checklist to track progress during migration.

---

## Pre-Migration

- [ ] Review `PROJECT_REORGANIZATION_PLAN.md`
- [ ] Resolve questions (include path strategy, etc.)
- [ ] Create feature branch: `refactor/project-reorganization`
- [ ] Backup current state (git commit)

---

## Phase 1: Preparation

- [x] Create all new directories (`src/`, `src/core/`, `src/index/`, etc.)
- [x] Verify directory structure
- [x] Update `.gitignore` if needed (verified - no changes needed)
- [x] Create `docs/REORGANIZATION_FILE_MAPPING.md`

**Validation:** ✅ Directories exist (15 src/ subdirectories, 3 resources/ subdirectories), no build impact

---

## Phase 2: Platform-Specific Code

- [x] Create `src/platform/windows/`, `src/platform/macos/`, `src/platform/linux/` (already done in Phase 1)
- [x] Move Windows files (19 files: AppBootstrap_win.cpp, AppBootstrap.h, main_win.cpp, FileOperations_win.cpp, FileOperations.h, FontUtils_win, ThreadUtils_win, StringUtils_win, DirectXManager, ShellContextUtils, DragDropUtils, PrivilegeUtils, resource.h, resource.rc)
- [x] Move macOS files (10 files: AppBootstrap_mac, main_mac, FileOperations_mac, FontUtils_mac, ThreadUtils_mac, StringUtils_mac, MetalManager)
- [x] Move Linux files (10 files: AppBootstrap_linux, main_linux, FileOperations_linux, FontUtils_linux, ThreadUtils_linux, StringUtils_linux, OpenGLManager)
- [x] **Note:** Did NOT move AppBootstrapResultBase.h or main_common.h - they will go to src/core/ in Phase 3
- [x] Update CMakeLists.txt paths (all platform file references updated)
- [ ] Test build on Windows (pending - requires Windows environment)
- [ ] Test build on macOS (pending - requires macOS environment)
- [ ] Test build on Linux (pending - requires Linux environment)

**Validation:** ✅ Files moved (39 total), CMakeLists.txt updated. Build testing pending platform availability.

---

## Phase 3: Core Application

- [x] Create `src/core/` (already done in Phase 1)
- [x] Move `Application.cpp/h`
- [x] Move `ApplicationLogic.cpp/h`
- [x] Move `CommandLineArgs.cpp/h`
- [x] Move `Settings.cpp/h`
- [x] Move `Version.h`
- [x] Move `AppBootstrapResultBase.h` (shared across platforms)
- [x] Move `main_common.h` (shared across platforms)
- [ ] Update includes (will be done in Phase 10)
- [x] Update CMakeLists.txt (all core file paths updated)
- [ ] Test build (pending - requires platform environment)

**Validation:** ✅ Files moved (11 files), CMakeLists.txt updated. Include updates pending Phase 10.

---

## Phase 4: Index System

- [x] Create `src/index/` (already done in Phase 1)
- [x] Move 14 index-related files
- [ ] Update includes (will be done incrementally)
- [x] Update CMakeLists.txt
- [ ] Test build (pending)
- [ ] Run index tests (pending)

**Validation:** ✅ Files moved, CMakeLists.txt updated

---

## Phase 5: Search Components

- [x] Create `src/search/` (already done in Phase 1)
- [x] Move 16 search-related files
- [ ] Update includes (will be done incrementally)
- [x] Update CMakeLists.txt
- [ ] Test build (pending)
- [ ] Run search tests (pending)

**Validation:** ✅ Files moved, CMakeLists.txt updated

---

## Phase 6: Path Operations

- [x] Create `src/path/` (already done in Phase 1)
- [x] Move 13 path-related files
- [ ] Update includes (will be done incrementally)
- [x] Update CMakeLists.txt
- [ ] Test build (pending)
- [ ] Run path tests (pending)

**Validation:** ✅ Files moved, CMakeLists.txt updated

---

## Phase 7: Remaining Components

- [x] Create `src/usn/` and move 3 files
- [x] Create `src/crawler/` and move 4 files
- [x] Create `src/utils/` and move 24 utility files
- [x] Create `src/filters/` and move 6 files
- [x] Create `src/gui/` and move 4 files
- [x] Create `src/api/` and move 6 API files
- [x] Move `ui/` → `src/ui/`
- [ ] Update all includes (will be done incrementally)
- [x] Update CMakeLists.txt
- [ ] Test build on all platforms (pending)

**Validation:** ✅ All components moved (47 files), CMakeLists.txt updated

---

## Phase 8: Resources

- [x] Create `resources/icons/` and `resources/windows/` (already done in Phase 1)
- [x] Move icon files (app_icon.icns, app_icon.ico)
- [x] Move manifest file (app.manifest)
- [x] Update CMakeLists.txt resource paths
- [ ] Test build (pending)
- [ ] Verify icons display (pending)

**Validation:** ✅ Resources moved (3 files), CMakeLists.txt updated

---

## Phase 9: Documentation

- [x] Move root-level `.md` files to `docs/` (11 files)
- [x] Move utility scripts to `scripts/` (count_lines.py)
- [x] Clean up root directory
- [x] Verify only essential files remain in root

**Validation:** ✅ Documentation organized (11 files moved)

---

## Phase 10: Include Paths

- [x] Decide on include path strategy (Option B: source root includes)
- [x] Update CMakeLists.txt include directories (added src/ to all targets)
- [ ] Update all include statements in source files (will be done incrementally as files are modified)
- [x] Update platform-specific includes (CMakeLists.txt updated)
- [ ] Test build on all platforms (pending)

**Validation:** ✅ Include directories configured. Source file includes will be updated incrementally.

---

## Phase 11: Update Tests

- [ ] Update test includes
- [ ] Update test CMakeLists.txt if needed
- [ ] Build tests on all platforms
- [ ] Run full test suite
- [ ] Verify all tests pass

**Validation:** All tests build and pass

---

## Phase 12: Final Validation

- [ ] Clean build on Windows
- [ ] Clean build on macOS
- [ ] Clean build on Linux
- [ ] Run application on all platforms
- [ ] Update README.md
- [ ] Update CMakeLists.txt comments
- [ ] Create migration summary
- [ ] Update `docs/REORGANIZATION_FILE_MAPPING.md`

**Validation:** Everything works, documentation updated

---

## Post-Migration

- [ ] Review changes with team
- [ ] Merge feature branch to main
- [ ] Update any CI/CD scripts if needed
- [ ] Celebrate! 🎉

---

## Quick File Count Reference

| Category | Files to Move |
|----------|---------------|
| Platform (Windows) | 13 |
| Platform (macOS) | 7 |
| Platform (Linux) | 5 |
| Core | 7 |
| Index | 8 |
| Search | 10 |
| Path | 7 |
| USN | 2 |
| Crawler | 2 |
| Utils | ~20 |
| Filters | 4 |
| GUI | 3 |
| API | 5 |
| UI (existing) | 8 |
| Resources | 3-4 |
| Documentation | ~10 |
| **Total** | **~120 files** |

**Note:** Updated January 4, 2026 to reflect new files added since original plan.

---

**Note:** This is a quick reference. See `PROJECT_REORGANIZATION_PLAN.md` for detailed instructions.


