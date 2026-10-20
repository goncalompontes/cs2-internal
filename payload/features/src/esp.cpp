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
      .providers = providers_,
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
