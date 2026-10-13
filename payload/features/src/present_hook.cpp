#include "cs2/features/present_hook.hpp"

#include <cstdio>

#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>

#include "kiero.h"
#endif

namespace cs2::features {
namespace {

using PresentFn = long(__stdcall*)(void*, unsigned, unsigned);

#if defined(_WIN32)
// Append a line to a log file next to the drive root so hook activity is
// observable from outside the target. Uses Win32 file I/O, not the CRT (a
// manually mapped module only has static-CRT per-thread state on the injector's
// entry thread).
void log_line(const char* text) noexcept {
  using CreateFileAFn = HANDLE(__stdcall*)(const char*, unsigned long, unsigned long, void*,
                                           unsigned long, unsigned long, HANDLE);
  static CreateFileAFn create_file = nullptr;
  if (create_file == nullptr) {
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) return;
    create_file = reinterpret_cast<CreateFileAFn>(GetProcAddress(kernel32, "CreateFileA"));
    if (create_file == nullptr) return;
  }
  constexpr unsigned long k_file_append_data = 0x4;
  constexpr unsigned long k_file_share_read = 0x1;
  constexpr unsigned long k_open_always = 0x4;
  constexpr unsigned long k_file_attribute_normal = 0x80;
  HANDLE file = create_file("C:\\cs2-payload.log", k_file_append_data, k_file_share_read, nullptr,
                            k_open_always, k_file_attribute_normal, nullptr);
  if (file == nullptr || file == INVALID_HANDLE_VALUE) return;
  std::size_t length = 0;
  while (text[length] != '\0') ++length;
  unsigned long written = 0;
  WriteFile(file, text, static_cast<unsigned long>(length), &written, nullptr);
  WriteFile(file, "\n", 1, &written, nullptr);
  CloseHandle(file);
}
#endif

// Single registration point for the C-style hook callback: the detour needs to
// reach the owning instance.
PresentHook* g_active = nullptr;
PresentFn g_original_present = nullptr;

long __stdcall present_detour(void* swapchain, unsigned sync_interval, unsigned flags) {
  if (g_active != nullptr) {
    g_active->on_present(swapchain);
  }
  return g_original_present(swapchain, sync_interval, flags);
}

constexpr std::size_t k_present_index = 8;

}  // namespace

PresentHook::PresentHook(render::UiBackend& backend, Overlay& overlay) noexcept
    : backend_(&backend), overlay_(&overlay) {}

PresentHook::~PresentHook() { uninstall(); }

bool PresentHook::install() noexcept {
  if (installed_) {
    return true;
  }
#if defined(_WIN32)
  log_line("present hook: install begin");
  // Only hook when the graphics stack is already present (the game). Keeps the
  // payload safe to map into non-graphical targets.
  if (GetModuleHandleW(L"dxgi.dll") == nullptr || GetModuleHandleW(L"d3d11.dll") == nullptr) {
    std::puts("[cs2-features] dxgi/d3d11 not loaded; skipping Present hook");
    return false;
  }

  // kiero builds a throwaway D3D11 device + swapchain to read the swapchain
  // method table, then binds Present with a MinHook inline hook (not a shared
  // vtable-slot patch).
  const auto init_status = kiero::init(kiero::RenderType::D3D11);
  log_line(init_status == kiero::Status::Success ? "kiero::init ok" : "kiero::init FAILED");
  if (init_status != kiero::Status::Success) {
    std::puts("[cs2-features] kiero::init failed");
    return false;
  }
  const auto bind_status = kiero::bind(k_present_index, reinterpret_cast<void**>(&g_original_present),
                                       reinterpret_cast<void*>(&present_detour));
  log_line(bind_status == kiero::Status::Success ? "kiero::bind ok" : "kiero::bind FAILED");
  if (bind_status != kiero::Status::Success) {
    std::puts("[cs2-features] kiero::bind failed");
    return false;
  }
  g_active = this;
  installed_ = true;
  std::puts("[cs2-features] IDXGISwapChain::Present hooked (kiero)");
  log_line("present hook installed (kiero)");
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
#if defined(_WIN32)
  kiero::unbind(k_present_index);
#endif
  installed_ = false;
}

void PresentHook::on_present(void* swapchain) noexcept {
  ++frames_;
#if defined(_WIN32)
  if (swapchain == nullptr) {
    return;
  }
  auto* chain = static_cast<IDXGISwapChain*>(swapchain);
  ID3D11Device* device = nullptr;
  if (chain->GetDevice(__uuidof(ID3D11Device), reinterpret_cast<void**>(&device)) < 0 ||
      device == nullptr) {
    return;
  }
  ID3D11DeviceContext* device_context = nullptr;
  device->GetImmediateContext(&device_context);
  DXGI_SWAP_CHAIN_DESC desc{};
  chain->GetDesc(&desc);

  // The game can recreate the device on a mode change; rebuild the backend on
  // the new device instead of using a stale one.
  if (backend_ready_ && context_.device != nullptr && context_.device != device) {
    backend_->shutdown();
    backend_ready_ = false;
  }

  // Keep exactly one reference to the device and its immediate context.
  if (context_.device == device) {
    device->Release();
    device = static_cast<ID3D11Device*>(context_.device);
  } else {
    if (context_.device != nullptr) {
      static_cast<ID3D11Device*>(context_.device)->Release();
    }
    context_.device = device;
  }
  if (context_.device_context == device_context) {
    device_context->Release();
    device_context = static_cast<ID3D11DeviceContext*>(context_.device_context);
  } else {
    if (context_.device_context != nullptr) {
      static_cast<ID3D11DeviceContext*>(context_.device_context)->Release();
    }
    context_.device_context = device_context;
  }
  context_.hwnd = desc.OutputWindow;
  context_.swapchain = swapchain;

  if (!backend_ready_) {
    backend_ready_ = backend_ != nullptr && backend_->initialize(context_);
    log_line(backend_ready_ ? "ui backend initialized" : "ui backend init FAILED");
  }
#endif
  if (backend_ready_ && backend_ != nullptr) {
    const float screen_width = static_cast<float>(desc.BufferDesc.Width);
    const float screen_height = static_cast<float>(desc.BufferDesc.Height);
    const render::OverlayModel& model = overlay_->update(screen_width, screen_height);
    backend_->render(context_, model);
    if (frames_ % 300 == 0) {
      log_line("presenting frames");
    }
  }
}

}  // namespace cs2::features
