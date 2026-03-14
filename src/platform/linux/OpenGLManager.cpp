/**
 * @file OpenGLManager.cpp
 * @brief Implementation of OpenGL rendering backend for Linux
 *
 * Provides OpenGL 3.3+ rendering backend for Linux platform.
 * Implements RendererInterface for unified rendering API.
 */

#include "OpenGLManager.h"
#include "utils/Logger.h"

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

// GLFW includes
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// OpenGL includes
// Note: On Linux, we need OpenGL headers for glGetString, glGetIntegerv, glViewport, etc.
// ImGui's OpenGL3 backend will handle function loading via GLAD or similar
#include <GL/gl.h>

// OpenGL configuration constants
namespace opengl_constants {
  // Clear color (ImGui default background: light gray-blue)
  // NOLINTNEXTLINE(cppcoreguidelines-avoid-c-arrays,hicpp-avoid-c-arrays,modernize-avoid-c-arrays) - constexpr aggregate init for GL clear color
  constexpr float kClearColor[4] = {0.45F, 0.55F, 0.60F, 1.00F};  // NOSONAR(cpp:S5945) - C-style array required for constexpr aggregate initialization

  // OpenGL version required for ImGui OpenGL3 backend
  constexpr int kOpenGLVersionMajor = 3;
  constexpr int kOpenGLVersionMinor = 3;

  // GLSL version string for ImGui
  constexpr const char* kGLSLVersion = "#version 330";
}

OpenGLManager::OpenGLManager() = default;

OpenGLManager::~OpenGLManager() {
  OpenGLManager::Cleanup();
}

bool OpenGLManager::Initialize(GLFWwindow* window) {
  if (initialized_) {
    LOG_ERROR("OpenGLManager already initialized");
    return false;
  }

  if (window == nullptr) {
    LOG_ERROR("Invalid GLFW window provided to Initialize");
    return false;
  }

  window_ = window;

  // Make the OpenGL context current
  glfwMakeContextCurrent(window_);

  // Enable VSync (swap interval 1 = wait for vertical blank)
  // This matches Windows DirectX behavior (kPresentSyncInterval = 1)
  // and macOS Metal behavior (displaySyncEnabled = YES)
  glfwSwapInterval(1);

  // Check OpenGL version (after context is current)
  if (const unsigned char* gl_version = glGetString(GL_VERSION); gl_version) {
    LOG_INFO("OpenGL version: " + std::string(reinterpret_cast<const char*>(gl_version)));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast) - OpenGL returns GLubyte*; safe to interpret as char*
  }

  // Verify we have at least OpenGL 3.3 (required for ImGui OpenGL3 backend)
  int major = 0;
  int minor = 0;
  glGetIntegerv(GL_MAJOR_VERSION, &major);
  glGetIntegerv(GL_MINOR_VERSION, &minor);

  if (major < opengl_constants::kOpenGLVersionMajor ||
      (major == opengl_constants::kOpenGLVersionMajor && minor < opengl_constants::kOpenGLVersionMinor)) {
    LOG_ERROR("OpenGL " +
              std::to_string(opengl_constants::kOpenGLVersionMajor) + "." +
              std::to_string(opengl_constants::kOpenGLVersionMinor) +
              " or higher required. Found " + std::to_string(major) + "." + std::to_string(minor));
    window_ = nullptr;
    return false;
  }

  initialized_ = true;
  LOG_INFO("OpenGL initialized successfully");
  return true;
}

void OpenGLManager::Cleanup() {
  if (!initialized_) {
    return;
  }

  // Shutdown ImGui if it was initialized
  if (ImGui::GetCurrentContext() != nullptr) {
    OpenGLManager::ShutdownImGui();
  }

  // Make context current before cleanup (if window still exists)
  if (window_ != nullptr) {
    glfwMakeContextCurrent(window_);
  }

  initialized_ = false;
  window_ = nullptr;
}

void OpenGLManager::BeginFrame() {
  if (!initialized_ || window_ == nullptr) {
    return;
  }

  // Make context current (in case it was changed)
  glfwMakeContextCurrent(window_);

  // Get framebuffer size (may differ from window size on high-DPI displays)
  int framebuffer_width = 0;
  int framebuffer_height = 0;
  glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);

  // Set viewport
  glViewport(0, 0, framebuffer_width, framebuffer_height);

  // Clear color buffer with ImGui default background color
  glClearColor(opengl_constants::kClearColor[0],
               opengl_constants::kClearColor[1],
               opengl_constants::kClearColor[2],
               opengl_constants::kClearColor[3]);
  glClear(GL_COLOR_BUFFER_BIT);

  // Call ImGui OpenGL3 NewFrame (platform-specific ImGui frame setup)
  ImGui_ImplOpenGL3_NewFrame();
}

void OpenGLManager::EndFrame() {
  if (!initialized_ || window_ == nullptr) {
    return;
  }

  // Swap buffers (present frame)
  glfwSwapBuffers(window_);
}

void OpenGLManager::HandleResize(int width, int height) {
  if (!initialized_ || window_ == nullptr) {
    return;
  }

  if (width == 0 || height == 0) {
    LOG_WARNING("HandleResize called with zero dimensions");
    return;
  }

  // Make context current
  glfwMakeContextCurrent(window_);

  // Get framebuffer size (may differ from window size on high-DPI displays)
  int framebuffer_width = 0;
  int framebuffer_height = 0;
  glfwGetFramebufferSize(window_, &framebuffer_width, &framebuffer_height);

  // Update viewport
  glViewport(0, 0, framebuffer_width, framebuffer_height);

  // ImGui will handle the resize automatically via ImGui_ImplGlfw_WindowSizeCallback
  // which is registered in AppBootstrap_linux
}

bool OpenGLManager::InitializeImGui() {
  if (!initialized_ || window_ == nullptr) {
    LOG_ERROR("OpenGLManager not initialized, cannot initialize ImGui");
    return false;
  }

  // Make context current
  glfwMakeContextCurrent(window_);

  // Setup ImGui OpenGL3 backend
  if (!ImGui_ImplOpenGL3_Init(opengl_constants::kGLSLVersion)) {
    LOG_ERROR("Failed to initialize ImGui OpenGL3 backend");
    return false;
  }

  LOG_INFO("ImGui OpenGL3 backend initialized successfully");
  return true;
}

void OpenGLManager::ShutdownImGui() {
  if (!initialized_) {
    return;
  }

  // Make context current before shutdown
  if (window_ != nullptr) {
    glfwMakeContextCurrent(window_);
  }

  // Shutdown ImGui OpenGL3 backend
  ImGui_ImplOpenGL3_Shutdown();
}

void OpenGLManager::RenderImGui() {
  if (!initialized_ || window_ == nullptr) {
    return;
  }

  // Make context current
  glfwMakeContextCurrent(window_);

  // Render ImGui draw data
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool OpenGLManager::IsInitialized() const {
  return initialized_;
}

