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

  // Damage optimum: the head cell's Chebyshev centre (the value is constant across the
  // cell, so the most robust point is the right choice; it must not depend on the
  // target's absolute bearing, which the old "nearest the chart origin" did).
  const Vec2 damage = field.argmax_damage();
  expect(near(damage.x, 0.05F) && near(damage.y, 0.0F), "damage optimum at the head centre");
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
    // The body is now the only cell; its Chebyshev centre is the optimum.
    expect(near(clipped.argmax_damage().x, -0.04F) && near(clipped.argmax_damage().y, 0.0F),
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

  // --- the inaccuracy composition (stepped by the engine value) ------------
  {
    cs2::aim::WeaponProfile w;
    w.inaccuracy_stand = 0.02F;
    w.inaccuracy_move = 0.10F;
    expect(near(cs2::aim::inaccuracy(w, 0.0F, true, false), 0.02F), "standing inaccuracy");
    expect(near(cs2::aim::inaccuracy(w, 250.0F, true, false), 0.12F), "moving adds the move term");
    w.inaccuracy = 0.005F;  // the engine's effective value (m_flInterpolatedInaccuracy)
    expect(near(cs2::aim::inaccuracy(w, 250.0F, true, false), 0.005F), "the effective value wins");
  }

  // --- the hitchance: the cone (hit) probability (docs/ragebot.md §2) -------
  {
    DamageField cone;
    cone.add(square(-0.02F, -0.02F, 0.02F, 0.02F), 100.0F);  // a 0.04-wide cell at the origin

    expect(near(cone.hit_probability(Vec2{}, 0.0F), 1.0F), "no cone, centre -> hit");
    expect(near(cone.hit_probability(Vec2{0.5F, 0.5F}, 0.0F), 0.0F), "no cone, far -> miss");
    expect(near(cone.hit_probability(Vec2{}, 0.001F), 1.0F), "a tiny cone inside -> 1");
    expect(cone.hit_probability(Vec2{0.5F, 0.5F}, 0.01F) < 0.01F, "a far cone -> ~0");
    // A cone straddling the cell edge gives a partial probability.
    const float edge = cone.hit_probability(Vec2{0.04F, 0.0F}, 0.04F);
    expect(edge > 0.0F && edge < 1.0F, "a straddling cone -> partial");
    // The hitchance optimum maximizes the probability.
    const Vec2 best = cone.argmax_hit_probability(0.04F);
    expect(cone.hit_probability(best, 0.04F) >= edge, "argmax_hit_probability improves it");
    // A wider cone over a small cell lowers the peak hitchance.
    expect(cone.hit_probability(cone.argmax_hit_probability(0.08F), 0.08F) <
               cone.hit_probability(best, 0.04F),
           "a wider cone lowers the peak hitchance");
  }

  // --- the global (whole-field) damage optimum -----------------------------
  {
    // Two equal-value cells of very different robustness: the old per-cell pick could
    // land in the thin sliver; the global search must prefer the wide block, i.e. the
    // best point with respect to the whole field, not one fragment.
    DamageField f;
    f.add(square(0.00F, 0.00F, 0.10F, 1.00F), 100.0F);   // tall, thin (margin 0.05)
    f.add(square(0.20F, -0.10F, 0.40F, 0.10F), 100.0F);  // wide block (margin 0.10)
    const Vec2 best = f.argmax_damage();
    expect(best.x > 0.20F && best.x < 0.40F && best.y > -0.10F && best.y < 0.10F,
           "global damage optimum is the robust cell, not the sliver");
    expect(near(f.value_at(best), 100.0F), "global damage optimum keeps the value");
  }

  // --- the hitchance is invariant to the aim's absolute pitch --------------
  {
    // A cone and a silhouette that share the chart's `1/cos(pitch)` metric: the
    // probability at the silhouette centre must not depend on the pitch. Ignoring the
    // metric (a circular chart cone) made the fire gate fail at a steep aim — the
    // "works level, stops working up/down" symptom.
    using cs2::aim::Basis;
    using cs2::aim::Vec3;
    const auto probability = [](float pitch) {
      constexpr float k_dist = 20.0F;
      constexpr float k_radius = 1.0F;
      const Vec3 centre{k_dist * std::cos(pitch), 0.0F, k_dist * std::sin(pitch)};
      DamageField f;
      f.add(cs2::aim::project_sphere(Vec3{}, centre, k_radius, Basis::world(), 64), 100.0F);
      const Vec2 c = cs2::aim::chebyshev_center(f.cells.front().polygon);
      return f.hit_probability(c, 0.5F * std::asin(k_radius / k_dist));
    };
    const float level = probability(0.0F);
    const float steep = probability(1.2F);
    expect(std::fabs(level - steep) < 0.12F, "hitchance is pitch-invariant");
    expect(level > 0.4F && steep > 0.4F, "hitchance positive at both pitches");
  }

  if (failures == 0) {
    std::printf("aim-field: ok\n");
    return 0;
  }
  return 1;
}
