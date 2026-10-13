#pragma once

// Embedded pixel font used for ESP text (bounding-box labels, flags, weapon and
// player names). The byte array is generated at build time from the vendored
// TTF (`payload/third_party/fonts/`) by `scripts/gen-font.py`.

#include <cstddef>

namespace cs2::render {

extern const unsigned char k_esp_font_ttf[];
extern const std::size_t k_esp_font_ttf_size;

}  // namespace cs2::render
