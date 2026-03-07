# 2026-03-01 Linux segfault on app close – investigation and fix

## Summary

Closing the app on Linux could trigger a **segmentation fault** due to **double shutdown** of the ImGui OpenGL3 backend.

## Root cause

In **AppBootstrap_linux.cpp**, `Cleanup()` and `CleanupOnException()` were calling **ImGui_ImplOpenGL3_Shutdown()** explicitly, and then calling **AppBootstrapCommon::CleanupImGuiAndGlfw(result)**. The common cleanup template calls **result.renderer->ShutdownImGui()**, which for **OpenGLManager** again calls **ImGui_ImplOpenGL3_Shutdown()**.

So **ImGui_ImplOpenGL3_Shutdown()** was invoked **twice** on exit. The second call runs after the OpenGL3 backend has already been torn down, leading to use-after-free or double-free in ImGui’s OpenGL3 code and a segfault.

## Platform comparison

- **Windows:** `Cleanup()` only calls `CleanupImGuiAndGlfw(result)`; the DirectX ImGui backend is shut down once via `result.renderer->ShutdownImGui()`.
- **macOS:** Same: only `CleanupImGuiAndGlfw(result)`; Metal ImGui backend shut down once via the renderer.
- **Linux (before fix):** `Cleanup()` called `ImGui_ImplOpenGL3_Shutdown()` and then `CleanupImGuiAndGlfw(result)` → `renderer->ShutdownImGui()` → `ImGui_ImplOpenGL3_Shutdown()` again → **double shutdown**.

## Fix applied

1. **AppBootstrapLinux::Cleanup()**  
   Removed the explicit `ImGui_ImplOpenGL3_Shutdown()` call. Only `CleanupImGuiAndGlfw(result)` is called, which performs OpenGL3 shutdown once via `result.renderer->ShutdownImGui()`.

2. **CleanupOnException()**  
   Removed the explicit `ImGui_ImplOpenGL3_Shutdown()` call so exception paths also rely on `CleanupImGuiAndGlfw(result)` for a single shutdown.

3. **Include**  
   Removed the unused `#include "imgui_impl_opengl3.h"` from `AppBootstrap_linux.cpp`; OpenGL3 is only used inside `OpenGLManager`.

## References

- `src/platform/linux/AppBootstrap_linux.cpp` – Linux cleanup and exception cleanup
- `src/core/AppBootstrapCommon.h` – `CleanupImGuiAndGlfw` template (calls `renderer->ShutdownImGui()` then `renderer->Cleanup()`)
- `src/platform/linux/OpenGLManager.cpp` – `ShutdownImGui()` calls `ImGui_ImplOpenGL3_Shutdown()`
