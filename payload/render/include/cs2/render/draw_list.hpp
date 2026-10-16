#pragma once

// Screen-space drawing primitives for the ESP overlay.
//
// UI-agnostic (ADR-0007): the game/feature layer fills a `DrawList`, and any
// `UiBackend` renders it. Commands are kept in insertion order.

#include <cstdint>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace cs2::render {

struct Color {
  std::uint8_t r = 255;
  std::uint8_t g = 255;
  std::uint8_t b = 255;
  std::uint8_t a = 255;
};

struct Rect {  // outline
  float x0 = 0.0F;
  float y0 = 0.0F;
  float x1 = 0.0F;
  float y1 = 0.0F;
  Color color{};
  float thickness = 1.0F;
};

struct FilledRect {
  float x0 = 0.0F;
  float y0 = 0.0F;
  float x1 = 0.0F;
  float y1 = 0.0F;
  Color color{};
};

struct Line {
  float x0 = 0.0F;
  float y0 = 0.0F;
  float x1 = 0.0F;
  float y1 = 0.0F;
  Color color{};
  float thickness = 1.0F;
};

struct Text {
  float x = 0.0F;
  float y = 0.0F;
  Color color{};
  std::string value;
  bool centered = false;  // center horizontally on `x`
  bool shadow = true;     // 1px drop shadow (readability fallback)
  bool outline = false;   // 1px black outline on every side (ESP labels)
};

// A filled convex polygon in screen space (used for the solid hitbox capsules).
// Points are wound consistently; the backend fills the convex hull.
struct FilledPoly {
  std::vector<std::pair<float, float>> points;
  Color color{};
};

using DrawCommand = std::variant<Rect, FilledRect, Line, Text, FilledPoly>;

struct DrawList {
  std::vector<DrawCommand> commands;

  void outline(float x0, float y0, float x1, float y1, Color color, float thickness = 1.0F) {
    commands.push_back(Rect{x0, y0, x1, y1, color, thickness});
  }
  void fill(float x0, float y0, float x1, float y1, Color color) {
    commands.push_back(FilledRect{x0, y0, x1, y1, color});
  }
  void line(float x0, float y0, float x1, float y1, Color color, float thickness = 1.0F) {
    commands.push_back(Line{x0, y0, x1, y1, color, thickness});
  }
  void text(float x, float y, Color color, std::string value, bool centered = false,
            bool outline = false) {
    commands.push_back(
        Text{x, y, color, std::move(value), centered, !outline, outline});
  }
  void poly(std::vector<std::pair<float, float>> points, Color color) {
    commands.push_back(FilledPoly{std::move(points), color});
  }

  void clear() { commands.clear(); }
  [[nodiscard]] bool empty() const noexcept { return commands.empty(); }
};

}  // namespace cs2::render
