# Comprehensive Guide: Parsing NTFS Recycle Bin Artifacts ($I and $R) in C++

This document provides a comprehensive technical overview and an production-ready C++17 implementation for parsing Windows Recycle Bin structures (`$I` metadata files) and mapping them to their corresponding payload files (`$R` data files).

## 1. Architectural Concepts: How Soft Delete Works
When a file is deleted via Windows File Explorer or standard desktop actions, the operation is a "soft delete." Instead of unallocating the file clusters immediately, the operating system executes the following steps:
1. **Relocation:** The file is moved from its original parent directory into the hidden system directory located at the volume root: `\$Recycle.Bin\<User-SID>\`.
2. **Obfuscation & Splitting:** The file is split into two distinct entities sharing a matching pseudo-random 6-character identifier (e.g., `XXXXXX`):
   * **`$RXXXXXX` (Data File):** The raw, unaltered payload contents of the deleted file.
   * **`$IXXXXXX` (Metadata File):** A small binary record containing forensic details about the deletion event.
3. **MFT Update:** The Master File Table updates the parent directory index records, logging the file under the local user's Recycle Bin security identifier folder.

---

## 2. Binary Layouts of $I Metadata Files

Windows utilizes two primary versions for the `$I` file structural layout depending on the OS version. Both structures maintain a fixed-size header followed by the original absolute file path.

### Windows 7, 8, and 8.1 (Version 1)
* **Header Size:** 20 bytes (0x14)
* **Format:** Fixed structural layout mapping.

| Offset (Hex) | Size (Bytes) | Data Type | Description |
|:---|:---|:---|:---|
| 0x00 | 8 | uint64_t | Structure Version (0x01) |
| 0x08 | 8 | uint64_t | Original File Size (in bytes) |
| 0x10 | 8 | uint64_t | Deletion Timestamp (Win32 FILETIME) |
| 0x18 | 520 | wchar_t[260] | Original Absolute File Path (Null-terminated UTF-16LE) |

### Windows 10 and 11 (Version 2)
* **Header Size:** 28 bytes (0x1C)
* **Format:** Variable or long-path capability.

| Offset (Hex) | Size (Bytes) | Data Type | Description |
|:---|:---|:---|:---|
| 0x00 | 8 | uint64_t | Structure Version (0x02) |
| 0x08 | 8 | uint64_t | Original File Size (in bytes) |
| 0x10 | 8 | uint64_t | Deletion Timestamp (Win32 FILETIME) |
| 0x18 | 4 | uint32_t | Absolute Path Character Count (Length including null terminator) |
| 0x1C | Variable | wchar_t[] | Original Absolute File Path (Null-terminated UTF-16LE) |

---

## 3. Mapping $I Files to $R Payload Data
To programmatically link metadata records to their active disk payloads, implement a name-matching heuristic based on the shared 6-character identifier:
1. Iterate over the targeted `$Recycle.Bin\<SID>\` directory.
2. Filter for files starting with the prefix `$I`.
3. Extract the unique suffix string (the characters between `$I` and the file extension, if any).
4. Construct the matching data file string by prepending `$R` to that same identifier string.
5. If the `$R` file exists, the metadata matches a valid payload available for programmatic restoration or processing.

---

## 4. Implementation: C++17 Cross-Version Parser

```cpp
/**
 * @file ntfs_recycle_bin_parser.cpp
 * @brief Comprehensive C++17 parser for Windows Recycle Bin $I/$R artifacts.
 * 
 * GENERAL EXPLANATION ABOUT SOFT DELETE:
 * When a user deletes a file on an NTFS volume without permanently bypassing the 
 * Recycle Bin (e.g., via Shift+Delete), Windows does not purge the data blocks 
 * from the disk or mark the MFT record as free. Instead, it renames and relocates 
 * the file to a hidden system container named `\$Recycle.Bin\<User-SID>\`.
 * 
 * During this move, Windows splits the entity into two files sharing a unique, 
 * matching 6-character alphanumeric sequence:
 * 1. $RXXXXXX - The actual content payload (the raw data clusters remain untouched).
 * 2. $IXXXXXX - The index metadata file containing structural details such as the 
 *    original file size, the exact moment of deletion, and the original absolute 
 *    filepath needed to execute a restore operation.
 * 
 * This program reads these binary structures across different Windows versions, 
 * decodes the Win32 FILETIME timestamps into human-readable format, and maps 
 * the index records directly to their corresponding data payload on disk.
 */

#include <iostream>
#include <fstream>
#include <filesystem>
#include <string>
#include <vector>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cstdint>

namespace fs = std::filesystem;

#pragma pack(push, 1)
// Fixed structural layout for Windows 7 / 8 / 8.1 ($I Version 1)
struct RecycleBinHeaderV1 {
    uint64_t version;
    uint64_t originalFileSize;
    uint64_t deletionTimestamp;
    wchar_t  originalPath[260];
};

// Variable layout header for Windows 10 / 11 ($I Version 2)
struct RecycleBinHeaderV2 {
    uint64_t version;
    uint64_t originalFileSize;
    uint64_t deletionTimestamp;
    uint32_t pathLength; 
};
#pragma pack(pop)

// Converts a 64-bit Windows FILETIME timestamp to an ISO 8601 string
std::string FormatFileTime(uint64_t fileTime) {
    if (fileTime == 0) return "N/A";
    
    // Windows FILETIME measures 100-nanosecond intervals since January 1, 1601.
    // Convert to Unix Epoch (seconds since January 1, 1970).
    constexpr uint64_t WINDOWS_TICK = 10000000ULL;
    constexpr uint64_t SEC_TO_UNIX_EPOCH = 11644473600ULL;
    
    time_t unixTime = static_cast<time_t>((fileTime / WINDOWS_TICK) - SEC_TO_UNIX_EPOCH);
    std::tm tm_info;
    
#if defined(_MSC_VER)
    if (gmtime_s(&tm_info, &unixTime) != 0) return "Invalid Time";
#else
    if (!gmtime_r(&unixTime, &tm_info)) return "Invalid Time";
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_info, "%Y-%m-%d %H:%M:%S UTC");
    return oss.str();
}

// Converts a UTF-16 wide string to a standard UTF-8 string for generic console output
std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    std::string out;
    for (wchar_t wc : wstr) {
        if (wc < 0x80) {
            out.push_back(static_cast<char>(wc));
        } else if (wc < 0x800) {
            out.push_back(static_cast<char>(0xC0 | ((wc >> 6) & 0x1F)));
            out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xE0 | ((wc >> 12) & 0x0F)));
            out.push_back(static_cast<char>(0x80 | ((wc >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (wc & 0x3F)));
        }
    }
    return out;
}

// Parses an individual $I metadata file and extracts structural contents
void ParseMetadataFile(const fs::path& filePath) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "[!] Failed to open file: " << filePath.string() << std::endl;
        return;
    }

    uint64_t versionCheck = 0;
    if (!file.read(reinterpret_cast<char*>(&versionCheck), sizeof(versionCheck))) {
        return;
    }
    
    // Rewind back to file beginning to perform clean structured reads
    file.seekg(0, std::ios::beg);

    uint64_t fileSize = 0;
    uint64_t deletionTime = 0;
    std::wstring originalPath = L"";

    if (versionCheck == 1) {
        RecycleBinHeaderV1 headerV1;
        if (!file.read(reinterpret_cast<char*>(&headerV1), sizeof(headerV1))) return;
        
        fileSize = headerV1.originalFileSize;
        deletionTime = headerV1.deletionTimestamp;
        originalPath = std::wstring(headerV1.originalPath);
    } 
    else if (versionCheck == 2) {
        RecycleBinHeaderV2 headerV2;
        if (!file.read(reinterpret_cast<char*>(&headerV2), sizeof(headerV2))) return;
        
        fileSize = headerV2.originalFileSize;
        deletionTime = headerV2.deletionTimestamp;
        
        std::vector<wchar_t> pathBuffer(headerV2.pathLength);
        if (file.read(reinterpret_cast<char*>(pathBuffer.data()), headerV2.pathLength * sizeof(wchar_t))) {
            originalPath = std::wstring(pathBuffer.data());
        }
    } 
    else {
        std::cout << "[*] Skipped: Unsupported or corrupted header layout version (" 
                  << versionCheck << ") in " << filePath.filename().string() << std::endl;
        return;
    }

    // Identify and map to the corresponding $R payload data file
    std::string filenameStr = filePath.filename().string();
    std::string rFilenameStr = filenameStr;
    if (rFilenameStr.length() >= 2 && rFilenameStr[0] == '$' && rFilenameStr[1] == 'I') {
        rFilenameStr[1] = 'R'; // Mutate prefix identifier from $I to $R
    }
    
    fs::path rFilePath = filePath.parent_path() / rFilenameStr;
    bool payloadExists = fs::exists(rFilePath);

    // Present parsed records cleanly to console
    std::cout << "=========================================================
"
              << "Metadata File    : " << filenameStr << "
"
              << "Format Version   : Windows " << (versionCheck == 1 ? "7/8 (v1)" : "10/11 (v2)") << "
"
              << "Original Path    : " << WideToUtf8(originalPath) << "
"
              << "Original Size    : " << fileSize << " bytes
"
              << "Deletion Time    : " << FormatFileTime(deletionTime) << "
"
              << "Mapped Data File : " << rFilenameStr << "
"
              << "Payload Status   : " << (payloadExists ? "AVAILABLE ON DISK" : "MISSING/OVERWRITTEN") << "
";
}

int main(int argc, char* argv[]) {
    std::string targetPath = "."; // Default search path context
    if (argc > 1) {
        targetPath = argv[1];
    }

    std::cout << "[*] Scanning target location: " << targetPath << std::endl;

    try {
        if (!fs::exists(targetPath) || !fs::is_directory(targetPath)) {
            std::cerr << "[!] Error: Target path is not a valid directory." << std::endl;
            return 1;
        }

        for (const auto& entry : fs::recursive_directory_iterator(targetPath)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                // Filter for metadata files matching $I prefix layout logic
                if (filename.length() >= 2 && filename[0] == '$' && filename[1] == 'I') {
                    ParseMetadataFile(entry.path());
                }
            }
        }
    } 
    catch (const std::exception& e) {
        std::cerr << "[!] Standard exception encountered: " << e.what() << std::endl;
    }

    return 0;
}
```

---

## 5. Compilation Reference

Ensure your toolchain flags target at least the **C++17** specification standard to support the modern cross-platform `<filesystem>` implementations cleanly.

### Microsoft Visual Studio Compiler (MSVC)
```bash
cl /EHsc /std:c++17 ntfs_recycle_bin_parser.cpp
```

### GCC / MinGW-w64 (Windows Environment)
```bash
g++ -std=c++17 -O2 ntfs_recycle_bin_parser.cpp -o ntfs_recycle_bin_parser.exe -lstdc++fs
```
