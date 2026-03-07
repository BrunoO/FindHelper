# macOS Application Class Parity Requirements

This document outlines what's required to bring `main_mac.mm` to full parity with the Windows version by using the `Application` class.

## Current State

**Windows (`main_gui.cpp`):**
- ✅ Uses `AppBootstrap` for initialization
- ✅ Uses `Application` class for main loop
- ✅ All components owned by `Application` class
- ✅ No global state

**macOS (`main_mac.mm`):**
- ❌ All initialization in `main()`
- ❌ All main loop logic in `main()`
- ❌ Components created locally (not owned by `Application`)
- ✅ No global state (after Phase 1)

## Required Changes

### 1. Create macOS Bootstrap Module (Similar to AppBootstrap)

**Problem:** macOS initialization is currently embedded in `main()`. We need a macOS-specific bootstrap module to match the Windows architecture.

**Solution:** Create `AppBootstrap_mac.h` and `AppBootstrap_mac.mm` (or extend `AppBootstrap` with macOS support).

**What needs to be bootstrapped:**
- GLFW initialization and window creation
- Metal device and command queue creation
- ImGui context and backend initialization
- Metal layer setup on NSWindow
- Settings loading
- Index loading from file (if requested)
- Window size management

**Structure:**
```cpp
// AppBootstrap_mac.h
struct AppBootstrapResultMac {
  GLFWwindow* window = nullptr;
  id<MTLDevice> device = nil;
  id<MTLCommandQueue> command_queue = nil;
  CAMetalLayer* layer = nil;
  AppSettings* settings = nullptr;
  FileIndex* file_index = nullptr;
  int* last_window_width = nullptr;
  int* last_window_height = nullptr;
  
  bool IsValid() const;
};

namespace AppBootstrapMac {
  AppBootstrapResultMac Initialize(const CommandLineArgs& cmd_args,
                                   FileIndex& file_index,
                                   int& last_window_width,
                                   int& last_window_height);
  void Cleanup(AppBootstrapResultMac& result);
}
```

**Estimated Effort:** 4-6 hours

---

### 2. Extend Application Class for macOS Metal Rendering

**Problem:** The `Application` class currently has a placeholder `RenderFrame()` for macOS. Metal rendering requires:
- Access to `CAMetalLayer` to get drawables
- Access to `MTLCommandQueue` to create command buffers
- Frame-by-frame resource management (`@autoreleasepool`, drawable acquisition)

**Current macOS rendering flow:**
```objc
@autoreleasepool {
  // Get drawable
  id<CAMetalDrawable> drawable = [layer nextDrawable];
  if (!drawable) continue;
  
  // Create command buffer
  id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
  
  // Setup render pass
  MTLRenderPassDescriptor* renderPassDescriptor = ...;
  id<MTLRenderCommandEncoder> renderEncoder = ...;
  
  // ImGui frame setup
  ImGui_ImplMetal_NewFrame(renderPassDescriptor);
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  
  // ... application logic and UI rendering ...
  
  // Render
  ImGui::Render();
  ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
  
  // Present
  [commandBuffer presentDrawable:drawable];
  [commandBuffer commit];
}
```

**Solution:** Extend `Application` class to store macOS-specific resources and implement proper Metal rendering.

**Changes needed:**

1. **Add macOS resources to Application class:**
```cpp
// Application.h
private:
#ifdef __APPLE__
  id<MTLDevice> metal_device_;              // From bootstrap
  id<MTLCommandQueue> metal_command_queue_;  // From bootstrap
  CAMetalLayer* metal_layer_;                // From bootstrap
#endif
```

2. **Update Application constructor:**
```cpp
// Application.cpp
Application::Application(AppBootstrapResultMac& bootstrap, const CommandLineArgs& cmd_args)
    : file_index_(bootstrap.file_index)
    , thread_pool_(std::make_unique<ThreadPool>(...))
    , monitor_(nullptr)  // macOS: no monitoring
    , search_worker_(*file_index_)
    , state_()
    , search_controller_()
    , settings_(bootstrap.settings)
    , window_(bootstrap.window)
    , directx_manager_(nullptr)  // macOS: no DirectX
    , last_window_width_(bootstrap.last_window_width)
    , last_window_height_(bootstrap.last_window_height)
#ifdef __APPLE__
    , metal_device_(bootstrap.device)
    , metal_command_queue_(bootstrap.command_queue)
    , metal_layer_(bootstrap.layer)
#endif
    , cmd_args_(cmd_args)
    , index_dumped_(false)
    , show_settings_(false)
    , show_metrics_(false)
{
  // Retain Metal objects (if using manual memory management)
  // [metal_device_ retain];
  // [metal_command_queue_ retain];
}
```

3. **Implement macOS RenderFrame():**
```cpp
// Application.cpp
void Application::RenderFrame() {
#ifdef __APPLE__
  @autoreleasepool {
    // Handle resize
    int width, height;
    glfwGetFramebufferSize(window_, &width, &height);
    metal_layer_.drawableSize = CGSizeMake(width, height);
    
    // Get drawable
    id<CAMetalDrawable> drawable = [metal_layer_ nextDrawable];
    if (!drawable) {
      return;  // Skip frame if no drawable
    }
    
    // Create command buffer
    id<MTLCommandBuffer> commandBuffer = [metal_command_queue_ commandBuffer];
    
    // Setup render pass
    MTLRenderPassDescriptor* renderPassDescriptor = 
        [MTLRenderPassDescriptor renderPassDescriptor];
    renderPassDescriptor.colorAttachments[0].texture = drawable.texture;
    renderPassDescriptor.colorAttachments[0].loadAction = MTLLoadActionClear;
    renderPassDescriptor.colorAttachments[0].clearColor = 
        MTLClearColorMake(0.45, 0.55, 0.60, 1.00);
    renderPassDescriptor.colorAttachments[0].storeAction = MTLStoreActionStore;
    
    // Create render encoder
    id<MTLRenderCommandEncoder> renderEncoder = 
        [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
    [renderEncoder pushDebugGroup:@"ImGui"];
    
    // Render ImGui
    ImGui::Render();
    ImGui_ImplMetal_RenderDrawData(ImGui::GetDrawData(), commandBuffer, renderEncoder);
    
    [renderEncoder popDebugGroup];
    [renderEncoder endEncoding];
    
    // Present
    [commandBuffer presentDrawable:drawable];
    [commandBuffer commit];
  }
#elif _WIN32
  // ... existing Windows rendering ...
#endif
}
```

4. **Update ProcessFrame() for macOS:**
```cpp
void Application::ProcessFrame() {
  glfwPollEvents();
  
  // Start the Dear ImGui frame
#ifdef __APPLE__
  // Metal frame setup happens in RenderFrame() after getting drawable
  // We need to setup the render pass descriptor first
  // This is a bit tricky - we may need to refactor the order
#elif _WIN32
  ImGui_ImplDX11_NewFrame();
#endif
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();
  
  // ... rest of frame processing ...
  
  // Rendering (platform-specific)
  RenderFrame();
  
  // Viewport rendering
  ImGuiIO &io = ImGui::GetIO();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
  }
}
```

**Challenge:** Metal requires the render pass descriptor to be created before `ImGui_ImplMetal_NewFrame()`, but we need to get the drawable first. This creates a chicken-and-egg problem with the current architecture.

**Possible Solutions:**
1. **Refactor frame setup order:** Move Metal setup earlier in `ProcessFrame()`, before `ImGui::NewFrame()`
2. **Two-phase rendering:** Setup Metal resources, then do ImGui frame, then render
3. **Platform-specific ProcessFrame():** Have separate implementations for Windows and macOS

**Recommended Approach:** Option 1 - Refactor to setup Metal resources before ImGui frame setup.

**Estimated Effort:** 6-8 hours (including refactoring)

---

### 3. Add Font Configuration Support

**Problem:** macOS doesn't apply font settings from `settings.json`. Windows uses `ApplyFontSettings()` in `AppBootstrap.cpp`.

**Solution:** Create `FontUtils_mac.mm` with macOS font configuration.

**What's needed:**
- Load font from file or system fonts
- Create ImGui font atlas with custom font
- Apply font size and scale settings
- Rebuild font atlas

**Reference:** Look at `FontUtils_win.cpp` for the Windows implementation pattern.

**Estimated Effort:** 2-3 hours

---

### 4. Update main_mac.mm to Use Application Class

**Current structure:**
```cpp
int main(int argc, char** argv) {
  // Parse args
  // Create FileIndex, ThreadPool
  // Setup GLFW, Metal, ImGui
  // Load index from file
  // Create components
  // Main loop (all logic here)
  // Cleanup
}
```

**Target structure:**
```cpp
int main(int argc, char** argv) {
  // Parse command line arguments
  CommandLineArgs cmd_args = ParseCommandLineArgs(argc, argv);
  
  // Handle help/version
  if (cmd_args.show_help) { ... }
  if (cmd_args.show_version) { ... }
  
  // Track window size
  int last_window_width = 1280;
  int last_window_height = 800;
  
  // Create FileIndex (will be passed to bootstrap)
  FileIndex file_index;
  
  // Initialize via macOS bootstrap
  AppBootstrapResultMac bootstrap = AppBootstrapMac::Initialize(
      cmd_args, file_index, last_window_width, last_window_height);
  
  if (!bootstrap.IsValid()) {
    LOG_ERROR("Failed to initialize application");
    return 1;
  }
  
  try {
    // Create Application instance
    Application app(bootstrap, cmd_args);
    
    // Run the application
    int exit_code = app.Run();
    
    // Cleanup
    AppBootstrapMac::Cleanup(bootstrap);
    
    return exit_code;
  } catch (const std::exception& e) {
    LOG_ERROR_BUILD("Exception: " << e.what());
    AppBootstrapMac::Cleanup(bootstrap);
    return 1;
  }
}
```

**Estimated Effort:** 2-3 hours

---

### 5. Handle Index Dump (Optional)

**Problem:** macOS doesn't support index dumping (no `UsnMonitor`).

**Current:** Index dump is handled in `Application::HandleIndexDump()`, which checks for `monitor_`. On macOS, `monitor_` is `nullptr`, so index dump is automatically skipped.

**Solution:** No changes needed - the current implementation already handles this correctly.

---

## Implementation Order

1. **Create macOS Bootstrap Module** (Step 1)
   - Extract initialization from `main_mac.mm`
   - Create `AppBootstrap_mac.h` and `AppBootstrap_mac.mm`
   - Test initialization works correctly

2. **Add Font Configuration** (Step 2)
   - Create `FontUtils_mac.mm`
   - Integrate into bootstrap
   - Test font loading

3. **Extend Application Class for macOS** (Step 3)
   - Add macOS resources to `Application` class
   - Refactor `ProcessFrame()` to handle Metal setup order
   - Implement macOS `RenderFrame()`
   - Test rendering works

4. **Update main_mac.mm** (Step 4)
   - Simplify to use bootstrap and Application class
   - Remove all initialization and main loop code
   - Test end-to-end

## Total Estimated Effort

- macOS Bootstrap: 4-6 hours
- Font Configuration: 2-3 hours
- Application Class Extension: 6-8 hours
- Update main_mac.mm: 2-3 hours
- **Total: 14-20 hours**

## Key Challenges

1. **Metal Rendering Order:** Metal requires drawable acquisition before ImGui frame setup, which conflicts with the current Windows-centric order. This requires refactoring `ProcessFrame()`.

2. **Objective-C++ Integration:** Application class needs to store Objective-C objects (`id<MTLDevice>`, etc.), which requires careful memory management.

3. **Platform-Specific Code:** Need to balance code sharing with platform-specific requirements. Consider using `#ifdef __APPLE__` guards.

4. **Testing:** macOS testing requires a Mac machine, which may not be available during development.

## Benefits After Completion

- ✅ Consistent architecture across platforms
- ✅ Shared Application class logic
- ✅ Easier maintenance (changes in one place)
- ✅ Better testability (Application class can be tested independently)
- ✅ No global state (already achieved)
- ✅ Clear separation of concerns (bootstrap vs. application logic)

## Notes

- The macOS version doesn't need `UsnMonitor` (no USN Journal on macOS), so `monitor_` will always be `nullptr` on macOS.
- Index loading from file is already supported and can be integrated into the bootstrap.
- Window size persistence is already handled in the current `main_mac.mm` and can be moved to `Application::SaveSettingsOnShutdown()`.

---

*Generated: 2025-12-25*
*Based on: Phase 1 implementation and current macOS codebase*

