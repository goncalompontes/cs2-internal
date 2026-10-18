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

// The sink receives the menu's shared settings block by reference; the definition is in
// `model_publisher.cpp` (which includes the full `wire.hpp`).
namespace cs2::overlay::wire {
struct Settings;
}

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

  // Movement features (`docs/re/subtick.md`): what the payload publishes into the
  // shared settings as **defaults** — the layer's menu can change them afterwards
  // (its `Movement` tab), and the sink below tells the payload when it did. Set from
  // the payload's own env (`CS2_PAYLOAD_MOVEMENT`).
  void set_movement_defaults(bool auto_strafe, bool bhop) noexcept {
    movement_auto_strafe_ = auto_strafe;
    movement_bhop_ = bhop;
  }
  // The ragebot's defaults (`docs/ragebot.md`): enabled, hold key (a VK code) and
  // auto-fire. Set from the payload's own env (`CS2_PAYLOAD_RAGE`).
  void set_rage_defaults(bool on, int key, bool fire) noexcept {
    rage_ = on;
    rage_key_ = key;
    rage_fire_ = fire;
  }
  using SettingsSink = void (*)(void* context, const cs2::overlay::wire::Settings& settings);
  // Called **every published frame** with the settings' state, so the menu is the
  // single owner of the payload features (an out-of-band change is corrected
  // rather than sticking).
  void set_settings_sink(SettingsSink sink, void* context) noexcept {
    settings_sink_ = sink;
    settings_sink_context_ = context;
  }

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
  bool movement_auto_strafe_ = false;
  bool movement_bhop_ = false;
  bool rage_ = false;
  int rage_key_ = 0;
  bool rage_fire_ = false;
  SettingsSink settings_sink_ = nullptr;
  void* settings_sink_context_ = nullptr;
#if defined(_WIN32)
  void* mapping_ = nullptr;  // HANDLE
  void* shared_ = nullptr;   // wire::Shared*
  void* thread_ = nullptr;   // HANDLE
  std::uint32_t settings_sequence_ = 0xFFFFFFFFU;
#endif
  std::atomic<bool> running_{false};
};

}  // namespace cs2::features
