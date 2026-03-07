#include "DirectXManager.h"
#include "utils/Logger.h"

#include <array>
#include <dxgi.h>

#include "imgui.h"
#include "imgui_impl_dx11.h"

// GLFW includes for native window access
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// DirectX configuration constants
namespace direct_x_constants {
  // Present parameters
  constexpr UINT kPresentSyncInterval = 1;  // Enable vsync (wait for vertical blank)
  constexpr UINT kPresentFlags = 0;          // No special present flags

  // Swap chain configuration
  constexpr UINT kSwapChainBufferCount = 2;  // Double buffering
  constexpr UINT kRefreshRateNumerator = 60;
  constexpr UINT kRefreshRateDenominator = 1;

  // Multisampling configuration
  constexpr UINT kSampleCount = 1;   // No multisampling
  constexpr UINT kSampleQuality = 0; // No multisampling quality

  // Render target configuration
  constexpr UINT kRenderTargetCount = 1;  // Single render target

  // Clear color (ImGui default background: light gray-blue)
  constexpr std::array<float, 4> kClearColor = {0.45F, 0.55F, 0.60F, 1.00F};

  // Feature levels to try (in order of preference)
  constexpr UINT kFeatureLevelCount = 2;
}

DirectXManager::DirectXManager() = default;

DirectXManager::~DirectXManager() { DirectXManager::Cleanup(); }

bool DirectXManager::Initialize(GLFWwindow* window) {
  if (pd3d_device_ != nullptr) {
    LOG_ERROR("DirectXManager already initialized");
    return false;
  }

  if (window == nullptr) {
    LOG_ERROR("Invalid GLFW window provided to Initialize");
    return false;
  }

  // Extract native HWND from GLFW window
  HWND h_wnd = glfwGetWin32Window(window);
  if (h_wnd == nullptr || !IsWindow(h_wnd)) {
    LOG_ERROR("Failed to get native window handle from GLFW window");
    return false;
  }

  if (!CreateDeviceD3D(h_wnd)) {
    LOG_ERROR("Failed to initialize Direct3D");
    return false;
  }

  LOG_INFO("Direct3D initialized successfully");
  return true;
}

void DirectXManager::Cleanup() {
  CleanupDeviceD3D();
}

void DirectXManager::BeginFrame() {
  // Call ImGui DirectX11 NewFrame (platform-specific ImGui frame setup)
  ImGui_ImplDX11_NewFrame();
}

void DirectXManager::EndFrame() {
  Present();
}

void DirectXManager::Present() {
  if (!p_swap_chain_) {
    return;
  }

  HRESULT hr = p_swap_chain_->Present(direct_x_constants::kPresentSyncInterval,
                                      direct_x_constants::kPresentFlags);
  if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    LOG_ERROR_BUILD("DirectX device lost during Present: HRESULT=0x" << std::hex << hr);
    // Note: Device recovery not implemented. Device loss is extremely rare (GPU removal,
    // driver crash) and recovery would require recreating device, swap chain, render targets,
    // and reinitializing ImGui backends. For a desktop file search application, user can
    // restart the app if this occurs. Error is logged for debugging purposes.
  } else if (FAILED(hr)) {
    LOG_ERROR_BUILD("Present failed: HRESULT=0x" << std::hex << hr);
  }
}

void DirectXManager::HandleResize(int width, int height) {
  if (pd3d_device_ == nullptr) {
    return;
  }

  if (width == 0 || height == 0) {
    LOG_WARNING("HandleResize called with zero dimensions");
    return;
  }

  CleanupRenderTarget();
  if (p_swap_chain_) {
    HRESULT hr = p_swap_chain_->ResizeBuffers(0, static_cast<UINT>(width), 
                                               static_cast<UINT>(height), 
                                               DXGI_FORMAT_UNKNOWN, 0);
    if (FAILED(hr)) {
      LOG_ERROR_BUILD("Failed to resize swap chain buffers: HRESULT=0x" << std::hex << hr);
      // Attempt to recover by recreating render target with old dimensions?
      return;
    }
    CreateRenderTarget();
  }
}

void DirectXManager::RenderImGui() {
  if (pd3d_device_context_ == nullptr || main_render_target_view_ == nullptr) {
    return;
  }

  pd3d_device_context_->OMSetRenderTargets(direct_x_constants::kRenderTargetCount,
                                           &main_render_target_view_, nullptr);
  pd3d_device_context_->ClearRenderTargetView(main_render_target_view_,
                                             direct_x_constants::kClearColor.data());
  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

bool DirectXManager::InitializeImGui() {
  if (pd3d_device_ == nullptr || pd3d_device_context_ == nullptr) {
    LOG_ERROR("DirectXManager not initialized - cannot initialize ImGui");
    return false;
  }

  if (!ImGui_ImplDX11_Init(pd3d_device_, pd3d_device_context_)) {
    LOG_ERROR("Failed to initialize ImGui DirectX11 backend");
    return false;
  }

  LOG_INFO("ImGui DirectX11 backend initialized successfully");
  return true;
}

void DirectXManager::ShutdownImGui() {
  ImGui_ImplDX11_Shutdown();
  LOG_INFO("ImGui DirectX11 backend shut down");
}

bool DirectXManager::IsInitialized() const {
  return pd3d_device_ != nullptr;
}

bool DirectXManager::CreateDeviceD3D(HWND h_wnd) {
  DXGI_SWAP_CHAIN_DESC swap_chain_desc{};
  swap_chain_desc.BufferCount = direct_x_constants::kSwapChainBufferCount;
  swap_chain_desc.BufferDesc.Width = 0;
  swap_chain_desc.BufferDesc.Height = 0;
  swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swap_chain_desc.BufferDesc.RefreshRate.Numerator = direct_x_constants::kRefreshRateNumerator;
  swap_chain_desc.BufferDesc.RefreshRate.Denominator = direct_x_constants::kRefreshRateDenominator;
  swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.OutputWindow = h_wnd;
  swap_chain_desc.SampleDesc.Count = direct_x_constants::kSampleCount;
  swap_chain_desc.SampleDesc.Quality = direct_x_constants::kSampleQuality;
  swap_chain_desc.Windowed = TRUE;
  swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  UINT create_device_flags = 0;
  D3D_FEATURE_LEVEL feature_level;
  if (const std::array<D3D_FEATURE_LEVEL, direct_x_constants::kFeatureLevelCount> feature_level_array = {
          D3D_FEATURE_LEVEL_11_0,
          D3D_FEATURE_LEVEL_10_0,
      };
      D3D11CreateDeviceAndSwapChain(
          nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, create_device_flags,
          feature_level_array.data(), direct_x_constants::kFeatureLevelCount, D3D11_SDK_VERSION,
          &swap_chain_desc, &p_swap_chain_, &pd3d_device_, &feature_level,
          &pd3d_device_context_) != S_OK) {
    return false;
  }

  CreateRenderTarget();
  return true;
}

void DirectXManager::CleanupDeviceD3D() {
  CleanupRenderTarget();
  if (p_swap_chain_) {
    p_swap_chain_->Release();
    p_swap_chain_ = nullptr;
  }
  if (pd3d_device_context_) {
    pd3d_device_context_->Release();
    pd3d_device_context_ = nullptr;
  }
  if (pd3d_device_) {
    pd3d_device_->Release();
    pd3d_device_ = nullptr;
  }
}

void DirectXManager::CreateRenderTarget() {
  if (p_swap_chain_ == nullptr || pd3d_device_ == nullptr) {
    return;
  }

  ID3D11Texture2D *p_back_buffer = nullptr;
  HRESULT hr = p_swap_chain_->GetBuffer(0, IID_PPV_ARGS(&p_back_buffer));
  if (FAILED(hr)) {
    LOG_ERROR_BUILD("Failed to get swap chain buffer: HRESULT=0x" << std::hex << hr);
    return;
  }

  hr = pd3d_device_->CreateRenderTargetView(p_back_buffer, nullptr, &main_render_target_view_);
  p_back_buffer->Release();  // Always release, even if CreateRenderTargetView fails

  if (FAILED(hr)) {
    LOG_ERROR_BUILD("Failed to create render target view: HRESULT=0x" << std::hex << hr);
    main_render_target_view_ = nullptr;  // Ensure it's null on failure
    return;
  }
}

void DirectXManager::CleanupRenderTarget() {
  if (main_render_target_view_) {
    main_render_target_view_->Release();
    main_render_target_view_ = nullptr;
  }
}
