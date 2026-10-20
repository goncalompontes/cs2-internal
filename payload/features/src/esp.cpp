#include "cs2/features/esp.hpp"

#include <cstdint>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "esp_detail.hpp"

namespace cs2::features {

namespace esp_detail {

auto now_ms() noexcept -> std::uint64_t {
#if defined(_WIN32)
  return GetTickCount64();
#else
  return 0;
#endif
}

}  // namespace esp_detail

namespace {

// The `Esp`'s own providers, falling back to the payload's registry for the
// ones it does not set. The registry is how the owner wires the sound events
// and the visibility state, which live outside the features module.
auto merge_providers(const EspProviders& own) -> EspProviders {
  EspProviders merged = own;
  const EspProviders registry = esp_providers();
  if (merged.hitboxes == nullptr && registry.hitboxes != nullptr) {
    merged.hitboxes = registry.hitboxes;
    merged.hitboxes_context = registry.hitboxes_context;
  }
  if (merged.sounds == nullptr && registry.sounds != nullptr) {
    merged.sounds = registry.sounds;
    merged.sounds_context = registry.sounds_context;
  }
  if (merged.visibility == nullptr && registry.visibility != nullptr) {
    merged.visibility = registry.visibility;
    merged.visibility_context = registry.visibility_context;
  }
  return merged;
}

}  // namespace

void Esp::build(const game::Snapshot& snapshot, const game::Matrix4x4& view, float width,
                float height, render::DrawList& out, EspStats* stats) const {
  if (stats != nullptr) {
    *stats = EspStats{};
  }
  if (!snapshot.valid || width <= 0.0F || height <= 0.0F) {
    return;
  }

  // One frame, shared by the element builders. The players come first: the
  // world markers, the radar and the sounds are drawn on top of them.
  esp_detail::Frame frame{
      .snapshot = snapshot,
      .view = view,
      .width = width,
      .height = height,
      .out = out,
      .config = config_,
      .providers = merge_providers(providers_),
      .stats = stats,
      .now_ms = esp_detail::now_ms(),
      .hitboxes = hitboxes_,
      .world = world_,
      .sounds = sounds_,
  };
  frame.hitboxes.reads = 0;

  esp_detail::build_players(frame);
  esp_detail::build_world(frame);
  esp_detail::build_radar(frame);
  esp_detail::build_sounds(frame);
}

}  // namespace cs2::features
