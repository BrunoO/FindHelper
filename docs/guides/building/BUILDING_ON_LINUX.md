# Building on Linux

This document provides instructions for setting up the build environment and compiling the application on Linux (Ubuntu/Debian and similar distributions).

## Prerequisites

### Required Dependencies

Install the required packages using your distribution's package manager:

**Ubuntu/Debian:**
```bash
sudo apt-get update
sudo apt-get install -y \
    build-essential \
    cmake \
    pkg-config \
    libglfw3-dev \
    libgl1-mesa-dev \
    libglu1-mesa-dev \
    libcurl4-openssl-dev \
    libfontconfig1-dev \
    libx11-dev \
    libxss-dev \
    wayland-protocols \
    libwayland-dev \
    libxkbcommon-dev \
    libxrandr-dev \
    libxinerama-dev \
    libxcursor-dev \
    libxi-dev \
    libwayland-bin
```

- **Note:** `libwayland-bin` is **required** for the full application build (it provides `wayland-scanner` needed by GLFW 3.4). `wayland-utils` is optional and lives in the **universe** repo. If you need it: `sudo add-apt-repository universe && sudo apt-get update`, then `sudo apt-get install wayland-utils`.

**Optional – Boost** (for `-DFAST_LIBS_BOOST=ON`): Install `libboost-dev`. Boost 1.80+ is required; Ubuntu 24.04 provides 1.83. On **Ubuntu 22.04** (default is 1.74), use either the PPA or build from source below.

  **Option A – PPA (easiest on Ubuntu 22.04):**
  ```bash
  sudo add-apt-repository ppa:mhier/libboost-latest
  sudo apt-get update
  sudo apt-get install libboost1.83-dev
  ```

  **Option B – Build Boost from source** (any distro, or when PPA is not desired):

  1. Download a release (1.80+) from [boost.org](https://www.boost.org/users/download/) (e.g. `boost_1_83_0.tar.bz2`).
  2. Install build dependencies (Ubuntu/Debian):
     ```bash
     sudo apt-get install -y build-essential python3 libicu-dev libbz2-dev zlib1g-dev
     ```
  3. Extract, bootstrap, and build (install to `/usr/local` so it does not override system Boost):
     ```bash
     tar xf boost_1_83_0.tar.bz2 && cd boost_1_83_0
     ./bootstrap.sh --prefix=/usr/local
     ./b2 -j$(nproc)
     sudo ./b2 install
     ```
  4. When building this project, point CMake to the custom Boost:
     ```bash
     cmake -DFAST_LIBS_BOOST=ON -DCMAKE_PREFIX_PATH=/usr/local -S . -B build
     cmake --build build --config Release
     ```

**Fedora/RHEL:**
```bash
sudo dnf install -y \
    gcc-c++ \
    cmake \
    glfw-devel \
    mesa-libGL-devel \
    libcurl-devel \
    fontconfig-devel \
    libX11-devel \
    libXScrnSaver-devel \
    wayland-protocols-devel \
    wayland-devel \
    libxkbcommon-devel \
    libXrandr-devel \
    libXinerama-devel \
    libXcursor-devel \
    libXi-devel
```

**Arch Linux:**
```bash
sudo pacman -S \
    base-devel \
    cmake \
    glfw \
    mesa \
    curl \
    fontconfig \
    libx11 \
    libxss \
    wayland-protocols \
    wayland \
    libxkbcommon \
    libxrandr \
    libxinerama \
    libxcursor \
    libxi
```

### Dependency Notes

- **GLFW 3.3+**: Required for windowing and input handling. **If not available**, CMake will automatically download and build GLFW 3.4 from source (see below).
- **OpenGL**: Required for rendering (usually provided by Mesa)
- **Fontconfig**: Required for font discovery (`FontUtils_linux.cpp`)
- **X11**: Required by GLFW on Linux (usually auto-linked, but explicitly linked for compatibility)
- **libxss** (`libxss-dev`): X11 Screen Saver extension for system idle detection
- **libcurl**: Required for network operations (if applicable)

### Automatic GLFW Fallback

If your system doesn't have GLFW 3.3+ available (e.g., older distributions), CMake will automatically:
1. Download GLFW 3.4 source from GitHub
2. Build it as part of the project

This requires network access during the first `cmake` configure step. The download is cached in your build directory for subsequent builds.

**To force using the FetchContent fallback** (even if system GLFW is installed):
```bash
# Remove system GLFW detection by clearing the cache
rm -rf build && mkdir build && cd build
cmake -DCMAKE_DISABLE_FIND_PACKAGE_glfw3=TRUE ..
```

## Initial Setup

### 1. Initialize Git Submodules

This project uses Git submodules for dependencies (`imgui`, `doctest`, `nlohmann_json`, `freetype`). You must initialize them:

```bash
git submodule update --init --recursive
```

This will download the contents of the `external/` submodules. If you skip this step, the build will fail.

## Building the Project

### 1. Create Build Directory

```bash
mkdir build
cd build
```

### 2. Configure with CMake

```bash
cmake -S .. -B .
```

Or from the project root:

```bash
cmake -S . -B build
```

### 3. Build the Application

```bash
cmake --build . --config Release
```

Or from the project root:

```bash
cmake --build build --config Release
```

### 4. Run the Application

After building, the executable will be in the build directory:

```bash
./FindHelper
```

Or with command-line arguments:

```bash
./FindHelper --crawl-folder /path/to/directory
```

## Troubleshooting

### GLFW Not Found (Automatic Fallback)

**Good news:** If system GLFW 3.3+ is not found, CMake will automatically download and build GLFW 3.4 from source via FetchContent. You should see a message like:

```
-- System GLFW 3.3+ not found - downloading and building GLFW 3.4 via FetchContent
```

**If automatic download fails** (e.g., no network access):

1. Install system GLFW: `sudo apt-get install libglfw3-dev` (or equivalent)
2. Check that GLFW 3.3+ is available: `pkg-config --modversion glfw3`
3. If using a custom GLFW installation, set `CMAKE_PREFIX_PATH`

**If you want to use system GLFW but CMake downloads it anyway:**

1. Ensure `libglfw3-dev` 3.3+ is installed (older versions may be rejected)
2. Clear CMake cache and reconfigure: `rm -rf build && cmake -S . -B build`

### Unable to locate package wayland-utils (Ubuntu/Debian)

`wayland-utils` is in the **universe** repository. Either:

1. **Enable universe** and install it:
   ```bash
   sudo add-apt-repository universe
   sudo apt-get update
   sudo apt-get install wayland-utils
   ```
2. **Skip it** – it is not required to build this project. The list above uses `libwayland-bin`, which provides `wayland-scanner` for GLFW.

### Could not find XSCRNSAVER_LIB (CMake)

The build needs the X11 Screen Saver extension (libXss) for system idle detection. Install the development package:

- **Ubuntu/Debian:** `sudo apt-get install libxss-dev`
- **Fedora/RHEL:** `sudo dnf install libXScrnSaver-devel`

Then reconfigure: `cmake -S . -B build` (or your build directory).

### Illegal instruction (crash on startup)

If the program exits immediately with **"Illegal instruction"** (or **SIGILL**), the binary was built with CPU instructions (e.g. AVX2) that your run-time CPU does not support. This can happen if:

- You built on one machine (or VM) and run on another with an older CPU.
- The build used `-march=native` or global `-mavx2` for the whole executable.

The project’s default Linux build uses **baseline x86_64** for the main executable and enables AVX2 **only** in the string-search module (with runtime detection). If you still see this crash:

1. **Clean rebuild** so the default (no global AVX2) is applied:
   ```bash
   rm -rf build && mkdir build && cd build
   cmake -S .. -B .
   cmake --build . --config Release
   ```
2. **Check CPU support:** `grep avx2 /proc/cpuinfo` (empty = no AVX2). The app will run without AVX2 using the scalar path.
3. If you intentionally want a build tuned for **this machine only** (faster, but not portable), you can add `-DCMAKE_CXX_FLAGS="-march=native"` when configuring; only do this if the binary will run on the same (or compatible) CPU.

### AVX2-RT:off but Boost (or system) says AVX2 is available

The app reports **AVX2-CT:on** (AVX2 code compiled) and **AVX2-RT:on/off** (runtime CPU support). **AVX2-RT** is from CPUID at **runtime** (same process as the running app). It requires: CPUID leaf 7 EBX bit 5 (AVX2), leaf 1 ECX bit 28 (AVX), and leaf 1 ECX bit 27 (OSXSAVE – OS has enabled XSAVE for AVX state).

- **Boost** “AVX2 detected” is usually from **build time** (when you ran `./b2`), so it reflects the **build** machine’s CPU (or compiler support), not necessarily the **run** environment.
- If you see **AVX2-RT:off** on the same machine where Boost reported AVX2, check:
  1. **Same environment?** (e.g. app in Docker/VM vs Boost built on host → CPUID can differ.)
  2. **Kernel/OS:** `grep -E 'avx2|osxsave' /proc/cpuinfo` – if `avx2` is missing, the kernel or hypervisor is not exposing AVX2 to this environment.
  3. **Debug build:** the log prints raw CPUID (leaf7 EBX, leaf1 ECX and which of avx2/avx/osxsave are set) when AVX2-RT is off; use that to confirm what the running process sees.

### X11 Linking Errors

If you encounter X11-related linker errors:

1. Ensure `libx11-dev` is installed
2. The CMake configuration should automatically find and link X11
3. If issues persist, verify X11 is in your library path: `ldconfig -p | grep X11`

### Fontconfig Issues

If font loading fails:

1. Ensure `libfontconfig1-dev` is installed
2. Verify `fc-list` command works: `fc-list : family | head -5`
3. Check font directories are accessible: `/usr/share/fonts/`, `~/.fonts/`

### OpenGL Context Creation Fails

If the application fails to create an OpenGL context:

1. Ensure you have a display server running (X11 or Wayland)
2. Check OpenGL support: `glxinfo | grep "OpenGL version"` (requires `mesa-utils`)
3. For headless systems, consider using virtual framebuffer: `Xvfb :99`

### ImGui/GLFW Version Mismatch

If you encounter linker errors related to `glfwGetPlatform()`:

1. This indicates a version mismatch between ImGui headers and system GLFW
2. **Solution**: CMake should automatically download GLFW 3.4 if system GLFW is too old
3. If the issue persists, force the FetchContent fallback:
   ```bash
   rm -rf build && mkdir build && cd build
   cmake -DCMAKE_DISABLE_FIND_PACKAGE_glfw3=TRUE ..
   cmake --build . --config Release
   ```
4. **Do not** patch ImGui submodule (address root cause instead)

## Testing

### Run Tests

To verify that the build is working correctly:

```bash
cd build
cmake --build . --target run_tests
```


## Platform-Specific Notes

### Wayland Support

The application uses GLFW, which supports both X11 and Wayland. On Wayland systems:

- GLFW will automatically use Wayland if available
- X11 fallback is available if Wayland is not present
- No special configuration required

### Font Rendering

Linux font rendering uses Fontconfig for font discovery. Common fonts are automatically mapped:

- **Consolas** → DejaVu Sans Mono / Liberation Mono
- **Segoe UI** → Ubuntu / DejaVu Sans
- **Arial** → Arial / Liberation Sans

See `FontUtils_linux.cpp` for complete font mapping.

## Additional Resources

- **Main README**: See `../../../README.md` for general project information
- **Build System Details**: See `../../platform/linux/LINUX_BUILD_SYSTEM_IMPLEMENTATION.md`
- **Linux Preparation**: See `../../platform/linux/LINUX_PREPARATION_REFACTORINGS.md`

## Getting Help

If you encounter issues not covered here:

1. Check the main project README
2. Review build system documentation
3. Check GitHub issues for similar problems
4. Verify all dependencies are correctly installed

