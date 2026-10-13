#include "cs2/features/dxgi_interposer.hpp"

#include <cstddef>
#include <cstdio>

#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi.h>
#include <dxgi1_2.h>
#include <windows.h>
#endif

namespace cs2::features {
namespace {

#if defined(_WIN32)

// DXGI method-table indices (IUnknown occupies 0..2 of every COM table).
constexpr std::size_t k_factory_create_swapchain = 10;            // IDXGIFactory
constexpr std::size_t k_factory2_create_swapchain_for_hwnd = 15;  // IDXGIFactory2
constexpr std::size_t k_swapchain_present = 8;                    // IDXGISwapChain
constexpr std::size_t k_swapchain_resize_buffers = 13;            // IDXGISwapChain

using PresentFn = long(__stdcall*)(void* chain, unsigned sync_interval, unsigned flags);
using ResizeBuffersFn = long(__stdcall*)(void* chain, unsigned buffer_count, unsigned width,
                                         unsigned height, int format, unsigned swapchain_flags);
using CreateSwapChainFn = long(__stdcall*)(void* factory, void* device, void* desc,
                                           void** out_chain);
using CreateSwapChainForHwndFn = long(__stdcall*)(void* factory, void* device, void* hwnd,
                                                  const void* desc, const void* fullscreen_desc,
                                                  void* output, void** out_chain);

// C-style detours need their state reachable without `this`; the interposer is
// a singleton (one swapchain per process) and owns the matching originals.
PresentFn g_present = nullptr;
ResizeBuffersFn g_resize_buffers = nullptr;
CreateSwapChainFn g_create_swapchain = nullptr;
CreateSwapChainForHwndFn g_create_swapchain_for_hwnd = nullptr;
DxgiInterposer::PresentCallback g_on_present = nullptr;
DxgiInterposer::ResizeCallback g_on_resize = nullptr;
void* g_user = nullptr;

long __stdcall present_detour(void* chain, unsigned sync_interval, unsigned flags) {
  if (g_on_present != nullptr) {
    g_on_present(g_user, chain);
  }
  return g_present(chain, sync_interval, flags);
}

long __stdcall resize_buffers_detour(void* chain, unsigned buffer_count, unsigned width,
                                     unsigned height, int format, unsigned swapchain_flags) {
  // Notify before forwarding: any cached view of the back buffer (holding a
  // reference) must be released or the game's ResizeBuffers fails/ stalls.
  if (g_on_resize != nullptr) {
    g_on_resize(g_user);
  }
  return g_resize_buffers(chain, buffer_count, width, height, format, swapchain_flags);
}

long __stdcall create_swapchain_detour(void* factory, void* device, void* desc,
                                       void** out_chain) {
  const long result = g_create_swapchain(factory, device, desc, out_chain);
  if (result >= 0 && out_chain != nullptr && *out_chain != nullptr) {
    // Every swapchain of this class shares the table we already patched. Warn
    // (once per call is too noisy, so just report) if that assumption breaks.
    void** method_table = *reinterpret_cast<void***>(*out_chain);
    if (method_table[k_swapchain_present] != reinterpret_cast<void*>(&present_detour)) {
      std::puts("[cs2-dxgi] swapchain method table differs from the discovered one");
    }
  }
  return result;
}

long __stdcall create_swapchain_for_hwnd_detour(void* factory, void* device, void* hwnd,
                                                const void* desc, const void* fullscreen_desc,
                                                void* output, void** out_chain) {
  return g_create_swapchain_for_hwnd(factory, device, hwnd, desc, fullscreen_desc, output,
                                     out_chain);
}

// Method tables read from a throwaway factory + swapchain. Both are class-level
// tables, so they cover swapchains created before or after install.
struct MethodTables {
  void** factory = nullptr;
  void** swapchain = nullptr;
  bool has_factory2 = false;
};

// Create a hidden window (no message pump required) to host the probe device.
HWND create_probe_window(HINSTANCE instance) noexcept {
  constexpr wchar_t k_class_name[] = L"cs2-dxgi-probe";
  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.style = CS_HREDRAW | CS_VREDRAW;
  window_class.lpfnWndProc = DefWindowProcW;
  window_class.hInstance = instance;
  window_class.lpszClassName = k_class_name;
  RegisterClassExW(&window_class);  // already-registered is fine
  return CreateWindowExW(0, k_class_name, L"cs2-dxgi-probe", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,
                         nullptr, nullptr, instance, nullptr);
}

void destroy_probe_window(HINSTANCE instance, HWND window) noexcept {
  if (window != nullptr) {
    DestroyWindow(window);
  }
  UnregisterClassW(L"cs2-dxgi-probe", instance);
}

bool discover_method_tables(MethodTables& out) noexcept {
  HMODULE dxgi = GetModuleHandleW(L"dxgi.dll");
  HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
  if (dxgi == nullptr || d3d11 == nullptr) {
    return false;
  }

  using CreateDXGIFactory1Fn = long(__stdcall*)(const IID&, void**);
  auto create_factory = reinterpret_cast<CreateDXGIFactory1Fn>(
      GetProcAddress(dxgi, "CreateDXGIFactory1"));
  auto create_device = reinterpret_cast<long(__stdcall*)(
      void*, int, void*, unsigned, const void*, unsigned, unsigned, void**, void**, void**)>(
      GetProcAddress(d3d11, "D3D11CreateDevice"));
  if (create_factory == nullptr || create_device == nullptr) {
    return false;
  }

  const HINSTANCE instance = GetModuleHandleW(nullptr);
  const HWND window = create_probe_window(instance);
  if (window == nullptr) {
    return false;
  }

  IDXGIFactory* factory = nullptr;
  IDXGIFactory2* factory2 = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  IDXGISwapChain* swapchain = nullptr;
  bool ok = false;

  if (create_factory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)) < 0 ||
      factory == nullptr) {
    goto cleanup;
  }
  out.factory = *reinterpret_cast<void***>(factory);
  if (factory->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2)) >= 0 &&
      factory2 != nullptr) {
    out.has_factory2 = true;
  }
  if (create_device(nullptr, /*D3D_DRIVER_TYPE_HARDWARE=*/1, nullptr, 0, nullptr, 0,
                    /*D3D11_SDK_VERSION=*/7, reinterpret_cast<void**>(&device), nullptr,
                    reinterpret_cast<void**>(&context)) < 0 ||
      device == nullptr) {
    goto cleanup;
  }

  {
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 8;
    desc.BufferDesc.Height = 8;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 1;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    if (factory->CreateSwapChain(device, &desc, &swapchain) < 0 || swapchain == nullptr) {
      goto cleanup;
    }
    out.swapchain = *reinterpret_cast<void***>(swapchain);
  }
  ok = out.factory != nullptr && out.swapchain != nullptr;

cleanup:
  if (swapchain != nullptr) swapchain->Release();
  if (context != nullptr) context->Release();
  if (device != nullptr) device->Release();
  if (factory2 != nullptr) factory2->Release();
  if (factory != nullptr) factory->Release();
  destroy_probe_window(instance, window);
  return ok;
}

#endif  // _WIN32

}  // namespace

DxgiInterposer::~DxgiInterposer() { uninstall(); }

bool DxgiInterposer::install(PresentCallback on_present, ResizeCallback on_resize,
                             void* user) noexcept {
  if (installed_) {
    return true;
  }
#if defined(_WIN32)
  if (on_present == nullptr) {
    return false;
  }

  MethodTables tables{};
  if (!discover_method_tables(tables)) {
    std::puts("[cs2-dxgi] method-table discovery failed (dxgi/d3d11 unavailable)");
    return false;
  }

  g_on_present = on_present;
  g_on_resize = on_resize;
  g_user = user;

  // Capture the originals before patching, so a Present landing between the
  // two stores never observes an unset trampoline.
  g_present = reinterpret_cast<PresentFn>(tables.swapchain[k_swapchain_present]);
  g_resize_buffers =
      reinterpret_cast<ResizeBuffersFn>(tables.swapchain[k_swapchain_resize_buffers]);
  g_create_swapchain =
      reinterpret_cast<CreateSwapChainFn>(tables.factory[k_factory_create_swapchain]);
  if (tables.has_factory2) {
    g_create_swapchain_for_hwnd = reinterpret_cast<CreateSwapChainForHwndFn>(
        tables.factory[k_factory2_create_swapchain_for_hwnd]);
  }

  present_ = std::make_unique<hooks::VtableHook>(tables.swapchain, k_swapchain_present,
                                                 reinterpret_cast<void*>(&present_detour));
  resize_buffers_ = std::make_unique<hooks::VtableHook>(
      tables.swapchain, k_swapchain_resize_buffers, reinterpret_cast<void*>(&resize_buffers_detour));
  create_swapchain_ = std::make_unique<hooks::VtableHook>(
      tables.factory, k_factory_create_swapchain, reinterpret_cast<void*>(&create_swapchain_detour));
  if (tables.has_factory2) {
    create_swapchain_for_hwnd_ = std::make_unique<hooks::VtableHook>(
        tables.factory, k_factory2_create_swapchain_for_hwnd,
        reinterpret_cast<void*>(&create_swapchain_for_hwnd_detour));
  }

  if (!present_->installed()) {
    std::puts("[cs2-dxgi] failed to patch the swapchain Present slot");
    uninstall();
    return false;
  }

  installed_ = true;
  std::puts("[cs2-dxgi] swapchain method table interposed (Present + ResizeBuffers)");
  return true;
#else
  (void)on_present;
  (void)on_resize;
  (void)user;
  return false;
#endif
}

void DxgiInterposer::uninstall() noexcept {
  if (!installed_ && present_ == nullptr) {
    return;
  }
  // VtableHook restores its original slot on destruction.
  create_swapchain_for_hwnd_.reset();
  create_swapchain_.reset();
  resize_buffers_.reset();
  present_.reset();
  g_present = nullptr;
  g_resize_buffers = nullptr;
  g_create_swapchain = nullptr;
  g_create_swapchain_for_hwnd = nullptr;
  g_on_present = nullptr;
  g_on_resize = nullptr;
  g_user = nullptr;
  installed_ = false;
}

}  // namespace cs2::features
