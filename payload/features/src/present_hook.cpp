#include "cs2/features/present_hook.hpp"

#include <cstdio>

#include "cs2/diag/watchdog.hpp"
#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#endif

namespace cs2::features {
namespace {

#if defined(_WIN32)
// Append a line to the payload log (under the payload's writable data dir) so
// hook activity is observable from outside the target. Uses Win32 file I/O, not
// the CRT (a manually mapped module only has static-CRT per-thread state on the
// injector's entry thread).
void log_line(const char* text) noexcept {
  using CreateFileAFn = HANDLE(__stdcall*)(const char*, unsigned long, unsigned long, void*,
                                           unsigned long, unsigned long, HANDLE);
  static CreateFileAFn create_file = nullptr;
  if (create_file == nullptr) {
    HMODULE kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == nullptr) {
      return;
    }
    create_file = reinterpret_cast<CreateFileAFn>(GetProcAddress(kernel32, "CreateFileA"));
    if (create_file == nullptr) {
      return;
    }
  }
  char path[MAX_PATH];
  if (!cs2::game::data_file_path("cs2-payload.log", path, sizeof(path))) {
    return;
  }

  constexpr unsigned long k_file_append_data = 0x4;
  constexpr unsigned long k_file_share_read = 0x1;
  constexpr unsigned long k_open_always = 0x4;
  constexpr unsigned long k_file_attribute_normal = 0x80;
  HANDLE file = create_file(path, k_file_append_data, k_file_share_read, nullptr,
                            k_open_always, k_file_attribute_normal, nullptr);
  if (file == nullptr || file == INVALID_HANDLE_VALUE) {
    return;
  }
  std::size_t length = 0;
  while (text[length] != '\0') {
    ++length;
  }
  unsigned long written = 0;
  WriteFile(file, text, static_cast<unsigned long>(length), &written, nullptr);
  WriteFile(file, "\n", 1, &written, nullptr);
  CloseHandle(file);
}
#endif

}  // namespace

PresentHook::PresentHook(render::UiBackend& backend, Overlay& overlay) noexcept
    : backend_(&backend), overlay_(&overlay) {}

PresentHook::~PresentHook() { uninstall(); }

void PresentHook::present_callback(void* user, void* swapchain) noexcept {
  static_cast<PresentHook*>(user)->on_present(swapchain);
}

void PresentHook::resize_callback(void* user) noexcept {
  static_cast<PresentHook*>(user)->on_resize();
}

auto PresentHook::install() noexcept -> bool {
  if (installed_) {
    return true;
  }
#if defined(_WIN32)
  log_line("[native-dx11] installing DXGI renderer");
  // Only interpose when the graphics stack is already present (the game). Keeps
  // the payload safe to map into non-graphical targets.
  if (GetModuleHandleW(L"dxgi.dll") == nullptr || GetModuleHandleW(L"d3d11.dll") == nullptr) {
    std::puts("[cs2-features] dxgi/d3d11 not loaded; skipping Present hook");
    return false;
  }

  // Replaces the swapchain Present/ResizeBuffers slots in the discovered method
  // table instead of MinHook-patching the Present function body (DXVK only
  // conditionally reserves a hot-patchable Present entry; ADR-0015).
  if (!interposer_.install(&present_callback, &resize_callback, this)) {
    std::puts("[cs2-features] DXGI interposition failed");
    return false;
  }
  installed_ = true;
  std::puts("[cs2-features] IDXGISwapChain::Present interposed (DXGI method table)");
  log_line("[native-dx11] Present hook installed");
  return true;
#else
  return false;
#endif
}

void PresentHook::uninstall() noexcept {
  if (!installed_) {
    return;
  }
  interposer_.uninstall();
  const std::scoped_lock lock(frame_mutex_);
  if (backend_ready_ && backend_ != nullptr) {
    backend_->shutdown();
    backend_ready_ = false;
  }
#if defined(_WIN32)
  if (context_.device_context) static_cast<ID3D11DeviceContext*>(context_.device_context)->Release();
  if (context_.device) static_cast<ID3D11Device*>(context_.device)->Release();
#endif
  context_ = {};
  installed_ = false;
}

void PresentHook::on_resize() noexcept {
  const std::scoped_lock lock(frame_mutex_);
  // Runs before the game's ResizeBuffers: drop any back-buffer reference the
  // backend holds, or the resize fails and the game stalls.
  if (backend_ready_ && backend_ != nullptr) {
    backend_->on_before_resize();
  }
}

void PresentHook::on_present(void* swapchain) noexcept {
  const std::scoped_lock lock(frame_mutex_);
  ++frames_;
  diag::heartbeat();
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
  if (FAILED(chain->GetDesc(&desc)) || desc.OutputWindow == nullptr) {
    if (device_context) device_context->Release();
    device->Release();
    return;
  }

  // The game can recreate the device on a mode change; rebuild the backend on
  // the new device instead of using a stale one.
  if (backend_ready_ && context_.device != nullptr && (context_.device != device || context_.swapchain != swapchain || context_.hwnd != desc.OutputWindow)) {
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

  if (backend_ready_ && backend_ != nullptr) {
    const auto screen_width = static_cast<float>(desc.BufferDesc.Width);
    const auto screen_height = static_cast<float>(desc.BufferDesc.Height);
    const render::OverlayModel& model = overlay_->update(screen_width, screen_height);
    backend_->render(context_, model);
    if (frames_ % 300 == 0) {
      log_line("presenting frames");
    }
  }
#endif
}

}  // namespace cs2::features
