#pragma once

#include <d3d11.h>
#include <windows.h>  // NOSONAR(cpp:S3806) - lowercase for portability on case-sensitive systems

#include "gui/RendererInterface.h"

struct GLFWwindow;

/**
 * DirectXManager - DirectX 11 Resource Management and Rendering
 *
 * This class provides a lightweight wrapper around DirectX 11 for managing
 * device creation, swap chain, render targets, and rendering operations.
 * It is designed to work with ImGui for UI rendering in a Windows application.
 *
 * The class abstracts away DirectX implementation details, providing a clean
 * interface that doesn't expose DirectX-specific types to callers.
 *
 * THREADING MODEL:
 * - This class is NOT thread-safe and assumes all operations occur on the
 *   main UI thread. DirectX device contexts are not thread-safe, and this
 *   design follows the standard pattern for single-threaded UI rendering.
 * - All methods (Initialize, Present, RenderImGui, HandleResize) must be
 *   called from the main thread that owns the window handle.
 * - Window resize events (WM_SIZE) are handled via WndProc callback, which
 *   runs on the main thread, ensuring thread safety.
 *
 * DESIGN PRINCIPLES:
 * 1. Abstraction: The class hides DirectX implementation details from callers.
 *    Methods like InitializeImGui() and ShutdownImGui() provide renderer-agnostic
 *    interfaces, making it easier to switch renderers in the future if needed.
 *
 * 2. Single-threaded assumption: No mutexes or synchronization primitives,
 *    reducing overhead and complexity. This is appropriate for UI rendering
 *    but would need modification if multi-threaded rendering were required.
 *
 * 3. Fixed configuration: Hardcoded settings (60Hz refresh, double buffering,
 *    DXGI_FORMAT_R8G8B8A8_UNORM) simplify initialization but reduce flexibility.
 *    Suitable for standard desktop applications but may need customization
 *    for specialized use cases.
 *
 * 4. Manual resource management: Uses raw COM pointers with manual Release()
 *    calls rather than smart pointers. This follows DirectX conventions but
 *    requires careful cleanup in destructor and error paths.
 *
 * 5. Vsync enabled: Present() uses Present(1, 0) which enables vertical sync,
 *    limiting frame rate to display refresh but preventing tearing. This is
 *    appropriate for UI applications but may need adjustment for high-FPS
 *    scenarios.
 *
 * FUTURE IMPROVEMENTS:
 * - Configuration struct: Allow callers to specify swap chain parameters
 *   (buffer count, format, refresh rate) rather than hardcoding values.
 * - Error recovery: Add retry logic for device creation failures, especially
 *   for cases where the device is lost (e.g., display mode changes, driver
 *   updates).
 * - Resource validation: Add more comprehensive null checks and validation
 *   before operations to provide better error messages.
 * - Thread safety (if needed): If multi-threaded rendering becomes necessary,
 *   consider using ID3D11DeviceContext1::CreateDeferredContext() for
 *   background thread command list generation, with synchronization on the
 *   main thread for execution.
 * - Smart pointer wrapper: Consider wrapping COM pointers in custom deleters
 *   or using Microsoft::WRL::ComPtr for automatic reference counting, though
 *   this may conflict with existing code patterns.
 * - Multi-viewport support: Enhance HandleResize to support multiple render
 *   targets if multi-monitor or advanced viewport scenarios are needed.
 * - Performance profiling: Add optional frame timing and GPU profiling hooks
 *   for performance analysis.
 */
/**
 * @class DirectXManager
 * @brief DirectX 11 implementation of RendererInterface for Windows
 *
 * Provides DirectX 11 rendering backend for Windows platform.
 * Implements RendererInterface to provide unified rendering API.
 */
class DirectXManager : public RendererInterface {
public:
  DirectXManager();
  ~DirectXManager() override;

  // RendererInterface implementation
  [[nodiscard]] bool Initialize(GLFWwindow* window) override;
  void Cleanup() override;
  void BeginFrame() override;
  void EndFrame() override;
  void HandleResize(int width, int height) override;
  [[nodiscard]] bool InitializeImGui() override;
  void ShutdownImGui() override;
  void RenderImGui() override;
  [[nodiscard]] bool IsInitialized() const override;

  // Prevent copying and moving (manages DirectX resources)
  DirectXManager(const DirectXManager &) = delete;
  DirectXManager &operator=(const DirectXManager &) = delete;
  DirectXManager(DirectXManager &&) = delete;
  DirectXManager &operator=(DirectXManager &&) = delete;

private:
  // Present the rendered frame (called by EndFrame)
  void Present();
  // Get device and device context (for internal use only)
  [[nodiscard]] ID3D11Device *GetDevice() const { return pd3d_device_; }
  [[nodiscard]] ID3D11DeviceContext *GetDeviceContext() const {
    return pd3d_device_context_;
  }

  // Helper methods
  bool CreateDeviceD3D(HWND h_wnd);
  void CleanupDeviceD3D();
  void CreateRenderTarget();
  void CleanupRenderTarget();

  ID3D11Device *pd3d_device_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention; p prefix for pointer
  ID3D11DeviceContext *pd3d_device_context_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention
  IDXGISwapChain *p_swap_chain_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention
  ID3D11RenderTargetView *main_render_target_view_ = nullptr;  // NOLINT(readability-identifier-naming) - project convention
};
