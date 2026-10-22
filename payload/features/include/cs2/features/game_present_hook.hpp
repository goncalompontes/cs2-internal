#pragma once

// Hooks the game's own present (`CRenderContextBase::Present` in
// `rendersystemdx11.dll`) so the overlay model is published to shared memory on
// the game's render thread — no payload thread (ADR-0016 addendum).
//
// Hook point (build 25815307): `rendersystemdx11 + 0x16dd0`, the vprof-scoped
// `CRenderContextBase::Present` that dispatches to the derived
// `CRenderContextDx11::Present` at `*(this)+0x510`. Its prologue is two
// `mov [rsp+x],reg` (10 bytes) followed by two pushes = exactly 12 bytes, so
// the engine's inline hook can relocate it without decoding relative branches.
//
// The Vulkan present layer still does the actual drawing; this hook only drives
// the state read + shm publish (which is what the removed thread did).

#include <cstdint>

#include "cs2/features/model_publisher.hpp"
#include "cs2/hooks/inline.hpp"

namespace cs2::features {

class GamePresentHook {
 public:
  explicit GamePresentHook(ModelPublisher& publisher) noexcept;
  ~GamePresentHook();

  GamePresentHook(const GamePresentHook&) = delete;
  GamePresentHook& operator=(const GamePresentHook&) = delete;

  [[nodiscard]] bool install() noexcept;
  void uninstall() noexcept;

  [[nodiscard]] bool installed() const noexcept { return installed_; }
  [[nodiscard]] void* original() const noexcept { return hook_.original(); }

  void on_present() noexcept;

 private:
  ModelPublisher* publisher_;
  hooks::InlineHook hook_;
  std::uint64_t last_publish_ms_ = 0;
  bool installed_ = false;
};

}  // namespace cs2::features
