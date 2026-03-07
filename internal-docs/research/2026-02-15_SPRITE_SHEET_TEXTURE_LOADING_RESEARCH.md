# Sprite Sheet Texture Loading Research

**Date:** 2026-02-15  
**Context:** Loading `detective_sprite_sheet.png` (8×8 grid, 64 frames, PNG with alpha) into GPU textures for display via ImGui across Windows (DirectX 11), macOS (Metal), and Linux (OpenGL).

---

## Summary

Loading a sprite sheet into a texture is a **two-step process**:

1. **Image decoding** – Load PNG from disk into decompressed RGBA pixel data in RAM.
2. **Texture upload** – Upload RGBA data to a GPU texture using the platform-specific graphics API.

ImGui does **not** load images; it only displays them. You must create the texture yourself and pass an `ImTextureID` to `ImGui::Image()` or `ImGui::ImageButton()`.

---

## Image Loading Libraries

### Option 1: stb_image (Recommended)

| Aspect | Details |
|--------|---------|
| **License** | Public domain |
| **Format** | Header-only, single file (`stb_image.h`) |
| **Formats** | PNG, JPEG, BMP, TGA, PSD, HDR, GIF |
| **Dependencies** | C standard library only |
| **Integration** | Add header, define `STB_IMAGE_IMPLEMENTATION` in one `.cpp` file |

**Pros:**
- Header-only, trivial to add (no CMake subdirectory)
- Public domain, no licensing concerns
- Widely used; recommended in [ImGui Image Loading Wiki](https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples)
- Works with all backends (OpenGL, DirectX 11, Metal)
- Can load from file or memory (`stbi_load`, `stbi_load_from_memory`)
- Optional `STBI_ONLY_PNG` to reduce code size if only PNG is needed

**Cons:**
- Only decodes; texture upload is backend-specific (you implement it)

**Usage pattern:**
```cpp
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG  // Optional: reduce binary size
#include "stb_image.h"

int width, height, channels;
unsigned char* data = stbi_load("path.png", &width, &height, &channels, 4);  // 4 = force RGBA
// ... upload to GPU ...
stbi_image_free(data);
```

---

### Option 2: SOIL2 (Simple OpenGL Image Library)

| Aspect | Details |
|--------|---------|
| **License** | MIT-0 |
| **Format** | C library with CMake build |
| **Formats** | PNG, JPEG, BMP, TGA, DDS |
| **Backend** | **OpenGL only** – creates GL textures directly |

**Pros:**
- One call to load and create OpenGL texture
- Built on stb_image internally

**Cons:**
- **OpenGL only** – not suitable for DirectX 11 (Windows) or Metal (macOS)
- Would require separate code paths for each platform
- Adds an external dependency and build step

**Verdict:** Not recommended for this cross-platform project.

---

### Option 3: Backend-Specific Loaders

- **DirectX 9:** `D3DXCreateTextureFromFile` – deprecated, not available on modern Windows SDK.
- **DirectX 11/12, Metal, OpenGL:** No built-in image loaders; you must decode (e.g. with stb_image) and upload manually.

---

## Backend-Specific Texture Upload

### ImTextureID per Backend

| Platform | Backend | ImTextureID Type |
|----------|---------|------------------|
| Windows | DirectX 11 | `ID3D11ShaderResourceView*` |
| macOS | Metal | `MTLTexture*` (or equivalent) |
| Linux | OpenGL | `GLuint` |

### Windows (DirectX 11)

1. Load RGBA with `stbi_load` (or `stbi_load_from_memory`).
2. Create `ID3D11Texture2D` with `D3D11_TEXTURE2D_DESC`:
   - `Format = DXGI_FORMAT_R8G8B8A8_UNORM`
   - `BindFlags = D3D11_BIND_SHADER_RESOURCE`
   - `Usage = D3D11_USAGE_DEFAULT`
3. Fill `D3D11_SUBRESOURCE_DATA` with pixel data (`SysMemPitch = width * 4`).
4. Call `ID3D11Device::CreateTexture2D`.
5. Create `ID3D11ShaderResourceView` from the texture.
6. Pass the SRV pointer to `ImGui::Image((ImTextureID)srv, size)`.

Device access: `DirectXManager` has `GetDevice()` / `GetDeviceContext()` (currently private; may need exposure or a texture-loading helper).

### Linux (OpenGL)

1. Load RGBA with stb_image.
2. `glGenTextures`, `glBindTexture(GL_TEXTURE_2D, id)`.
3. `glTexImage2D` with `GL_RGBA`, `GL_UNSIGNED_BYTE`, pixel data.
4. Set `GL_TEXTURE_MIN_FILTER` / `GL_TEXTURE_MAG_FILTER` (e.g. `GL_LINEAR`).
5. Pass `(ImTextureID)(intptr_t)texture_id` to `ImGui::Image()`.

### macOS (Metal)

1. Load RGBA with stb_image.
2. Create `MTLTexture` via `MTLDevice::newTextureWithDescriptor:` with `MTLTextureDescriptor` (RGBA8Unorm, 2D).
3. Copy pixels with `replaceRegion:...` or a staging buffer.
4. Pass the `MTLTexture*` to `ImGui::Image()` (Metal backend uses `MTLTexture` as `ImTextureID`).

---

## Sprite Sheet Considerations

For `detective_sprite_sheet.png` (8×8 grid, 64 frames):

1. **Load the full sheet** as a single texture (recommended).
2. **Frame size:** `frame_w = total_width / 8`, `frame_h = total_height / 8`.
3. **UV coordinates:** For frame `(col, row)`:
   - `u0 = col / 8.0f`, `u1 = (col + 1) / 8.0f`
   - `v0 = row / 8.0f`, `v1 = (row + 1) / 8.0f`
4. **Display a single frame:** Use `ImGui::Image` with `ImVec2(uv0, uv0)` and `ImVec2(uv1, uv1)` for the UV rect (ImGui 1.90+ supports `ImGui::Image` with UV coordinates).

**Alpha channel:** The sprite sheet uses transparency. Use 4 channels (RGBA) when loading; stb_image with `stbi_load(..., 4)` ensures this.

---

## Recommended Approach

1. **Add stb_image** as a single header in `external/stb/` (or similar).
2. **Create a platform-agnostic interface** (e.g. `TextureLoader` or `SpriteSheet`) that:
   - Uses stb_image to decode PNG to RGBA.
   - Dispatches to platform-specific upload code.
3. **Implement backend-specific loaders** in platform directories:
   - `TextureLoader_win.cpp` – DirectX 11
   - `TextureLoader_linux.cpp` – OpenGL
   - `TextureLoader_mac.mm` – Metal
4. **Expose device/context** from `DirectXManager` (or equivalent) if needed for texture creation, or add a texture-loading helper that receives the device.

---

## References

- [ImGui Image Loading and Displaying Examples](https://github.com/ocornut/imgui/wiki/Image-Loading-and-Displaying-Examples)
- [stb_image.h](https://github.com/nothings/stb/blob/master/stb_image.h)
- [SOIL2](https://github.com/SpartanJ/SOIL2) (OpenGL-only, not recommended here)
- Project backends: `DirectXManager` (Windows), `MetalManager` (macOS), `OpenGLManager` (Linux)
