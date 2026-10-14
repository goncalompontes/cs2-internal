#pragma once

// Publishes the overlay model to shared memory for the Vulkan present layer
// (ADR-0016). The payload no longer draws into the game's D3D present; it maps
// the shared segment (under the payload's writable data dir, `%LOCALAPPDATA%\
// cs2-internal` on native Windows — not `C:\` root, which a Medium-integrity
// game process cannot write), runs `Overlay::update` on a timer thread, and
// serializes the `render::OverlayModel` into the `wire::Shared` layout. The
// layer reads it.
//
// The layer writes the presented extent back into the same segment so the
// payload can project world coordinates.

#include <atomic>

#include "cs2/features/overlay.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

namespace cs2::features {

class ModelPublisher {
 public:
  explicit ModelPublisher(Overlay& overlay) noexcept : overlay_(&overlay) {}
  ~ModelPublisher();

  ModelPublisher(const ModelPublisher&) = delete;
  ModelPublisher& operator=(const ModelPublisher&) = delete;

  // Map the shared segment without starting a thread. Returns false if it
  // cannot be created (non-graphical/host builds return false).
  [[nodiscard]] bool map() noexcept;

  // Build + publish one frame synchronously. Called from a game thread (the
  // present hook) so the payload needs no thread of its own.
  void publish_now() noexcept;

  // Map the segment and start a dedicated publish thread (legacy path). Prefer
  // map() + publish_now() from a game thread.
  [[nodiscard]] bool start() noexcept;
  void stop() noexcept;

  [[nodiscard]] bool running() const noexcept { return running_.load(); }

 private:
#if defined(_WIN32)
  static unsigned long __stdcall thread_entry(void* self) noexcept;
  void run() noexcept;
  void publish(const render::OverlayModel& model, float width, float height) noexcept;
  // Menu settings (ADR-0021): publish the defaults with the segment, then adopt
  // whatever the layer's menu writes back.
  void publish_settings() noexcept;
  void apply_settings() noexcept;
#endif

  Overlay* overlay_;
#if defined(_WIN32)
  void* mapping_ = nullptr;  // HANDLE
  void* shared_ = nullptr;   // wire::Shared*
  void* thread_ = nullptr;   // HANDLE
  std::uint32_t settings_sequence_ = 0xFFFFFFFFU;
#endif
  std::atomic<bool> running_{false};
};

}  // namespace cs2::features
