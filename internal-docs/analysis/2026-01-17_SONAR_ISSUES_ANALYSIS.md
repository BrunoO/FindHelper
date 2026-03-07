# SonarQube Issues Created Today - Analysis

**Date:** 2026-01-17  
**Analysis:** New SonarQube issues created today and their relation to clang-tidy warning fixes

## Summary

**Total new issues today:** 14

### Issues by Rule

| Rule | Count | Description | Relation to clang-tidy fixes |
|------|-------|-------------|----------------------------|
| `cpp:S3230` | 8 | Use in-class initializers instead of constructor initializer list | **Directly related** - We fixed member initialization warnings by adding constructor initializer lists, but SonarQube prefers in-class initializers |
| `cpp:S5277` | 3 | Not directly mapped | Unrelated to today's fixes |
| `cpp:S1181` | 2 | Exception handling | Unrelated to today's fixes |
| `cpp:S886` | 1 | Not directly mapped | Unrelated to today's fixes |

### Issues by File

| File | Count | Status |
|------|-------|--------|
| `src/path/PathStorage.cpp` | 8 | **FIXED** - Moved initializers to in-class initializers in header |
| `tests/TestHelpers.cpp` | 3 | Unrelated to today's fixes |
| `src/search/SearchResultUtils.cpp` | 2 | Unrelated to today's fixes |
| `src/search/ParallelEngine.h` | 1 | Unrelated to today's fixes |

## Details: PathStorage.cpp Issues

All 8 issues in `PathStorage.cpp` were related to rule `cpp:S3230`:

**Problem:** We fixed clang-tidy member initialization warnings by adding constructor initializer lists:
```cpp
PathStorage::PathStorage()
    : path_storage_{}
    , path_offsets_{}
    // ... etc
```

**SonarQube Rule cpp:S3230:** Prefers in-class initializers for default values instead of constructor initializer lists.

**Solution:** Moved initializers to in-class initializers in the header file:
```cpp
// In PathStorage.h
std::vector<char> path_storage_{};
std::vector<size_t> path_offsets_{};
// ... etc
```

This satisfies both:
- **clang-tidy:** Members are explicitly initialized (no uninitialized warnings)
- **SonarQube:** Uses in-class initializers (preferred modern C++ style)

## Fix Applied

**Commit:** `9c55e63` - "Fix SonarQube cpp:S3230: Use in-class initializers instead of constructor initializer list"

**Changes:**
- Moved `{}` initializers from constructor initializer list to in-class initializers in `PathStorage.h`
- Removed redundant constructor initializer list entries
- All tests pass
- No clang-tidy warnings introduced

## Next Steps

The remaining 6 issues (3 in `TestHelpers.cpp`, 2 in `SearchResultUtils.cpp`, 1 in `ParallelEngine.h`) are unrelated to today's clang-tidy warning fixes and can be addressed separately.

## Notes

- The SonarQube JSON file (`sonar-results/sonarqube_issues.json`) is a snapshot from the last SonarQube scan
- New scans will reflect the fixes we made
- The script `scripts/check_today_sonar_issues.py` can be used to check for new issues after SonarQube runs
