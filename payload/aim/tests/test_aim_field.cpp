// Unit tests for the aim damage field (docs/ragebot.md, ADR-0022).

#include <cmath>
#include <cstdio>
#include <vector>

#include "cs2/aim/field.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 1e-3F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

auto square(float x0, float y0, float x1, float y1) -> cs2::aim::Polygon {
  cs2::aim::Polygon poly;
  poly.points = {{x0, y0}, {x1, y0}, {x1, y1}, {x0, y1}};
  return poly;
}

}  // namespace

auto main() -> int {
  using cs2::aim::DamageField;
  using cs2::aim::Vec2;

  // A small, high-value head cell and a large, low-value body cell.
  DamageField field;
  field.add(square(0.04F, -0.01F, 0.06F, 0.01F), 100.0F);
  field.add(square(-0.10F, -0.10F, 0.02F, 0.10F), 10.0F);

  expect(!field.empty(), "field is non-empty");
  expect(near(field.value_at(Vec2{0.05F, 0.0F}), 100.0F), "head value");
  expect(near(field.value_at(Vec2{-0.05F, 0.0F}), 10.0F), "body value");
  expect(near(field.value_at(Vec2{0.5F, 0.5F}), 0.0F), "outside value");

  // Damage optimum: the head cell's point nearest the origin.
  const Vec2 damage = field.argmax_damage();
  expect(near(damage.x, 0.04F) && near(damage.y, 0.0F), "damage optimum on the head's near edge");
  expect(near(field.value_at(damage), 100.0F), "damage optimum has head value");

  // Hitchance optimum: the head cell's Chebyshev centre.
  const Vec2 chance = field.argmax_hitchance();
  expect(near(chance.x, 0.05F) && near(chance.y, 0.0F), "hitchance optimum at the head centre");

  // Clipping: keep x <= 0.045 -> the head survives (narrower), the body too.
  {
    DamageField clipped = field;
    clipped.clip(Vec2{1.0F, 0.0F}, 0.045F);
    expect(near(clipped.value_at(Vec2{0.042F, 0.0F}), 100.0F), "clip keeps the head sliver");
  }
  // Keep x <= 0.03 -> the head vanishes, the body is best.
  {
    DamageField clipped = field;
    clipped.clip(Vec2{1.0F, 0.0F}, 0.03F);
    expect(near(clipped.value_at(Vec2{0.042F, 0.0F}), 0.0F), "clip removes the head");
    expect(near(clipped.value_at(Vec2{-0.05F, 0.0F}), 10.0F), "clip keeps the body");
    // The body now contains the chart origin, so the nearest point is the origin.
    expect(near(clipped.argmax_damage().x, 0.0F) && near(clipped.argmax_damage().y, 0.0F),
           "body is now the damage optimum");
  }

  // Expected value: no blur matches the damage optimum; a wide blur prefers the
  // larger body cell.
  expect(near(field.argmax_expected(0.0F).x, 0.04F, 5e-2F), "zero-sigma matches damage optimum");
  const Vec2 blurred = field.argmax_expected(0.05F);
  expect(blurred.x < 0.03F, "wide blur prefers the large body cell");

  // --- the pellet-sum objective (docs/ragebot.md §2/§3) --------------------
  {
    DamageField pellet_field;
    pellet_field.add(square(-0.01F, -0.01F, 0.01F, 0.01F), 100.0F);  // a cell at the origin
    pellet_field.add(square(0.09F, -0.01F, 0.11F, 0.01F), 100.0F);   // a cell at x = 0.1
    const std::vector<Vec2> apart{Vec2{0.0F, 0.0F}, Vec2{0.1F, 0.0F}};

    // From the origin, pellet 1 (aim + 0.1) lands on the far cell: 100 + 100.
    expect(near(pellet_field.value_at_pellets(Vec2{0.0F, 0.0F}, apart), 200.0F),
           "two pellets sum over two cells");
    // A single pellet is the single-ray value.
    expect(
        near(pellet_field.value_at_pellets(Vec2{0.0F, 0.0F}, std::vector<Vec2>{Vec2{0.0F, 0.0F}}),
             100.0F),
        "one pellet is the single-ray value");
    // The optimum reaches the two-pellet sum.
    const Vec2 pellet_aim = pellet_field.argmax_pellets(apart);
    expect(near(pellet_field.value_at_pellets(pellet_aim, apart), 200.0F),
           "the pellet optimum reaches the sum");
    // K=1 reduces to the damage optimum.
    const Vec2 single = pellet_field.argmax_pellets(std::vector<Vec2>{Vec2{0.0F, 0.0F}});
    expect(near(single.x, pellet_field.argmax_damage().x) &&
               near(single.y, pellet_field.argmax_damage().y),
           "K=1 reduces to argmax_damage");
  }

  if (failures == 0) {
    std::printf("aim-field: ok\n");
    return 0;
  }
  return 1;
}
