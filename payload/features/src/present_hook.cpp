#include "cs2/features/present_hook.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#endif

namespace cs2::features {
namespace {

using PresentFn = long(__stdcall*)(void*, unsigned, unsigned);

// Append a line to a log file next to the drive root, so hook activity is
// observable from outside the target process.
void log_line(const char* text) noexcept {
#if defined(_WIN32)
  std::FILE* file = nullptr;
  if (fopen_s(&file, "C:\\cs2-payload.log", "a") == 0 && file != nullptr) {
    std::fprintf(file, "%s\n", text);
    std::fclose(file);
  }
#else
  (void)text;
#endif
}

// Single registration point for the C-style hook callback. The detour needs to
// reach the owning instance; this is the one place a module-static is used (the
// alternative is threading a context through the hook engine, which is heavier
// than warranted here).
PresentHook* g_active = nullptr;
PresentFn g_original_present = nullptr;

long __stdcall present_detour(void* swapchain, unsigned sync_interval, unsigned flags) {
  if (g_active != nullptr) {
    g_active->on_present(swapchain);
  }
  return g_original_present(swapchain, sync_interval, flags);
}

#if defined(_WIN32)
constexpr std::size_t k_present_index = 8;

// Create a throwaway device + swapchain to obtain the swapchain vtable. The
// vtable lives in dxgi.dll's read-only data, so the pointer stays valid after
// the dummy objects are released. Window/D3D entry points are resolved at
// runtime (not imported) so the payload stays import-light and can be mapped
// into any target.
void** find_present_vtable() noexcept {
  using RegisterClassExWFn = unsigned short(__stdcall*)(const WNDCLASSEXW*);
  using CreateWindowExWFn = HWND(__stdcall*)(unsigned long, const wchar_t*, const wchar_t*,
                                             unsigned long, int, int, int, int, HWND, HMENU,
                                             HINSTANCE, void*);
  using DestroyWindowFn = int(__stdcall*)(HWND);
  using UnregisterClassWFn = int(__stdcall*)(const wchar_t*, HINSTANCE);
  using DefWindowProcWFn = long long(__stdcall*)(HWND, unsigned, unsigned long long, long long);
  using CreateDeviceFn = long(__stdcall*)(void*, int, void*, unsigned, const void*, unsigned,
                                          unsigned, const void*, void**, void**, int*, void**);

  HMODULE user32 = GetModuleHandleW(L"user32.dll");
  HMODULE d3d11 = GetModuleHandleW(L"d3d11.dll");
  if (user32 == nullptr || d3d11 == nullptr) {
    return nullptr;
  }

  const auto register_class =
      reinterpret_cast<RegisterClassExWFn>(GetProcAddress(user32, "RegisterClassExW"));
  const auto create_window =
      reinterpret_cast<CreateWindowExWFn>(GetProcAddress(user32, "CreateWindowExW"));
  const auto destroy_window =
      reinterpret_cast<DestroyWindowFn>(GetProcAddress(user32, "DestroyWindow"));
  const auto unregister_class =
      reinterpret_cast<UnregisterClassWFn>(GetProcAddress(user32, "UnregisterClassW"));
  const auto def_window_proc =
      reinterpret_cast<DefWindowProcWFn>(GetProcAddress(user32, "DefWindowProcW"));
  const auto create = reinterpret_cast<CreateDeviceFn>(
      GetProcAddress(d3d11, "D3D11CreateDeviceAndSwapChain"));
  if (register_class == nullptr || create_window == nullptr || destroy_window == nullptr ||
      unregister_class == nullptr || def_window_proc == nullptr || create == nullptr) {
    return nullptr;
  }

  WNDCLASSEXW window_class{};
  window_class.cbSize = sizeof(window_class);
  window_class.lpfnWndProc = reinterpret_cast<WNDPROC>(def_window_proc);
  window_class.hInstance = GetModuleHandleW(nullptr);
  window_class.lpszClassName = L"cs2_present_probe";
  register_class(&window_class);
  HWND window = create_window(0, window_class.lpszClassName, L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8,
                              nullptr, nullptr, window_class.hInstance, nullptr);

  DXGI_SWAP_CHAIN_DESC desc{};
  desc.BufferCount = 1;
  desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  desc.OutputWindow = window;
  desc.SampleDesc.Count = 1;
  desc.Windowed = TRUE;
  desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

  IDXGISwapChain* swapchain = nullptr;
  ID3D11Device* device = nullptr;
  ID3D11DeviceContext* context = nullptr;
  D3D_FEATURE_LEVEL level{};
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1};
  const long result =
      create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, levels, 2, D3D11_SDK_VERSION, &desc,
             reinterpret_cast<void**>(&swapchain), reinterpret_cast<void**>(&device),
             reinterpret_cast<int*>(&level), reinterpret_cast<void**>(&context));

  void** vtable = nullptr;
  if (result >= 0 && swapchain != nullptr) {
    vtable = *reinterpret_cast<void***>(swapchain);
  }
  if (swapchain != nullptr) swapchain->Release();
  if (context != nullptr) context->Release();
  if (device != nullptr) device->Release();
  if (window != nullptr) destroy_window(window);
  unregister_class(window_class.lpszClassName, window_class.hInstance);
  return vtable;
}
#endif

}  // namespace

PresentHook::PresentHook(render::UiBackend& backend) noexcept : backend_(&backend) {}

PresentHook::~PresentHook() { uninstall(); }

bool PresentHook::install() noexcept {
  if (installed_) {
    return true;
  }
  log_line("present hook: install begin");
#if defined(_WIN32)
  // Only hook when the graphics stack is already present (the game). This keeps
  // the payload safe to map into non-graphical targets.
  if (GetModuleHandleW(L"dxgi.dll") == nullptr || GetModuleHandleW(L"d3d11.dll") == nullptr) {
    std::puts("[cs2-features] dxgi/d3d11 not loaded; skipping Present hook");
    return false;
  }
  void** vtable = find_present_vtable();
  if (vtable == nullptr) {
    std::puts("[cs2-features] could not obtain the swapchain vtable");
    log_line("present hook: vtable not found");
    return false;
  }
  g_original_present = reinterpret_cast<PresentFn>(vtable[k_present_index]);
  hook_ = std::make_unique<hooks::VtableHook>(vtable, k_present_index,
                                              reinterpret_cast<void*>(&present_detour));
  if (!hook_->installed()) {
    hook_.reset();
    return false;
  }
  g_active = this;
  installed_ = true;
  std::puts("[cs2-features] IDXGISwapChain::Present hooked");
  log_line("present hook installed");
  return true;
#else
  return false;
#endif
}

void PresentHook::uninstall() noexcept {
  if (!installed_) {
    return;
  }
  if (backend_ready_ && backend_ != nullptr) {
    backend_->shutdown();
    backend_ready_ = false;
  }
  g_active = nullptr;
  hook_.reset();
  installed_ = false;
}

void PresentHook::on_present(void* swapchain) noexcept {
  ++frames_;
#if defined(_WIN32)
  if (!backend_ready_ && swapchain != nullptr) {
    auto* chain = static_cast<IDXGISwapChain*>(swapchain);
    ID3D11Device* device = nullptr;
    if (chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)) >= 0 &&
        device != nullptr) {
      ID3D11DeviceContext* device_context = nullptr;
      device->GetImmediateContext(&device_context);
      DXGI_SWAP_CHAIN_DESC desc{};
      chain->GetDesc(&desc);
      context_.device = device;
      context_.device_context = device_context;
      context_.hwnd = desc.OutputWindow;
      context_.swapchain = swapchain;
      backend_ready_ = backend_ != nullptr && backend_->initialize(context_);
      if (!backend_ready_) {
        std::puts("[cs2-features] UI backend initialization failed");
        log_line("ui backend init failed");
      } else {
        log_line("ui backend initialized (device/context/hwnd acquired)");
      }
    }
  }
#endif
  if (backend_ready_ && backend_ != nullptr) {
    backend_->render();
    if (frames_ % 300 == 0) {
      log_line("presenting frames");
    }
  }
}

}  // namespace cs2::features
