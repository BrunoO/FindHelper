# Matcher Simplification Proposal

## Current State

We have **5 matchers** with **3 prefixes**:
1. **Substring** - No prefix (default)
2. **Glob** - No prefix (auto-detected by `*` or `?`)
3. **SimpleRegex** - `re:` prefix
4. **PathPattern** - `pp:` prefix  
5. **StdRegex** - `rs:` prefix

## Problem

Users need to remember prefixes (`re:`, `pp:`, `rs:`) which adds cognitive load. The `re:` prefix is particularly redundant since PathPattern supports all SimpleRegex features plus more.

## Analysis

### SimpleRegex (`re:`) vs PathPattern (`pp:`)

**SimpleRegex supports:**
- `.` - any character
- `*` - zero or more
- `^` - start anchor
- `$` - end anchor

**PathPattern supports:**
- ✅ All SimpleRegex features (`.`, `*`, `^`, `$`)
- ✅ Plus: `**` (cross-directory), `?`, character classes `[abc]`, quantifiers `{n}`, shorthands `\d`, `\w`
- ✅ Faster (DFA-optimized)
- ✅ Better performance

**Conclusion**: PathPattern is a strict superset of SimpleRegex. We can eliminate `re:`.

### StdRegex (`rs:`) vs PathPattern

**StdRegex supports:**
- Full ECMAScript regex
- Alternation: `(a|b)`
- Groups, lookahead, etc.

**PathPattern supports:**
- Most regex features but NOT alternation `(a|b)`
- Character classes, quantifiers, anchors

**Conclusion**: StdRegex is still needed for complex patterns with alternation.

## Proposed Simplification

### Option 1: Remove `re:` Prefix (Recommended)

**Changes:**
- Remove `re:` prefix
- Auto-detect patterns with `^` or `$` anchors → use PathPattern
- Keep `pp:` for explicit path patterns (optional, for clarity)
- Keep `rs:` for full regex

**New matchers:**
1. **Substring** - No prefix (default)
2. **Glob** - Auto-detected (`*` or `?`)
3. **PathPattern** - Auto-detected (`^` or `$`) OR `pp:` prefix (optional)
4. **StdRegex** - `rs:` prefix (for complex regex with `|`, `(`, `)`)

**Benefits:**
- ✅ Eliminates one prefix (`re:`)
- ✅ PathPattern is faster and more powerful than SimpleRegex
- ✅ Backward compatible (existing `re:` patterns can be migrated)
- ✅ Simpler for users (one less thing to remember)

**Migration:**
- `re:^main` → `^main` (auto-detected as PathPattern)
- `re:\.cpp$` → `\.cpp$` (auto-detected as PathPattern)
- `pp:src/**/*.cpp` → Still works (explicit prefix)
- `pp:src/**/*.cpp` → `src/**/*.cpp` (could also auto-detect `**`)

### Option 2: Remove `re:` and Make `pp:` Optional

**Changes:**
- Remove `re:` prefix
- Auto-detect PathPattern by:
  - Contains `^` or `$` (anchors)
  - Contains `**` (cross-directory)
  - Contains `[` (character classes)
  - Contains `\d`, `\w` (shorthands)
  - Contains `{` (quantifiers)
- Make `pp:` prefix optional (for explicit control)
- Keep `rs:` for full regex

**New matchers:**
1. **Substring** - No prefix (default)
2. **Glob** - Auto-detected (`*` or `?` but not `**`)
3. **PathPattern** - Auto-detected (advanced features) OR `pp:` prefix (optional)
4. **StdRegex** - `rs:` prefix OR auto-detected (contains `|` or `(`)

**Benefits:**
- ✅ Eliminates `re:` prefix
- ✅ Makes `pp:` optional (auto-detection)
- ✅ Even simpler for users

**Risks:**
- ⚠️ Auto-detection might be ambiguous (e.g., `(test)` could be PathPattern quantifier or StdRegex group)
- ⚠️ Need careful detection logic

### Option 3: Remove All Prefixes, Smart Auto-Detection

**Changes:**
- Remove all prefixes (`re:`, `pp:`, `rs:`)
- Smart auto-detection:
  - Contains `|` or `(` → StdRegex (complex regex)
  - Contains `^`, `$`, `**`, `[`, `\d`, `\w`, `{` → PathPattern
  - Contains `*` or `?` (but not `**`) → Glob
  - Default → Substring

**Benefits:**
- ✅ No prefixes to remember
- ✅ Maximum simplicity

**Risks:**
- ⚠️ Ambiguity issues (e.g., `(test)` could be PathPattern or StdRegex)
- ⚠️ Users lose explicit control
- ⚠️ Harder to debug (which matcher is being used?)

## Recommendation

**Option 1** is the best balance:
- ✅ Removes redundant `re:` prefix
- ✅ PathPattern is better than SimpleRegex anyway
- ✅ Keeps `pp:` and `rs:` for explicit control when needed
- ✅ Minimal risk, easy migration
- ✅ Backward compatible (can support `re:` as deprecated alias)

## Implementation Plan

1. **Update `DetectPatternType()`**:
   - Remove `re:` prefix check
   - Add auto-detection: if pattern contains `^` or `$` → PathPattern
   - Keep `pp:` prefix (optional, for explicit control)

2. **Update documentation**:
   - Remove `re:` from all docs
   - Update examples to use PathPattern or auto-detection
   - Add migration guide

3. **Backward compatibility** (optional):
   - Support `re:` as deprecated alias (warn user, use PathPattern)
   - Remove after deprecation period

4. **Update tests**:
   - Remove `re:` tests
   - Add PathPattern auto-detection tests
   - Update examples

## Examples After Simplification

### Before (Current)
```
re:^main        → SimpleRegex
re:\.cpp$       → SimpleRegex
pp:src/**/*.cpp → PathPattern
rs:^main\.(cpp|h)$ → StdRegex
*.txt           → Glob
file            → Substring
```

### After (Option 1)
```
^main           → PathPattern (auto-detected by ^)
\.cpp$          → PathPattern (auto-detected by $)
pp:src/**/*.cpp → PathPattern (explicit prefix, optional)
src/**/*.cpp    → PathPattern (auto-detected by **)
rs:^main\.(cpp|h)$ → StdRegex (needs alternation)
*.txt           → Glob
file            → Substring
```

## Questions to Consider

1. **Should we keep `pp:` prefix optional?**
   - Pro: Explicit control, clarity
   - Con: Still requires remembering a prefix

2. **Should we auto-detect `**` as PathPattern?**
   - Pro: More intuitive (glob-like but with `**`)
   - Con: Might conflict with literal `**` in filenames (rare)

3. **Should we support `re:` as deprecated alias?**
   - Pro: Backward compatibility
   - Con: Adds complexity, temporary code

4. **What about StdRegex auto-detection?**
   - Could auto-detect patterns with `|` or `(` as StdRegex
   - But PathPattern also uses `(` for quantifiers `{n}`
   - Might need `rs:` prefix to disambiguate

## Decision

**Recommended: Option 1** - Remove `re:` prefix, keep `pp:` and `rs:` optional.

This provides the best balance of simplicity and control.

---

## Implementation Status

✅ **IMPLEMENTED** - Option 1 has been fully implemented:
- ✅ Removed `SimpleRegex` from `PatternType` enum
- ✅ Updated `DetectPatternType()` to auto-detect PathPattern for patterns with `^` or `$`
- ✅ **EXPANDED:** Auto-detect PathPattern for patterns with both `.` and `*` (SimpleRegex patterns)
- ✅ Removed all `re:` prefix handling
- ✅ Updated UI files (Popups.cpp, EmptyState.cpp)
- ✅ Updated GeminiApiUtils documentation
- ✅ Updated tests to use PathPattern instead of `re:`
- ✅ Updated SearchContext.h comments

**Performance Optimization (2026-01-04):**
- ✅ Expanded PathPattern auto-detection to include patterns with both `.` and `*`
- ✅ Ensures all SimpleRegex-compatible patterns are pre-compiled and use DFA optimization
- ✅ Prevents performance regression for patterns like `.*test`, `test.*`, etc.

**Migration Guide:**
- `re:^main` → `^main` (auto-detected as PathPattern)
- `re:\.cpp$` → `\.cpp$` (auto-detected as PathPattern)
- `pp:src/**/*.cpp` → Still works (explicit prefix, optional)





