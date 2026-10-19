/**
 * @file MetalManager.mm
 * @brief Implementation of Metal rendering backend for macOS
 *
 * Extracts Metal rendering code from Application class into a dedicated manager,
 * implementing RendererInterface for unified rendering API.
 */

#include "MetalManager.h"
#include "utils/Logger.h"

#ifdef __APPLE__

#include "imgui.h"
#include "imgui_impl_metal.h"

// GLFW includes for native Cocoa access
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#import <AppKit/AppKit.h>

MetalManager::MetalManager()
    : device_(nil)
    , command_queue_(nil)
    , layer_(nil)
    , current_drawable_(nil)
    , current_command_buffer_(nil)
    , current_render_encoder_(nil)
    , imgui_shutdown_(false)
{
}

MetalManager::~MetalManager() {
  Cleanup();
}

bool MetalManager::Initialize(GLFWwindow* window) {
  if (device_ != nil) {
    LOG_ERROR("MetalManager already initialized");
    return false;
  }

  if (window == nullptr) {
    LOG_ERROR("Invalid GLFW window provided to Initialize");
    return false;
  }

  // Create Metal device
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  if (device == nil) {
    LOG_ERROR("Failed to create Metal device");
    return false;
  }
  device_ = device;

  // Create command queue
  id<MTLCommandQueue> commandQueue = [device newCommandQueue];
  if (commandQueue == nil) {
    LOG_ERROR("Failed to create Metal command queue");
    device_ = nil;
    return false;
  }
  command_queue_ = commandQueue;

  // Setup Metal Layer on the window
  NSWindow *nswin = glfwGetCocoaWindow(window);
  if (nswin == nil) {
    LOG_ERROR("glfwGetCocoaWindow returned nil NSWindow");
    device_ = nil;
    command_queue_ = nil;
    return false;
  }
  
  CAMetalLayer *layer = [CAMetalLayer layer];
  layer.device = device;
  layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
  // Enable display sync (VSync) to limit frame rate to monitor refresh rate.
  // Without this, the render loop runs as fast as possible, burning CPU cycles.
  // This matches the Windows DirectX behavior (kPresentSyncInterval = 1).
  layer.displaySyncEnabled = YES;
  nswin.contentView.layer = layer;
  nswin.contentView.wantsLayer = YES;
  layer_ = layer;

  LOG_INFO("Metal initialized successfully");
  return true;
}

void MetalManager::Cleanup() {
  // Release frame state
  current_drawable_ = nil;
  current_command_buffer_ = nil;
  current_render_encoder_ = nil;

  // Metal resources are managed by ARC, but we clear references
  layer_ = nil;
  command_queue_ = nil;
  device_ = nil;
}

void MetalManager::BeginFrame() {
  if (layer_ == nil || command_queue_ == nil) {
    return;
  }

  // Update layer drawable size from current window size
  // This ensures the layer size matches the window size (handles both resize callbacks and initial setup)
  // Note: We can't easily get window from layer, so we rely on HandleResize being called
  // But we can check if drawable size is zero and skip frame if so
  CGSize current_size = layer_.drawableSize;
  if (current_size.width == 0 || current_size.height == 0) {
    // Layer size not set yet - skip frame
    return;
  }

  // Get drawable for this frame
  current_drawable_ = [layer_ nextDrawable];
  if (!current_drawable_) {
    return;  // Skip frame if no drawable available
  }

  // Create command buffer
  current_command_buffer_ = [command_queue_ commandBuffer];

  // Setup render pass descriptor
  MTLRenderPassDescriptor* renderPassDescriptor = 
      [MTLRenderPassDescriptor renderPassDescriptor];
  renderPassDescriptor.colorAttachments[0].texture = current_drawable_.texture;
  renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
  renderPassDescriptor.colorAttachments[0].clearColor = 
      MTLClearColorMake(0.45, 0.55, 0.60, 1.00);
  renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;

  // Create render encoder
  current_render_encoder_ = 
      [current_command_buffer_ renderCommandEncoderWithDescriptor:renderPassDescriptor];
  [current_render_encoder_ pushDebugGroup:@"ImGui"];

  // Call ImGui Metal NewFrame with render pass descriptor
  // This must be called after setting up the render pass descriptor
  ImGui_ImplMetal_NewFrame(renderPassDescriptor);
}

void MetalManager::EndFrame() {
  if (!current_drawable_ || !current_command_buffer_ || !current_render_encoder_) {
    return;  // Skip if Metal resources weren't set up
  }

  // End encoding
  [current_render_encoder_ popDebugGroup];
  [current_render_encoder_ endEncoding];
  current_render_encoder_ = nil;

  // Present drawable and commit command buffer
  [current_command_buffer_ presentDrawable:current_drawable_];
  [current_command_buffer_ commit];
  
  // Wait until the command buffer is scheduled for execution.
  // This provides proper frame pacing by blocking until the GPU starts executing,
  // similar to how DirectX's Present(1, 0) blocks until vsync.
  // Without this, the main loop would keep spinning and rendering frames ahead,
  // causing high CPU usage even with displaySyncEnabled.
  [current_command_buffer_ waitUntilScheduled];
  
  current_drawable_ = nil;
  current_command_buffer_ = nil;
}

void MetalManager::HandleResize(int width, int height) {
  if (layer_ == nil) {
    return;
  }

  if (width == 0 || height == 0) {
    LOG_WARNING("HandleResize called with zero dimensions");
    return;
  }

  layer_.drawableSize = CGSizeMake(static_cast<CGFloat>(width), 
                                    static_cast<CGFloat>(height));
}

bool MetalManager::InitializeImGui() {
  if (device_ == nil) {
    LOG_ERROR("MetalManager not initialized - cannot initialize ImGui");
    return false;
  }

  if (!ImGui_ImplMetal_Init(device_)) {
    LOG_ERROR("Failed to initialize ImGui Metal backend");
    return false;
  }

  LOG_INFO("ImGui Metal backend initialized successfully");
  return true;
}

void MetalManager::ShutdownImGui() {
  // Prevent double shutdown
  if (imgui_shutdown_) {
    LOG_WARNING("ShutdownImGui called multiple times, ignoring");
    return;
  }

  // End any active render encoder before releasing. Metal asserts in
  // [_MTLCommandEncoder dealloc] if the encoder was not ended (e.g. app exits
  // mid-frame when a quick filter is applied or window closes).
  if (current_render_encoder_ != nil) {
    [current_render_encoder_ popDebugGroup];
    [current_render_encoder_ endEncoding];
    current_render_encoder_ = nil;
  }
  current_drawable_ = nil;
  current_command_buffer_ = nil;

  // Shutdown ImGui Metal backend
  ImGui_ImplMetal_Shutdown();
  imgui_shutdown_ = true;
  LOG_INFO("ImGui Metal backend shut down");
}

void MetalManager::RenderImGui() {
  if (!current_command_buffer_ || !current_render_encoder_) {
    return;
  }

  // Render ImGui draw data
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), current_command_buffer_, 
                                  current_render_encoder_);
}

bool MetalManager::IsInitialized() const {
  return device_ != nil && command_queue_ != nil && layer_ != nil;
}

#endif  // __APPLE__

