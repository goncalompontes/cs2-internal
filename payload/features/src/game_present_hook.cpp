#include "cs2/features/game_present_hook.hpp"

#include <cstdio>
#include <cstdint>

#include "cs2/diag/watchdog.hpp"
#include "cs2/tls/bootstrap.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::features {
namespace {

#if defined(_WIN32)

// `CRenderContextBase::Present` RVA in rendersystemdx11.dll (build 25815307).
// Resolved by RE (RTTI + the vprof scope string "CRenderContextBase::Present").
constexpr std::uintptr_t k_present_rva = 0x16dd0;

// ~30 Hz publish (matches the old thread's cadence).
constexpr unsigned long long k_publish_interval_ms = 33;

using PresentFn = std::uint64_t(__fastcall*)(void* self, std::uint64_t a, std::uint64_t b,
                                             std::uint64_t c);

GamePresentHook* g_active = nullptr;

std::uint64_t __fastcall present_detour(void* self, std::uint64_t a, std::uint64_t b,
                                        std::uint64_t c) {
  if (g_active == nullptr) {
    return 0;
  }
  auto* original = reinterpret_cast<PresentFn>(g_active->original());
  g_active->on_present();
  return original(self, a, b, c);
}

#endif

}  // namespace

GamePresentHook::GamePresentHook(ModelPublisher& publisher) noexcept : publisher_(&publisher) {}

GamePresentHook::~GamePresentHook() { uninstall(); }

auto GamePresentHook::install() noexcept -> bool {
  if (installed_) {
    return true;
  }
#if defined(_WIN32)
  HMODULE module = GetModuleHandleA("rendersystemdx11.dll");
  if (module == nullptr) {
    std::puts("[cs2-features] rendersystemdx11.dll not loaded; skipping game present hook");
    return false;
  }
  void* target = reinterpret_cast<void*>(reinterpret_cast<std::uintptr_t>(module) + k_present_rva);
  g_active = this;
  const auto error = hook_.install(target, reinterpret_cast<void*>(&present_detour));
  if (error.has_value()) {
    g_active = nullptr;
    std::printf("[cs2-features] game present inline hook failed: %s\n", hooks::to_string(*error));
    return false;
  }
  installed_ = true;
  std::puts("[cs2-features] CRenderContextBase::Present hooked (game present)");
  return true;
#else
  return false;
#endif
}

void GamePresentHook::uninstall() noexcept {
  if (!installed_) {
    return;
  }
  hook_.uninstall();
  g_active = nullptr;
  installed_ = false;
}

void GamePresentHook::on_present() noexcept {
#if defined(_WIN32)
  // The render thread has no TLS block for our mapped image until we install one.
  cs2::tls::ensure_thread_tls();
  diag::heartbeat();
  const unsigned long long now = GetTickCount64();
  if (last_publish_ms_ != 0 && now - last_publish_ms_ < k_publish_interval_ms) {
    return;
  }
  last_publish_ms_ = now;
  if (publisher_ != nullptr) {
    publisher_->publish_now();
  }
#endif
}

}  // namespace cs2::features
