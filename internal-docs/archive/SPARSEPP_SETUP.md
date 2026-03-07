# FAST_LIBS Setup Guide

## Overview
The project supports three hash map implementations via feature flags:
1. **`std::unordered_map`** (default) - Standard library implementation
2. **`ankerl::unordered_dense`** (`FAST_LIBS=ON`) - Better memory efficiency, header-only
3. **`boost::unordered_map`** (`FAST_LIBS_BOOST=ON`) - Faster lookups (Boost 1.80+), stable references

## Comparison

| Feature | std::unordered_map | ankerl::unordered_dense | boost::unordered_map |
|---------|-------------------|------------------------|---------------------|
| **Memory overhead** | ~20-30 bytes/entry | ~1 byte/entry | ~12 bytes/entry |
| **Lookup performance** | Baseline | 10-30% faster | ~2x faster |
| **Insert/erase** | Baseline | Faster | Slower |
| **Stable references** | ✅ Yes | ❌ No | ✅ Yes |
| **Dependency** | None (standard) | Git submodule | Boost 1.80+ |
| **Best for** | Default, compatibility | Memory-constrained | Lookup-heavy workloads |

## Option 1: std::unordered_map (Default)

No setup required - this is the default when no flags are set.

```bash
# Build with standard library (default)
nmake
# or
cmake ..
cmake --build .
```

## Option 2: ankerl::unordered_dense (FAST_LIBS)

### Benefits
- **Memory**: ~1 byte overhead per entry vs ~20-30 bytes for `std::unordered_map`
- **Performance**: Typically 10-30% faster lookups
- **Compatibility**: Drop-in replacement (same API)

### Setup Instructions

#### 1. Initialize Git Submodules

External dependencies (ImGui, unordered_dense, etc.) are included as **Git Submodules**.

**First-time setup:**
```bash
# Initialize all submodules (including unordered_dense)
git submodule update --init --recursive
```

**If you've already cloned the repository:**
```bash
# Just initialize unordered_dense submodule
git submodule update --init external/unordered_dense
```

The unordered_dense repository will be cloned into `external/unordered_dense/` with headers under `include/ankerl/`.

#### 2. Enable FAST_LIBS

**Using Makefile (NMAKE):**
```bash
# Build with FAST_LIBS (unordered_dense) enabled
nmake FAST_LIBS=1

# Or set environment variable
set FAST_LIBS=1
nmake
```

**Using CMake:**
```bash
# Configure with FAST_LIBS enabled
cmake -DFAST_LIBS=ON ..

# Build
cmake --build .
```

## Option 3: boost::unordered_map (FAST_LIBS_BOOST)

### Benefits
- **Performance**: ~2x faster lookups than `std::unordered_map` (Boost 1.80+)
- **Stable references**: Like `std::unordered_map`, maintains pointer/iterator stability
- **Compatibility**: Drop-in replacement (same API)

### Setup Instructions

#### 1. Install Boost 1.80 or Later

**Windows (vcpkg - Recommended):**
vcpkg is a C++ package manager that CMake can automatically use. This is the easiest way to install Boost:

```bash
# 1. Install vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
.\bootstrap-vcpkg.bat

# 2. Install Boost (header-only, no compilation needed for unordered_map)
.\vcpkg install boost:x64-windows

# Note: For 32-bit builds, use: .\vcpkg install boost:x86-windows
```

Then configure CMake with the vcpkg toolchain:
```bash
cmake -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake -DFAST_LIBS_BOOST=ON ..
cmake --build .
```

**Windows (Manual Installation):**
1. Download Boost from https://www.boost.org/
2. Extract to a location like `C:\boost_1_80_0`
3. Set `BOOST_ROOT` environment variable (optional, but recommended):
   ```bash
   set BOOST_ROOT=C:\boost_1_80_0
   ```

**Linux:**
```bash
# Ubuntu/Debian
sudo apt-get install libboost-dev

# Or build from source
wget https://boostorg.jfrog.io/artifactory/main/release/1.80.0/source/boost_1_80_0.tar.gz
tar -xzf boost_1_80_0.tar.gz
```

#### 2. Enable FAST_LIBS_BOOST

**Using CMake (with vcpkg - Recommended):**
```bash
# Configure with vcpkg toolchain (CMake will automatically use Boost from vcpkg)
cmake -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake -DFAST_LIBS_BOOST=ON ..

# Build
cmake --build .
```

**Using CMake (with manual Boost installation):**
```bash
# CMake will automatically find Boost via find_package
cmake -DFAST_LIBS_BOOST=ON ..

# Or specify Boost root manually if not in system paths
cmake -DFAST_LIBS_BOOST=ON -DBOOST_ROOT=C:/boost_1_80_0 ..

# Build
cmake --build .
```

**Using Makefile (NMAKE):**
```bash
# Set Boost root (if not in common paths)
set BOOST_ROOT=C:\boost_1_80_0

# Build with Boost
nmake FAST_LIBS_BOOST=1

# Or set environment variable
set FAST_LIBS_BOOST=1
nmake
```

**Note:** If both `FAST_LIBS` and `FAST_LIBS_BOOST` are set, `FAST_LIBS_BOOST` takes precedence.

## Verification

After building, check the status bar in the application:
- **Default**: Shows "(Debug)" or "(Release)"
- **FAST_LIBS**: Shows "(Debug, FS)" or "(Release, FS)"
- **FAST_LIBS_BOOST**: Shows "(Debug, Boost)" or "(Release, Boost)"

### Files Using Hash Maps (all options):
- `FileIndex.h` - Main file index (`index_` map)
- `ContiguousStringBuffer.h` - ID to index mapping (`id_to_index_` map)
- `ContiguousStringBuffer.cpp` - Extension filtering sets
- `SearchController.cpp` - Result comparison maps
- `SearchWorker.cpp` - Path mapping during batch processing

**Note:** `StringPool::pool_` in `FileIndex.h` always uses `std::unordered_set` (not affected by flags) because it requires stable references for pointer safety.

## Testing

### A/B Testing
You can easily compare performance by building with different options:

```bash
# Build without FAST_LIBS (standard library)
nmake clean
nmake

# Test and measure memory/performance
# ... run tests ...

# Build with unordered_dense
nmake clean
nmake FAST_LIBS=1

# Test again and compare
# ... run tests ...

# Build with Boost
nmake clean
nmake FAST_LIBS_BOOST=1

# Test again and compare
# ... run tests ...
```

### Expected Improvements

**unordered_dense (FAST_LIBS):**
- **Memory**: 20-30 MB saved per million entries
- **Performance**: 10-30% faster lookups (especially for large maps)
- **Cache locality**: Better due to lower memory overhead

**boost::unordered_map (FAST_LIBS_BOOST):**
- **Performance**: ~2x faster lookups than `std::unordered_map`
- **Memory**: Slightly better than `std::unordered_map` (~12 bytes/entry vs ~20-30 bytes)
- **Stable references**: Maintains pointer stability (important for `StringPool`)

## Troubleshooting

### Error: `ankerl/unordered_dense.h`: No such file or directory
**Solution**: Initialize the git submodule:
```bash
git submodule update --init --recursive
```

### Error: Boost not found (CMake)
**Solution**: 
1. **Using vcpkg (Recommended):**
   ```bash
   # Install vcpkg and Boost
   git clone https://github.com/Microsoft/vcpkg.git
   cd vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg install boost:x64-windows
   
   # Configure CMake with vcpkg toolchain
   cmake -DCMAKE_TOOLCHAIN_FILE=[path_to_vcpkg]/scripts/buildsystems/vcpkg.cmake -DFAST_LIBS_BOOST=ON ..
   ```

2. **Manual installation:**
   - Download Boost 1.80+ from https://www.boost.org/
   - Extract to a location like `C:\boost_1_80_0`
   - Set `BOOST_ROOT` environment variable or pass `-DBOOST_ROOT=C:/boost_1_80_0` to CMake
   - Ensure Boost version is 1.80 or later (CMake will check automatically)

### Error: Boost not found (Makefile)
**Solution**: 
1. Set `BOOST_ROOT` environment variable to your Boost installation:
   ```bash
   set BOOST_ROOT=C:\boost_1_80_0
   ```
2. Or modify `Makefile` to add your Boost include path manually

### Compilation errors with unordered_dense
**Solution**: 
1. Ensure you're using a C++17 compatible compiler (project is already set to C++17)
2. Ensure submodules are up to date:
   ```bash
   git submodule update --init --recursive
   ```

### Want to disable FAST_LIBS temporarily?
**Solution**: Simply build without `FAST_LIBS` or `FAST_LIBS_BOOST` flags - the code will fall back to `std::unordered_map`

## Implementation Details

The feature flags are implemented via `HashMapAliases.h`, which provides:
- `hash_map_t<K, V>` - Alias for hash map (varies by flag)
- `hash_set_t<V>` - Alias for hash set (varies by flag)

All code uses these aliases, making it easy to switch implementations.

## Updating Dependencies

### Updating unordered_dense

```bash
# Update unordered_dense submodule to latest commit
cd external/unordered_dense
git pull origin main
cd ../..

# Commit the updated submodule reference
git add external/unordered_dense
git commit -m "Update unordered_dense submodule to latest version"
```

Or update all submodules at once:
```bash
git submodule update --remote --merge
git add external/unordered_dense
git commit -m "Update unordered_dense submodule"
```

### Updating Boost

Simply download and install a newer version of Boost from https://www.boost.org/ and update `BOOST_ROOT` if needed.

## References
- [unordered_dense GitHub](https://github.com/martinus/unordered_dense) - Used as submodule
- [Boost.Unordered Documentation](https://www.boost.org/doc/libs/1_80_0/doc/html/unordered.html)
- [Analysis Document](../docs/UNORDERED_MAP_SET_ANALYSIS.md)
