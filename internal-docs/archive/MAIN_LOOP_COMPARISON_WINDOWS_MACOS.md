# Main Loop Comparison: Windows vs macOS

This document compares the main loop implementations between `main_gui.cpp` (Windows) and `main_mac.mm` (macOS) to identify any missing pieces that could cause issues.

## ✅ Currently Matching

### 1. ImGui Configuration Flags
Both platforms enable:
- `ImGuiConfigFlags_NavEnableKeyboard`
- `ImGuiConfigFlags_DockingEnable`
- `ImGuiConfigFlags_ViewportsEnable`

### 2. Viewport Style Configuration
Both platforms configure:
- `style.WindowRounding = 0.0f`
- `style.Colors[ImGuiCol_WindowBg].w = 1.0f`

### 3. Platform Window Updates
Both platforms call:
- `ImGui::UpdatePlatformWindows()`
- `ImGui::RenderPlatformWindowsDefault()`

## ⚠️ Differences That May Need Attention

### 1. Font Settings Application

**Windows (`AppBootstrap.cpp` line 327):**
```cpp
ApplyFontSettings(app_settings);
```

**macOS:** ❌ Missing - No font configuration applied

**Impact:** macOS uses default ImGui fonts. Font settings from `settings.json` are not applied.

**Recommendation:** Create `FontUtils_mac.mm` or add macOS font configuration support.

---

### 2. Rendering Order

**Windows order:**
1. `ImGui::Render()`
2. `RenderImGui()` (renders main window)
3. `UpdatePlatformWindows()` / `RenderPlatformWindowsDefault()` (renders viewports)
4. `Present()` (presents main window)

**macOS order:**
1. `ImGui::Render()`
2. `RenderDrawData()` (renders main window)
3. `presentDrawable()` / `commit()` (presents main window) ⚠️
4. `UpdatePlatformWindows()` / `RenderPlatformWindowsDefault()` (renders viewports) ⚠️

**Potential Issue:** On macOS, viewport rendering happens AFTER the main window is presented. This might be fine for Metal (since `RenderPlatformWindowsDefault()` creates its own command buffers), but the order differs from Windows.

**Recommendation:** Verify this order is correct for Metal backend. Consider moving viewport calls before `commit()` if issues arise.

---

### 3. Initialization Architecture

**Windows:** Uses `AppBootstrap` abstraction
- Centralized initialization
- Error handling
- Resource management

**macOS:** Manual initialization
- Direct GLFW/Metal setup
- Inline error handling

**Impact:** Less code reuse, but simpler for macOS-specific needs.

**Recommendation:** Consider extracting common initialization patterns if both platforms grow more complex.

---

### 4. Style Configuration

**Windows (`AppBootstrap.cpp` lines 302-307):**
```cpp
ImGuiStyle &style = ImGui::GetStyle();
style.FrameRounding = 8.0f;
style.GrabRounding = 6.0f;
```

**macOS:** ❌ Missing - No frame/grab rounding configuration

**Impact:** macOS windows have sharper corners (0.0f default vs 8.0f on Windows).

**Recommendation:** Add style configuration for consistency:
```cpp
ImGuiStyle &style = ImGui::GetStyle();
style.FrameRounding = 8.0f;
style.GrabRounding = 6.0f;
```

---

### 5. Index Dump Feature

**Windows:** Has index dump logic in main loop (lines 98-112)

**macOS:** ❌ Missing - No index dump support

**Impact:** Cannot dump index to file on macOS.

**Recommendation:** Add index dump support if needed for macOS testing/debugging.

---

## 🔍 Code Structure Comparison

### Main Loop Structure

**Windows:**
```cpp
while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    
    // Application logic
    ApplicationLogic::Update(...);
    
    // UI rendering
    UIRenderer::RenderMainWindow(...);
    
    // Rendering
    ImGui::Render();
    RenderImGui();
    UpdatePlatformWindows();
    RenderPlatformWindowsDefault();
    Present();
}
```

**macOS:**
```cpp
while (!glfwWindowShouldClose(window)) {
    @autoreleasepool {
        glfwPollEvents();
        
        // Metal-specific: Get drawable, create command buffer
        id<CAMetalDrawable> drawable = [layer nextDrawable];
        id<MTLCommandBuffer> commandBuffer = [commandQueue commandBuffer];
        // ... setup render pass descriptor ...
        
        ImGui_ImplMetal_NewFrame(renderPassDescriptor);
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        
        // Application logic
        ApplicationLogic::Update(...);
        
        // UI rendering
        UIRenderer::RenderMainWindow(...);
        
        // Rendering
        ImGui::Render();
        ImGui_ImplMetal_RenderDrawData(..., commandBuffer, renderEncoder);
        [commandBuffer presentDrawable:drawable];
        [commandBuffer commit];
        
        UpdatePlatformWindows();
        RenderPlatformWindowsDefault();
    }
}
```

---

## 📋 Checklist for Future Parity

- [ ] Add font configuration support on macOS (`ApplyFontSettings` equivalent)
- [ ] Verify viewport rendering order is correct for Metal backend
- [ ] Add frame/grab rounding style configuration on macOS
- [ ] Consider adding index dump support on macOS (if needed)
- [ ] Document any platform-specific rendering order requirements

---

## 🎯 Key Takeaway

The most critical missing piece was the **viewport platform window calls** (`UpdatePlatformWindows()` / `RenderPlatformWindowsDefault()`), which we've now added. The other differences are mostly cosmetic (style) or feature-specific (fonts, index dump) and don't affect core functionality.

