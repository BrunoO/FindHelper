# Parameter Name Proposals: `--default-root-crawl`

**Current Name:** `--default-root-crawl`  
**Purpose:** Specifies a folder path to crawl/index when no index file is provided  
**Usage Context:**
- Windows: Used when no admin rights (alternative to USN Journal)
- macOS/Linux: Required if no index file is provided
- Uses `FolderCrawler` to build the index by scanning directory tree

---

## Analysis of Current Name

### Issues with `--default-root-crawl`:
1. ❌ **"default" is misleading** - It's not a default value, it's an explicit option
2. ❌ **"root" is ambiguous** - Could be confused with filesystem root (`/` or `C:\`)
3. ✅ **"crawl" is clear** - Indicates directory traversal
4. ❌ **Too long** - 20 characters (with dashes)

---

## Proposed Alternatives

### 🥇 **Top Recommendation: `--crawl-folder`**

**Pros:**
- ✅ Short and clear (13 characters)
- ✅ "crawl" clearly indicates directory traversal
- ✅ "folder" is intuitive and platform-agnostic
- ✅ Matches the action: "crawl this folder"
- ✅ Easy to remember

**Cons:**
- ⚠️ Doesn't explicitly mention it's an alternative to USN Journal

**Example Usage:**
```bash
./FindHelper --crawl-folder /home/user/documents
./FindHelper --crawl-folder C:\Users\Documents
```

**Help Text:**
```
  --crawl-folder=<path>        Folder to crawl and index (alternative to USN Journal)
                               (macOS/Linux: required if no index file)
                               (Windows: used when no admin rights)
```

---

### 🥈 **Alternative 1: `--index-folder`**

**Pros:**
- ✅ Clear purpose: "index this folder"
- ✅ Consistent with `--index-from-file` naming
- ✅ Short (14 characters)
- ✅ "index" is the end goal

**Cons:**
- ⚠️ Could be confused with `--index-from-file` (both start with "index")

**Example Usage:**
```bash
./FindHelper --index-folder /home/user/documents
```

**Help Text:**
```
  --index-folder=<path>        Folder to index by crawling (alternative to USN Journal)
```

---

### 🥉 **Alternative 2: `--scan-folder`**

**Pros:**
- ✅ "scan" is intuitive and commonly used
- ✅ Short (13 characters)
- ✅ Clear action verb

**Cons:**
- ⚠️ "scan" might imply a quick operation (but crawling can be slow)
- ⚠️ Less specific than "crawl"

**Example Usage:**
```bash
./FindHelper --scan-folder /home/user/documents
```

---

### **Alternative 3: `--crawl-path`**

**Pros:**
- ✅ Short (12 characters)
- ✅ "path" is more generic (works for files/folders)
- ✅ Consistent with other path parameters

**Cons:**
- ⚠️ "path" is less intuitive than "folder"
- ⚠️ Could be confused with file paths

**Example Usage:**
```bash
./FindHelper --crawl-path /home/user/documents
```

---

### **Alternative 4: `--folder-crawl`**

**Pros:**
- ✅ Noun-first pattern (like `--index-file`)
- ✅ Clear and descriptive

**Cons:**
- ⚠️ Slightly less natural word order
- ⚠️ Same length as current name

**Example Usage:**
```bash
./FindHelper --folder-crawl /home/user/documents
```

---

### **Alternative 5: `--build-index-from`**

**Pros:**
- ✅ Very explicit about the action
- ✅ Makes it clear this builds the index

**Cons:**
- ❌ Too long (19 characters)
- ❌ Verbose
- ❌ Less intuitive

**Example Usage:**
```bash
./FindHelper --build-index-from /home/user/documents
```

---

## Comparison Table

| Name | Length | Clarity | Intuitiveness | Consistency | Recommendation |
|------|--------|---------|---------------|-------------|----------------|
| `--default-root-crawl` | 20 | ⭐⭐⭐ | ⭐⭐ | ⭐⭐ | ❌ Current (has issues) |
| `--crawl-folder` | 13 | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ✅ **BEST** |
| `--index-folder` | 14 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ✅ Good |
| `--scan-folder` | 13 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐ | ⭐⭐⭐ | ✅ Good |
| `--crawl-path` | 12 | ⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐⭐ | ⚠️ Acceptable |
| `--folder-crawl` | 14 | ⭐⭐⭐⭐ | ⭐⭐⭐ | ⭐⭐⭐ | ⚠️ Acceptable |
| `--build-index-from` | 19 | ⭐⭐⭐⭐⭐ | ⭐⭐ | ⭐⭐ | ❌ Too verbose |

---

## Recommendation Summary

### 🏆 **Primary Recommendation: `--crawl-folder`**

**Rationale:**
1. **Shortest and clearest** - Easy to type and remember
2. **Action-oriented** - "crawl this folder" is intuitive
3. **Platform-agnostic** - "folder" works on all platforms
4. **Consistent pattern** - Verb-noun structure matches other options
5. **No ambiguity** - Clear what it does

### 📝 **Migration Notes**

If changing the parameter name:

1. **Backward Compatibility:** Consider supporting both names temporarily:
   ```cpp
   // Support both old and new names
   if (strncmp(arg, "--default-root-crawl=", 21) == 0 ||
       strncmp(arg, "--crawl-folder=", 15) == 0) {
     // ... parse value
   }
   ```

2. **Update Documentation:**
   - `CommandLineArgs.cpp` - Help text
   - `AppBootstrap.cpp` - Error messages
   - `AppBootstrap_linux.cpp` - Error messages
   - `AppBootstrap_mac.mm` - Error messages
   - `BUILDING_ON_LINUX.md` - Examples
   - Any other docs mentioning the parameter

3. **Variable Name:** Consider renaming `default_root_crawl` to `crawl_folder` for consistency:
   ```cpp
   std::string crawl_folder; // Instead of default_root_crawl
   ```

---

## Examples with Recommended Name

### Help Text:
```
Options:
  --index-from-file=<file>      Load index from a text file (one path per line)
  --crawl-folder=<path>          Crawl and index a folder (alternative to USN Journal)
                                 (macOS/Linux: required if no index file)
                                 (Windows: used when no admin rights and no index file)
```

### Error Messages:
```
// Windows
"Use --crawl-folder=<path> to crawl a folder without admin rights"

// macOS/Linux
"On macOS, either --index-from-file or --crawl-folder must be provided"
```

### Usage Examples:
```bash
# Linux
./FindHelper --crawl-folder /home/user/documents

# macOS
./FindHelper --crawl-folder ~/Documents

# Windows (no admin)
FindHelper.exe --crawl-folder C:\Users\Documents
```

---

## Final Recommendation

**Change `--default-root-crawl` to `--crawl-folder`**

This name is:
- ✅ 35% shorter (13 vs 20 characters)
- ✅ More intuitive and clear
- ✅ Better aligned with user mental model
- ✅ Easier to remember and type

