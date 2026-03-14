#pragma once

/**
 * @file RendererInterface.h
 * @brief Abstract interface for platform-specific rendering backends
 *
 * This interface provides a unified abstraction for rendering across platforms:
 * - Windows: DirectX 11 (via DirectXManager)
 * - macOS: Metal (via MetalManager)
 * - Linux: OpenGL (via OpenGLManager)
 *
 * The interface hides platform-specific implementation details from the
 * Application class, allowing it to work with any rendering backend through
 * a common API.
 *
 * DESIGN PRINCIPLES:
 * 1. Platform Agnostic: No platform-specific types in the interface
 * 2. Lifecycle Management: Clear initialization and cleanup methods
 * 3. Frame-Based Rendering: BeginFrame/EndFrame pattern for frame setup/teardown
 * 4. ImGui Integration: Built-in ImGui backend initialization
 *
 * THREADING MODEL:
 * - All methods must be called from the main UI thread
 * - Renderers are not thread-safe (matches ImGui's threading model)
 *
 * @see DirectXManager.h for Windows implementation
 * @see MetalManager.h for macOS implementation
 * @see OpenGLManager.h for Linux implementation
 */

struct GLFWwindow;

/**
 * @class RendererInterface
 * @brief Abstract base class for platform-specific rendering backends
 *
 * Provides a unified interface for DirectX (Windows), Metal (macOS), and
 * OpenGL (Linux). Hides platform-specific implementation details from
 * Application class.
 */
class RendererInterface {
public:
  virtual ~RendererInterface() = default;

protected:
  // Protected default constructor (interface class)
  RendererInterface() = default;

public:
  // Non-copyable, non-movable (interface class)
  RendererInterface(const RendererInterface&) = delete;
  RendererInterface& operator=(const RendererInterface&) = delete;
  RendererInterface(RendererInterface&&) = delete;
  RendererInterface& operator=(RendererInterface&&) = delete;

  /**
   * @brief Initialize renderer with GLFW window
   *
   * Sets up the rendering backend (DirectX device, Metal device, OpenGL context)
   * for the given GLFW window. Must be called before any other rendering operations.
   *
   * @param window GLFW window handle (platform-specific native handle extracted internally)
   * @return true on success, false on failure
   */
  virtual bool Initialize(GLFWwindow* window) = 0;

  /**
   * @brief Cleanup all rendering resources
   *
   * Releases all platform-specific resources (devices, contexts, swap chains).
   * Safe to call multiple times. Should be called before window destruction.
   */
  virtual void Cleanup() = 0;

  /**
   * @brief Begin rendering frame
   *
   * Sets up render target, clears framebuffer, and prepares for rendering.
   * Must be called once per frame before ImGui::NewFrame().
   *
   * For DirectX: Sets render target view, clears back buffer
   * For Metal: Gets drawable, creates command buffer and render encoder
   * For OpenGL: Binds framebuffer, clears color buffer
   */
  virtual void BeginFrame() = 0;

  /**
   * @brief End rendering frame
   *
   * Presents the rendered frame to the screen (swap buffers, present drawable).
   * Must be called once per frame after ImGui::Render() and RenderImGui().
   *
   * For DirectX: Presents swap chain
   * For Metal: Commits command buffer, presents drawable
   * For OpenGL: Swaps buffers
   */
  virtual void EndFrame() = 0;

  /**
   * @brief Handle window resize event
   *
   * Recreates render targets and swap chain buffers for the new window size.
   * Should be called from window resize callback.
   *
   * @param width New window width in pixels
   * @param height New window height in pixels
   */
  virtual void HandleResize(int width, int height) = 0;

  /**
   * @brief Initialize ImGui renderer backend
   *
   * Sets up ImGui's platform-specific rendering backend (DirectX11, Metal, OpenGL3).
   * Must be called after Initialize() and before BeginFrame().
   *
   * @return true on success, false on failure
   */
  virtual bool InitializeImGui() = 0;

  /**
   * @brief Shutdown ImGui renderer backend
   *
   * Cleans up ImGui's platform-specific rendering backend.
   * Should be called before Cleanup().
   */
  virtual void ShutdownImGui() = 0;

  /**
   * @brief Render ImGui draw data
   *
   * Renders ImGui's draw data to the render target. Must be called after
   * ImGui::Render() and before EndFrame().
   */
  virtual void RenderImGui() = 0;

  /**
   * @brief Check if renderer is initialized
   *
   * @return true if Initialize() was successful, false otherwise
   */
  [[nodiscard]] virtual bool IsInitialized() const = 0;
};


