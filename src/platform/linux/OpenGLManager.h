#pragma once

/**
 * @file OpenGLManager.h
 * @brief OpenGL implementation of RendererInterface for Linux
 *
 * Provides OpenGL rendering backend for Linux platform.
 * Implements RendererInterface to provide unified rendering API.
 *
 * This class encapsulates all OpenGL-specific rendering code, similar to
 * DirectXManager (Windows) and MetalManager (macOS).
 */

#include "gui/RendererInterface.h"

struct GLFWwindow;

/**
 * @class OpenGLManager
 * @brief OpenGL implementation of RendererInterface for Linux
 *
 * Manages OpenGL context, framebuffer, and rendering operations.
 * Handles frame setup (clear, bind framebuffer) and presentation (swap buffers).
 */
class OpenGLManager : public RendererInterface {
public:
  OpenGLManager();
  ~OpenGLManager() override;

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

  // Prevent copying
  OpenGLManager(const OpenGLManager&) = delete;
  OpenGLManager& operator=(const OpenGLManager&) = delete;
  OpenGLManager(OpenGLManager&&) = delete;
  OpenGLManager& operator=(OpenGLManager&&) = delete;

private:
  GLFWwindow* window_{nullptr};  // NOLINT(readability-identifier-naming) - project convention: snake_case_
  bool initialized_{false};  // NOLINT(readability-identifier-naming) - project convention: snake_case_
};

