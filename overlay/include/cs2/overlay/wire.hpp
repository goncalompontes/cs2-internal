#pragma once

// Portable shared-memory wire format for the overlay model (ADR-0016).
//
// Written by the injected Windows payload (`features::ModelPublisher`), read by
// the Vulkan present layer. POD only — no Vulkan, no platform types — and the
// same header is compiled by both the MSVC-ABI payload (clang-cl) and the Linux
// layer (gcc), so the layouts must agree. Only fixed-size arrays and 4-byte
// scalars are used to keep the layout identical across ABIs.
//
// Synchronization: `sequence` is a seqlock around `model`. The payload writes
// an odd value, updates `model`, then writes the next even value. The layer
// retries while the sequence is odd or changed during the copy. `screen_width`/
// `screen_height` are written by the layer so the payload knows the presented
// size to project world coordinates against.

#include <atomic>
#include <cstdint>

namespace cs2::overlay::wire {

inline constexpr uint32_t k_magic = 0x324F5343U;  // 'CSO2'
inline constexpr uint32_t k_version = 4;

inline constexpr int k_max_sections = 16;
inline constexpr int k_max_lines = 64;
inline constexpr int k_max_draws = 2048;
inline constexpr int k_max_title = 64;
inline constexpr int k_max_label = 48;
inline constexpr int k_max_value = 200;
inline constexpr int k_max_text = 192;

// Force byte-identical layout across the MSVC-ABI payload and the gcc-built
// layer (their default padding for these structs differs).
#pragma pack(push, 1)

struct Line {
  char label[k_max_label];
  char value[k_max_value];
};

struct Section {
  char title[k_max_title];
  uint32_t line_count;
  Line lines[k_max_lines];
};

enum class DrawKind : uint32_t {
  rect = 0,
  filled_rect = 1,
  line = 2,
  text = 3,
};

struct Draw {
  DrawKind kind;
  float x0;
  float y0;
  float x1;
  float y1;
  float thickness;
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
  uint8_t centered;
  uint8_t shadow;   // 1px drop shadow
  uint8_t outline;  // 1px outline on every side
  uint8_t reserved;
  char text[k_max_text];
};

// Menu-editable configuration (ADR-0021). Written by the layer's menu, read by
// the payload that builds the ESP, under `settings_sequence` (seqlock, same
// convention as `model`).
struct Settings {
  uint8_t box;
  uint8_t health_bar;
  uint8_t ammo_bar;
  uint8_t name;
  uint8_t weapon;
  uint8_t flags;
  uint8_t only_enemies;
  uint8_t show_dead;
  uint8_t show_hud;       // draw the debug HUD window
  uint8_t menu_open;      // written by the layer; the payload may suppress input
  uint8_t auto_strafe;    // payload movement features (`docs/re/subtick.md`):
  uint8_t bhop;           //   silent airborne air-strafe / auto-jump
  uint8_t reserved[4];
};

struct Model {
  char title[k_max_title];
  uint32_t section_count;
  Section sections[k_max_sections];
  uint32_t draw_count;
  Draw draws[k_max_draws];
  uint32_t in_game;  // 1 when a local player is present (menu may interact)
};

struct Shared {
  uint32_t magic;
  uint32_t version;
  std::atomic<uint32_t> screen_width;   // written by the layer
  std::atomic<uint32_t> screen_height;  // written by the layer
  std::atomic<uint32_t> sequence;       // seqlock, written by the payload
  std::atomic<uint32_t> settings_sequence;  // seqlock, written by both sides
  Settings settings;
  Model model;
};

#pragma pack(pop)

}  // namespace cs2::overlay::wire
