#include "cs2/features/present_hook.hpp"

#include <cstdio>

#include "cs2/diag/watchdog.hpp"
#include "cs2/features/menu.hpp"
#include "cs2/game/log.hpp"

#if defined(_WIN32)
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#endif

namespace cs2::features {

// The present hook's activity is logged through the payload's shared log
// writer (`cs2::game::append_log`): it appends a line to the payload log
// under the payload's writable data dir, so the hook's activity is observable
// from outside the target. It uses Win32 file I/O, not the CRT (a manually
// mapped module only has static-CRT per-thread state on the injector's entry
// thread).

PresentHook::PresentHook(render::UiBackend& backend, Overlay& overlay, Menu* menu) noexcept
    : backend_(&backend), overlay_(&overlay), menu_(menu) {}

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
  cs2::game::append_log("present hook: install begin (dxgi method-table interposer)");
  // Only interpose when the graphics stack is already present (the game). Keeps
  // the payload safe to map into non-graphical targets.
  if (GetModuleHandleW(L"dxgi.dll") == nullptr || GetModuleHandleW(L"d3d11.dll") == nullptr) {
    std::puts("[cs2-features] dxgi/d3d11 not loaded; skipping Present hook");
    cs2::game::append_log("present hook: dxgi/d3d11 not loaded; skipped");
    return false;
  }

  // Replaces the swapchain Present/ResizeBuffers slots in the discovered method
  // table instead of MinHook-patching the Present function body (DXVK only
  // conditionally reserves a hot-patchable Present entry; ADR-0015).
  cs2::game::append_log("present hook: interposing (discovering method tables)");
  if (!interposer_.install(&present_callback, &resize_callback, this)) {
    std::puts("[cs2-features] DXGI interposition failed");
    cs2::game::append_log("present hook: DXGI interposition FAILED");
    return false;
  }
  installed_ = true;
  std::puts("[cs2-features] IDXGISwapChain::Present interposed (DXGI method table)");
  cs2::game::append_log("present hook installed (dxgi method-table interposer)");
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
  interposer_.uninstall();
  installed_ = false;
}

void PresentHook::on_resize() noexcept {
  // Runs before the game's ResizeBuffers: drop any back-buffer reference the
  // backend holds, or the resize fails and the game stalls.
  if (backend_ready_ && backend_ != nullptr) {
    backend_->on_before_resize();
  }
}

void PresentHook::on_present(void* swapchain) noexcept {
  ++frames_;
  diag::heartbeat();
#if defined(_WIN32)
  if (swapchain == nullptr) {
    return;
  }
  if (frames_ == 1) {
    cs2::game::append_log("present hook: first Present call");
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
    cs2::game::append_log(backend_ready_ ? "ui backend initialized" : "ui backend init FAILED");
  }

  if (backend_ready_ && backend_ != nullptr) {
    const auto screen_width = static_cast<float>(desc.BufferDesc.Width);
    const auto screen_height = static_cast<float>(desc.BufferDesc.Height);
    const render::OverlayModel& model = overlay_->update(screen_width, screen_height);
    // The in-process menu (workstream A): tick it (poll the toggle, subclass the
    // game window's WndProc, release the cursor, apply the settings) and draw it
    // inside the ImGui frame via the backend's UI callback.
    if (menu_ != nullptr) {
      menu_->tick(model.in_game, context_.hwnd);
      backend_->set_ui_callback(&Menu::ui_entry, menu_);
    }
    backend_->render(context_, model);
    if (frames_ % 300 == 0) {
      cs2::game::append_log("presenting frames");
    }
  }
#endif
}

}  // namespace cs2::features
