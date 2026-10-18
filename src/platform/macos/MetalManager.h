#pragma once

/**
 * @file MetalManager.h
 * @brief Metal implementation of RendererInterface for macOS
 *
 * Provides Metal rendering backend for macOS platform.
 * Implements RendererInterface to provide unified rendering API.
 *
 * This class encapsulates all Metal-specific rendering code that was previously
 * inline in Application::ProcessFrame() and Application::RenderFrame().
 */

#include "gui/RendererInterface.h"

struct GLFWwindow;

#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>

/**
 * @class MetalManager
 * @brief Metal implementation of RendererInterface for macOS
 *
 * Manages Metal device, command queue, and rendering operations.
 * Handles frame setup (drawable, command buffer, render encoder) and presentation.
 */
class MetalManager : public RendererInterface {
public:
  MetalManager();
  ~MetalManager() override;

  // RendererInterface implementation
  bool Initialize(GLFWwindow* window) override;
  void Cleanup() override;
  void BeginFrame() override;
  void EndFrame() override;
  void HandleResize(int width, int height) override;
  bool InitializeImGui() override;
  void ShutdownImGui() override;
  void RenderImGui() override;
  [[nodiscard]] bool IsInitialized() const override;

  // Accessors for frame state (needed for Application to check if frame is valid)
  [[nodiscard]] bool HasValidFrame() const {
    return current_drawable_ != nil &&
           current_command_buffer_ != nil &&
           current_render_encoder_ != nil;
  }

  // Non-copyable, non-movable (public so copy attempts give clear error)
  MetalManager(const MetalManager&) = delete;
  MetalManager& operator=(const MetalManager&) = delete;
  MetalManager(MetalManager&&) = delete;
  MetalManager& operator=(MetalManager&&) = delete;

private:
  // Metal resources
  id<MTLDevice> device_;  // NOLINT(readability-identifier-naming) - project uses snake_case_ for members
  id<MTLCommandQueue> command_queue_;  // NOLINT(readability-identifier-naming)
  CAMetalLayer* layer_;  // NOLINT(readability-identifier-naming)

  // Frame state (set in BeginFrame, used in RenderImGui/EndFrame)
  id<CAMetalDrawable> current_drawable_;  // NOLINT(readability-identifier-naming)
  id<MTLCommandBuffer> current_command_buffer_;  // NOLINT(readability-identifier-naming)
  id<MTLRenderCommandEncoder> current_render_encoder_;  // NOLINT(readability-identifier-naming)

  // Track shutdown state to prevent double shutdown
  bool imgui_shutdown_;  // NOLINT(readability-identifier-naming)
};

#endif  // __APPLE__


