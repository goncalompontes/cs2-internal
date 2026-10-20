#include "cs2/aim/field.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace cs2::aim {
namespace {

constexpr float k_epsilon = 1e-6F;

// The AABB of a convex polygon (its chart cells are small, so a linear scan is cheap).
[[nodiscard]] auto polygon_bounds(const Polygon& polygon) noexcept -> std::pair<Vec2, Vec2> {
  Vec2 mn = polygon.points.front();
  Vec2 mx = mn;
  for (const Vec2& p : polygon.points) {
    mn.x = std::min(mn.x, p.x);
    mn.y = std::min(mn.y, p.y);
    mx.x = std::max(mx.x, p.x);
    mx.y = std::max(mx.y, p.y);
  }
  return {mn, mx};
}

// Standard normal pdf / cdf (double precision for the boundary integral).
[[nodiscard]] inline auto std_pdf(double x) noexcept -> double {
  return 0.39894228040143267794 * std::exp(-0.5 * x * x);
}
[[nodiscard]] inline auto std_cdf(double x) noexcept -> double {
  return 0.5 * std::erfc(-x * 0.70710678118654752440);
}

// 8-point Gauss-Legendre nodes/weights on [-1, 1].
constexpr double k_gl_x[8] = {-0.9602898564975363, -0.7966664774136267, -0.5255324099163290,
                              -0.1834346424956498, 0.1834346424956498,  0.5255324099163290,
                              0.7966664774136267,  0.9602898564975363};
constexpr double k_gl_w[8] = {0.1012285362903763, 0.2223810344533745, 0.3137066458778873,
                              0.3626837833783620, 0.3626837833783620, 0.3137066458778873,
                              0.2223810344533745, 0.1012285362903763};

// The standard bivariate-normal (rho = 0) probability mass over a convex polygon, in
// **standard-normal chart coordinates** (vertices already translated by `-aim` and
// divided by sigma). No 2-D sampling: Green's theorem reduces the integral to the
// boundary line integral `-∮ φ(x)Φ(y) dx`. Each edge is split into ~1σ segments and
// integrated with 8-point Gauss-Legendre (the integrand is peaked on the σ scale, so a
// single rule per edge would undersample a long edge). O(edges · segments).
//
// When translation gradient pointers are non-null, it also returns the derivative of the
// mass with respect to the *un-scaled* translation of the polygon (the boundary-flux form):
//   ∂M/∂a_u = (1/σu)·∮ φφ dv,   ∂M/∂a_v = -(1/σv)·∮ φφ du
// For fixed unscaled polygon/aim, the scale derivative is
//   ∂M/∂σu = -(1/σu)·∮ φ(x)Φ(y)(x² - 1) dx.
// All derivatives reuse the same quadrature nodes.
[[nodiscard]] auto polygon_normal_mass(const Polygon& scaled, double* gradient_u,
                                       double* gradient_v, double* gradient_sigma_u = nullptr,
                                       double sigma_u = 1.0) noexcept -> double {
  const std::size_t n = scaled.points.size();
  if (n < 3) {
    return 0.0;
  }
  double total = 0.0;         // ∮ φΦ du
  double flux_u = 0.0;        // ∮ φφ dv
  double flux_v = 0.0;        // ∮ φφ du
  double flux_sigma_u = 0.0;  // ∮ φΦ (u² - 1) du
  for (std::size_t i = 0; i < n; ++i) {
    const Vec2 a = scaled.points[i];
    const Vec2 b = scaled.points[(i + 1) % n];
    const double dx = static_cast<double>(b.x) - static_cast<double>(a.x);
    const double dy = static_cast<double>(b.y) - static_cast<double>(a.y);
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length < 1e-12) {
      continue;
    }
    int segments = static_cast<int>(std::ceil(length));  // ~1 standard deviation each
    if (segments < 1) {
      segments = 1;
    }
    if (segments > 64) {
      segments = 64;
    }
    const double seg_dx = dx / segments;
    const double seg_dy = dy / segments;
    double edge_mass = 0.0;     // ∫ edge φΦ du
    double edge_grad = 0.0;     // ∫ edge φφ dt
    double edge_sigma_u = 0.0;  // ∫ edge φΦ (u² - 1) du
    for (int s = 0; s < segments; ++s) {
      const double x0 = static_cast<double>(a.x) + s * seg_dx;
      const double y0 = static_cast<double>(a.y) + s * seg_dy;
      double local_mass = 0.0;
      double local_grad = 0.0;
      double local_sigma_u = 0.0;
      for (int k = 0; k < 8; ++k) {
        const double t = 0.5 * (k_gl_x[k] + 1.0);
        const double x = x0 + t * seg_dx;
        const double y = y0 + t * seg_dy;
        const double pdf = std_pdf(x);
        local_mass += 0.5 * k_gl_w[k] * pdf * std_cdf(y);
        local_grad += 0.5 * k_gl_w[k] * pdf * std_pdf(y);
        local_sigma_u += 0.5 * k_gl_w[k] * pdf * std_cdf(y) * (x * x - 1.0);
      }
      edge_mass += seg_dx * local_mass;
      edge_grad += local_grad;
      edge_sigma_u += seg_dx * local_sigma_u;
    }
    total += edge_mass;
    flux_u += seg_dy * edge_grad;
    flux_v += seg_dx * edge_grad;
    flux_sigma_u += edge_sigma_u;
  }
  // The signed mass `-total` follows the winding; flip the flux with it so the gradient
  // matches the (positive) mass.
  const double mass = -total;
  const double sign = mass < 0.0 ? -1.0 : 1.0;
  if (gradient_u != nullptr) {
    *gradient_u = sign * flux_u;
  }
  if (gradient_v != nullptr) {
    *gradient_v = sign * flux_v;
  }
  if (gradient_sigma_u != nullptr) {
    *gradient_sigma_u = sign * -flux_sigma_u / sigma_u;
  }
  return std::fabs(mass);
}

}  // namespace

namespace {

constexpr float k_field_epsilon = 1e-4F;

// How many standard deviations of the cone kernel still matter. The mass of a convex
// region entirely outside the `k_mass_cutoff`-sigma box is at most the Gaussian tail
// `Φ(-cutoff) = 6.2e-16`, so dropping such a region changes the objective by less than
// `cells · 6.2e-16` — far below the `1e-4` the aim's tie-breaks resolve. This is a
// *proven* bound, not a heuristic: every region inside the box is still integrated
// exactly, so the result is the exact optimum of the model to that precision.
// Without it, each evaluation integrates every cell's full boundary (`docs/ragebot-perf.md`
// §3.9: the cone objective was the dominant cost at a small sigma).
constexpr float k_mass_cutoff = 8.0F;

// Does the polygon's AABB (in standard-normal chart units, i.e. already scaled by the
// kernel) intersect the `±k_mass_cutoff` box? A polygon outside it contributes only the
// negligible Gaussian tail above.
[[nodiscard]] auto within_mass_cutoff(const Polygon& scaled) noexcept -> bool {
  float min_x = scaled.points.front().x;
  float max_x = min_x;
  float min_y = scaled.points.front().y;
  float max_y = min_y;
  for (const Vec2& p : scaled.points) {
    min_x = std::min(min_x, p.x);
    max_x = std::max(max_x, p.x);
    min_y = std::min(min_y, p.y);
    max_y = std::max(max_y, p.y);
  }
  return max_x >= -k_mass_cutoff && min_x <= k_mass_cutoff && max_y >= -k_mass_cutoff &&
         min_y <= k_mass_cutoff;
}

// The "interesting" chart points of the field: each candidate cell's Chebyshev centre,
// its centroid, its vertices, and its point nearest the field centroid. The optima are
// searched over this finite set, so the answer is the global best point rather than a
// single cell's own optimum.
//
// Only cells that can reach the optimal value contribute candidates: the damage of a
// point is the max over its containing cells, so the maximum achievable value is
// `max(cell.value)` and a point attaining it must lie in a cell with that value. Cells
// below the floor are therefore skipped — this drops candidates that provably cannot
// improve the objective, not geometry (`docs/ragebot-perf.md` §3.9).
[[nodiscard]] auto field_candidates(const std::vector<Cell>& cells, float value_floor)
    -> std::vector<Vec2> {
  std::vector<Vec2> out;
  out.reserve(cells.size() * 12);
  Vec2 sum{};
  std::size_t points = 0;
  for (const Cell& cell : cells) {
    for (const Vec2& p : cell.polygon.points) {
      sum = sum + p;
      ++points;
    }
  }
  const Vec2 centre = points > 0 ? sum * (1.0F / static_cast<float>(points)) : Vec2{};
  for (const Cell& cell : cells) {
    if (cell.value + k_field_epsilon < value_floor) {
      continue;
    }
    out.push_back(chebyshev_center(cell.polygon));
    out.push_back(polygon_centroid(cell.polygon));
    out.push_back(nearest_point_to(cell.polygon, centre));
    for (const Vec2& p : cell.polygon.points) {
      out.push_back(p);
    }
  }
  out.push_back(centre);
  return out;
}

// The largest distance the aim at `point` can drift before it leaves the union of the
// cells currently covering it: the best per-cell margin among the cells that contain it.
// Only the cells near `point` are examined (the grid is a pure acceleration).
[[nodiscard]] auto field_margin(const DamageField& field, const Vec2& point,
                                std::vector<int>& scratch) -> float {
  field.cells_near(point, scratch);
  float best = -1.0F;
  for (const int index : scratch) {
    const Cell& cell = field.cells[static_cast<std::size_t>(index)];
    if (polygon_contains(cell.polygon, point)) {
      best = std::max(best, polygon_margin(cell.polygon, point));
    }
  }
  return best;
}

// The weighted Gaussian-mass objective and its gradient at `aim`, over **every** cell.
// `weighted` selects `Σ value_i·mass_i` (expected value) or `Σ mass_i` (hit probability).
// The gradient is the exact boundary-flux derivative of the same mass sum, so no cell is
// skipped and no sampling is involved.
struct MassGradient {
  double value = 0.0;
  double gx = 0.0;
  double gy = 0.0;
};

[[nodiscard]] auto mass_gradient(const std::vector<Cell>& cells, const Vec2& aim, float sigma_u,
                                 float sigma_v, bool weighted) -> MassGradient {
  MassGradient out;
  Polygon scaled;
  const double cosine = std::cos(static_cast<double>(aim.y));
  const double sigma_u_pitch =
      cosine > 0.05
          ? static_cast<double>(sigma_v) * std::sin(static_cast<double>(aim.y)) / (cosine * cosine)
          : 0.0;
  for (const Cell& cell : cells) {
    scaled.points.clear();
    scaled.points.reserve(cell.polygon.points.size());
    for (const Vec2& p : cell.polygon.points) {
      scaled.points.push_back(Vec2{.x = (p.x - aim.x) / sigma_u, .y = (p.y - aim.y) / sigma_v});
    }
    if (!within_mass_cutoff(scaled)) {
      continue;  // its mass is below the Gaussian tail bound (see `k_mass_cutoff`)
    }
    double gx = 0.0;
    double gy = 0.0;
    double sigma_u_gradient = 0.0;
    const double mass = polygon_normal_mass(scaled, &gx, &gy, &sigma_u_gradient, sigma_u);
    const double weight = weighted ? static_cast<double>(cell.value) : 1.0;
    out.value += weight * mass;
    // ∂mass/∂a_u = -(1/σu)·∮φφ dv ; ∂mass/∂a_v = +(1/σv)·∮φφ du  (the translate
    // direction flips the flux sign; `gx`/`gy` are `∮φφ dv` / `∮φφ du`).
    out.gx -= weight * gx / sigma_u;
    out.gy += weight * gy / sigma_v;
    // The chart covariance depends on pitch: σu = σv / cos(pitch). Include its chain rule.
    out.gy += weight * sigma_u_gradient * sigma_u_pitch;
  }
  return out;
}

// Maximize the smooth objective by **gradient ascent with a backtracking line search**
// (a few Newton-like steps, no grid, no sampling). Seeded from the highest-value cells.
template <typename Evaluate>
[[nodiscard]] auto mass_ascent(const std::vector<Cell>& cells, float sigma, bool weighted,
                               Evaluate&& evaluate) -> Vec2 {
  std::vector<std::size_t> order(cells.size());
  for (std::size_t i = 0; i < order.size(); ++i) {
    order[i] = i;
  }
  const std::size_t top = std::min<std::size_t>(cells.size(), 4);
  std::partial_sort(
      order.begin(), order.begin() + top, order.end(),
      [&](std::size_t a, std::size_t b) -> auto { return cells[a].value > cells[b].value; });

  Vec2 best{};
  double best_value = -1.0;
  const auto consider = [&](const Vec2& candidate) -> auto {
    const double value = evaluate(candidate);
    if (value > best_value) {
      best_value = value;
      best = candidate;
    }
  };
  for (std::size_t i = 0; i < top; ++i) {
    consider(chebyshev_center(cells[order[i]].polygon));
    consider(polygon_centroid(cells[order[i]].polygon));
  }

  float min_x = best.x;
  float max_x = best.x;
  float min_y = best.y;
  float max_y = best.y;
  for (const Cell& cell : cells) {
    for (const Vec2& p : cell.polygon.points) {
      min_x = std::min(min_x, p.x);
      max_x = std::max(max_x, p.x);
      min_y = std::min(min_y, p.y);
      max_y = std::max(max_y, p.y);
    }
  }
  float step = 0.25F * std::max(max_x - min_x, max_y - min_y);
  if (step <= 0.0F) {
    step = 1.0e-3F;
  }
  const float sigma_v = std::max(sigma, k_epsilon);
  Vec2 aim = best;
  for (int iteration = 0; iteration < 32 && step > 1.0e-5F; ++iteration) {
    const float sigma_u = sigma_v * yaw_scale(aim.y);
    const MassGradient gradient = mass_gradient(cells, aim, sigma_u, sigma_v, weighted);
    const double norm = std::sqrt(gradient.gx * gradient.gx + gradient.gy * gradient.gy);
    if (norm < 1.0e-12) {
      break;  // a critical point: the exact optimum
    }
    const double value_before = gradient.value;
    bool moved = false;
    // 8 halvings take the step to 1/256 of its start, well below the aim resolution; 12
    // was overkill and every extra probe is a full field evaluation.
    for (int line = 0; line < 8; ++line) {
      const Vec2 candidate{.x = aim.x + static_cast<float>(step * gradient.gx / norm),
                           .y = aim.y + static_cast<float>(step * gradient.gy / norm)};
      const double value = evaluate(candidate);
      if (value > best_value + 1.0e-9) {
        aim = candidate;
        best_value = value;
        moved = true;
        break;
      }
      step *= 0.5F;  // backtrack
    }
    if (!moved) {
      break;
    }
    // Convergence: once a step improves the objective by less than the resolution the aim
    // can act on, further ascent cannot change the chosen direction. This replaces a fixed
    // 32-iteration budget that was the cone objective's dominant cost when it ran per
    // solve and per backtrack pose (`docs/ragebot-perf.md` §3.9).
    if (best_value - value_before <= 1.0e-4 * std::max(1.0, best_value)) {
      break;
    }
  }
  return aim;
}

}  // namespace

void DamageField::add(const Polygon& polygon, float value, float base) {
  if (polygon.empty() || value <= 0.0F) {
    return;
  }
  cells.push_back(
      Cell{.polygon = polygon, .value = value, .base_value = base >= 0.0F ? base : value});
  grid_.cell_count = 0;  // the index is stale
}

void DamageField::add_hitbox(const Polygon& polygon, float hitgroup_multiplier, float distance,
                             const WeaponProfile& weapon) {
  add(polygon,
      weapon.damage * hitgroup_multiplier * range_falloff(weapon.range_modifier, distance));
}

void DamageField::clip(const Vec2& normal, float offset) {
  for (Cell& cell : cells) {
    cell.polygon = clip_half_plane(cell.polygon, normal, offset);
  }
  std::erase_if(cells, [](const Cell& cell) -> bool { return cell.polygon.empty(); });
  grid_.cell_count = 0;  // the index is stale
}

// Build the uniform grid over the cells' AABBs. Bucket size targets ~1 cell per bucket
// (so a probe touches a handful of cells), and a cell whose AABB spans too many buckets
// is kept in `large` and tested unconditionally — that bounds the memory per cell while
// keeping the query exact.
void DamageField::build_index() const {
  PointGrid grid;
  grid.cell_count = cells.size();
  if (cells.empty()) {
    grid_ = std::move(grid);
    return;
  }

  Vec2 mn = polygon_bounds(cells.front().polygon).first;
  Vec2 mx = polygon_bounds(cells.front().polygon).second;
  for (const Cell& cell : cells) {
    const auto [lo, hi] = polygon_bounds(cell.polygon);
    mn.x = std::min(mn.x, lo.x);
    mn.y = std::min(mn.y, lo.y);
    mx.x = std::max(mx.x, hi.x);
    mx.y = std::max(mx.y, hi.y);
  }
  const float width = mx.x - mn.x;
  const float height = mx.y - mn.y;
  if (!(width > 0.0F) || !(height > 0.0F)) {
    grid_ = std::move(grid);  // degenerate field: every probe falls back to the linear scan
    return;
  }

  // ~1 cell per bucket, capped at 64x64 buckets.
  const auto cells_count = static_cast<int>(cells.size());
  const int target = std::clamp(cells_count, 1, 4096);
  const float aspect = width / height;
  int nx = static_cast<int>(std::lround(std::sqrt(static_cast<double>(target) * aspect)));
  nx = std::clamp(nx, 1, 64);
  int ny = std::clamp(target / nx, 1, 64);

  const float cell_w = width / static_cast<float>(nx);
  const float cell_h = height / static_cast<float>(ny);
  grid.min = mn;
  grid.max = mx;
  grid.nx = nx;
  grid.ny = ny;

  const auto bucket_x = [&](float x) -> int {
    const int index = static_cast<int>((x - mn.x) / cell_w);
    return std::clamp(index, 0, nx - 1);
  };
  const auto bucket_y = [&](float y) -> int {
    const int index = static_cast<int>((y - mn.y) / cell_h);
    return std::clamp(index, 0, ny - 1);
  };

  constexpr int k_max_buckets_per_cell = 64;
  const auto bucket_count = static_cast<std::size_t>(nx) * static_cast<std::size_t>(ny);
  std::vector<int> counts(bucket_count + 1, 0);
  std::vector<std::pair<int, int>> ranges;  // per cell: [x0..x1]x[y0..y1] packed
  ranges.reserve(cells.size());
  for (const Cell& cell : cells) {
    const auto [lo, hi] = polygon_bounds(cell.polygon);
    const int x0 = bucket_x(lo.x);
    const int x1 = bucket_x(hi.x);
    const int y0 = bucket_y(lo.y);
    const int y1 = bucket_y(hi.y);
    const int span = (x1 - x0 + 1) * (y1 - y0 + 1);
    ranges.emplace_back(x0, x1);
    if (span > k_max_buckets_per_cell) {
      grid.large.push_back(static_cast<int>(ranges.size()) - 1);
      continue;
    }
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        ++counts[static_cast<std::size_t>(y) * static_cast<std::size_t>(nx) +
                 static_cast<std::size_t>(x) + 1];
      }
    }
  }
  // Prefix sum -> bucket start offsets, then a second pass filling `items`.
  for (std::size_t i = 1; i < counts.size(); ++i) {
    counts[i] += counts[i - 1];
  }
  grid.start = counts;
  grid.items.resize(static_cast<std::size_t>(counts.back()));
  std::vector<int> cursor(grid.start.begin(), grid.start.end() - 1);
  for (std::size_t index = 0; index < cells.size(); ++index) {
    const auto [x0, x1] = ranges[index];
    const auto [lo, hi] = polygon_bounds(cells[index].polygon);
    const int y0 = bucket_y(lo.y);
    const int y1 = bucket_y(hi.y);
    const int span = (x1 - x0 + 1) * (y1 - y0 + 1);
    if (span > k_max_buckets_per_cell) {
      continue;
    }
    for (int y = y0; y <= y1; ++y) {
      for (int x = x0; x <= x1; ++x) {
        const auto bucket = static_cast<std::size_t>(y) * static_cast<std::size_t>(nx) +
                            static_cast<std::size_t>(x);
        grid.items[static_cast<std::size_t>(cursor[bucket]++)] = static_cast<int>(index);
      }
    }
  }
  grid_ = std::move(grid);
}

void DamageField::cells_near(const Vec2& point, std::vector<int>& out) const {
  out.clear();
  if (grid_.cell_count != cells.size()) {
    build_index();
  }
  if (grid_.empty()) {
    // Degenerate field (a point / a line): fall back to the exact linear scan.
    out.reserve(cells.size());
    for (std::size_t i = 0; i < cells.size(); ++i) {
      out.push_back(static_cast<int>(i));
    }
    return;
  }
  if (point.x < grid_.min.x || point.x > grid_.max.x || point.y < grid_.min.y ||
      point.y > grid_.max.y) {
    // Outside the field: only the `large` cells can still contain it.
    out = grid_.large;
    return;
  }
  const float cell_w = (grid_.max.x - grid_.min.x) / static_cast<float>(grid_.nx);
  const float cell_h = (grid_.max.y - grid_.min.y) / static_cast<float>(grid_.ny);
  const int x = std::clamp(static_cast<int>((point.x - grid_.min.x) / cell_w), 0, grid_.nx - 1);
  const int y = std::clamp(static_cast<int>((point.y - grid_.min.y) / cell_h), 0, grid_.ny - 1);
  const auto bucket = static_cast<std::size_t>(y) * static_cast<std::size_t>(grid_.nx) +
                      static_cast<std::size_t>(x);
  out.reserve(static_cast<std::size_t>(grid_.start[bucket + 1] - grid_.start[bucket]) +
              grid_.large.size());
  for (int i = grid_.start[bucket]; i < grid_.start[bucket + 1]; ++i) {
    out.push_back(grid_.items[static_cast<std::size_t>(i)]);
  }
  out.insert(out.end(), grid_.large.begin(), grid_.large.end());
}

auto DamageField::value_at(const Vec2& point) const noexcept -> float {
  std::vector<int> scratch;
  return value_at(point, scratch);
}

auto DamageField::value_at(const Vec2& point, std::vector<int>& scratch) const noexcept -> float {
  cells_near(point, scratch);
  float best = 0.0F;
  for (const int index : scratch) {
    const Cell& cell = cells[static_cast<std::size_t>(index)];
    if (polygon_contains(cell.polygon, point)) {
      best = std::max(best, cell.value);
    }
  }
  return best;
}

auto DamageField::base_value_at(const Vec2& point) const noexcept -> float {
  std::vector<int> scratch;
  return base_value_at(point, scratch);
}

auto DamageField::base_value_at(const Vec2& point, std::vector<int>& scratch) const noexcept
    -> float {
  cells_near(point, scratch);
  float best = 0.0F;
  for (const int index : scratch) {
    const Cell& cell = cells[static_cast<std::size_t>(index)];
    if (polygon_contains(cell.polygon, point)) {
      best = std::max(best, cell.base_value);
    }
  }
  return best;
}

auto DamageField::hit_probability_exact(const Vec2& aim, float sigma_u,
                                        float sigma_v) const noexcept -> float {
  if (cells.empty()) {
    return 0.0F;
  }
  if (sigma_u <= k_epsilon && sigma_v <= k_epsilon) {
    return value_at(aim) > 0.0F ? 1.0F : 0.0F;
  }
  // The cells are disjoint by construction, so the probability the shot lands in the
  // target is the sum of each cell's Gaussian mass (exact, no sampling). Every cell is
  // accounted for — there is no distance cutoff.
  double total = 0.0;
  Polygon scaled;
  for (const Cell& cell : cells) {
    scaled.points.clear();
    scaled.points.reserve(cell.polygon.points.size());
    for (const Vec2& p : cell.polygon.points) {
      scaled.points.push_back(Vec2{.x = (p.x - aim.x) / sigma_u, .y = (p.y - aim.y) / sigma_v});
    }
    if (!within_mass_cutoff(scaled)) {
      continue;  // below the Gaussian tail bound (see `k_mass_cutoff`)
    }
    total += polygon_normal_mass(scaled, nullptr, nullptr);
  }
  return static_cast<float>(std::clamp(total, 0.0, 1.0));
}

auto DamageField::expected_exact(const Vec2& aim, float sigma_u, float sigma_v) const noexcept
    -> float {
  if (cells.empty()) {
    return 0.0F;
  }
  if (sigma_u <= k_epsilon && sigma_v <= k_epsilon) {
    return value_at(aim);
  }
  double total = 0.0;
  Polygon scaled;
  for (const Cell& cell : cells) {
    scaled.points.clear();
    scaled.points.reserve(cell.polygon.points.size());
    for (const Vec2& p : cell.polygon.points) {
      scaled.points.push_back(Vec2{.x = (p.x - aim.x) / sigma_u, .y = (p.y - aim.y) / sigma_v});
    }
    if (!within_mass_cutoff(scaled)) {
      continue;  // below the Gaussian tail bound (see `k_mass_cutoff`)
    }
    total += static_cast<double>(cell.value) * polygon_normal_mass(scaled, nullptr, nullptr);
  }
  return static_cast<float>(total);
}

auto DamageField::hit_probability(const Vec2& aim, float sigma) const noexcept -> float {
  if (sigma <= k_epsilon) {
    return value_at(aim) > 0.0F ? 1.0F : 0.0F;
  }
  // The cone is isotropic on the sphere; on the chart its yaw half-width is
  // `sigma * yaw_scale(pitch)` (the yaw/pitch metric). Exact Gaussian mass.
  return hit_probability_exact(aim, sigma * yaw_scale(aim.y), sigma);
}

auto DamageField::argmax_hit_probability(float sigma, int grid) const -> Vec2 {
  (void)grid;  // no grid: the optimum is found exactly via the Gaussian mass + search
  if (cells.empty()) {
    return Vec2{};
  }
  if (sigma <= k_epsilon) {
    return argmax_damage();
  }
  return mass_ascent(cells, sigma, false, [&](const Vec2& aim) -> double {
    return static_cast<double>(hit_probability(aim, sigma));
  });
}

auto DamageField::value_at_pellets(const Vec2& aim, std::span<const Vec2> offsets) const noexcept
    -> float {
  if (offsets.empty()) {
    return value_at(aim);
  }
  float sum = 0.0F;
  for (const Vec2& offset : offsets) {
    sum += value_at(aim + offset);
  }
  return sum;
}

auto DamageField::argmax_pellets(std::span<const Vec2> offsets) const -> Vec2 {
  if (cells.empty()) {
    return Vec2{};
  }
  // One pellet (or an all-zero offset set) is exactly the damage optimum.
  bool trivial = true;
  for (const Vec2& offset : offsets) {
    if (offset.x != 0.0F || offset.y != 0.0F) {
      trivial = false;
      break;
    }
  }
  if (trivial) {
    return argmax_damage();
  }

  // Candidate aims: each cell's own optima and vertices, and each of those shifted by
  // `-offset_k` so a candidate sits on a cell boundary reached by a pellet. The
  // pellet sum is constant on each region these points bound, so the maximum is one
  // of them (the design's "the optimum lies in a finite candidate set").
  std::vector<Vec2> candidates;
  candidates.reserve(cells.size() * (2 + 8) * (offsets.size() + 1));
  candidates.push_back(Vec2{});
  for (const Cell& cell : cells) {
    std::vector<Vec2> anchors;
    anchors.push_back(nearest_point(cell.polygon));
    anchors.push_back(chebyshev_center(cell.polygon));
    for (const Vec2& point : cell.polygon.points) {
      anchors.push_back(point);
    }
    for (const Vec2& anchor : anchors) {
      candidates.push_back(anchor);
      for (const Vec2& offset : offsets) {
        candidates.push_back(Vec2{.x = anchor.x - offset.x, .y = anchor.y - offset.y});
      }
    }
  }

  Vec2 best{};
  float best_value = -1.0F;
  for (const Vec2& candidate : candidates) {
    const float value = value_at_pellets(candidate, offsets);
    if (value > best_value + k_epsilon) {
      best_value = value;
      best = candidate;
    }
  }
  return best;
}

auto DamageField::best_cell() const noexcept -> const Cell* {
  const Cell* best = nullptr;
  for (const Cell& cell : cells) {
    if (best == nullptr || cell.value > best->value) {
      best = &cell;
    }
  }
  return best;
}

auto DamageField::argmax_damage() const -> Vec2 {
  if (cells.empty()) {
    return Vec2{};
  }
  // A **global** search over the candidate points (not "the best cell's own optimum"):
  // maximize the damage, then the local margin so a tie between a solid chest fragment
  // and a thin head sliver prefers the robust point. The value is constant across a
  // cell, so this returns the best point of the field as a whole. Candidates come only
  // from the cells that can reach the optimal value (`field_candidates`).
  const float best_value = best_cell() != nullptr ? best_cell()->value : 0.0F;
  const std::vector<Vec2> candidates = field_candidates(cells, best_value);
  Vec2 best = candidates.front();
  float best_seen = -1.0F;
  float best_margin = -1.0F;
  std::vector<int> scratch;
  for (const Vec2& candidate : candidates) {
    const float value = value_at(candidate, scratch);
    if (value + k_field_epsilon < best_seen) {
      continue;  // cannot beat the best value, so its margin is irrelevant
    }
    const float margin = field_margin(*this, candidate, scratch);
    const bool better =
        value > best_seen + k_field_epsilon ||
        (std::fabs(value - best_seen) <= k_field_epsilon && margin > best_margin + k_field_epsilon);
    if (better) {
      best = candidate;
      best_seen = value;
      best_margin = margin;
    }
  }
  return best;
}

auto DamageField::argmax_hitchance() const -> Vec2 {
  if (cells.empty()) {
    return Vec2{};
  }
  // Global: the largest margin across the whole field, but only among points that carry
  // a **comparable damage** (`>= 0.5 · best`) so a fat low-value limb cannot out-margin
  // the head (`docs/ragebot.md` §7). This is the best point of the field as a whole, not
  // of one fragment.
  const float best_value = best_cell() != nullptr ? best_cell()->value : 0.0F;
  const float floor_value = best_value * 0.5F;
  const std::vector<Vec2> candidates = field_candidates(cells, floor_value);
  Vec2 best = candidates.front();
  float best_margin = -1.0F;
  float shared_value = -1.0F;
  std::vector<int> scratch;
  for (const Vec2& candidate : candidates) {
    const float value = value_at(candidate, scratch);
    if (value < floor_value) {
      continue;
    }
    const float margin = field_margin(*this, candidate, scratch);
    const bool better = margin > best_margin + k_field_epsilon ||
                        (std::fabs(margin - best_margin) <= k_field_epsilon &&
                         value > shared_value + k_field_epsilon);
    if (better) {
      best = candidate;
      best_margin = margin;
      shared_value = value;
    }
  }
  return best;
}

auto DamageField::bounds() const noexcept -> Polygon {
  Polygon box;
  if (cells.empty()) {
    return box;
  }
  float min_x = cells.front().polygon.points.front().x;
  float max_x = min_x;
  float min_y = cells.front().polygon.points.front().y;
  float max_y = min_y;
  for (const Cell& cell : cells) {
    for (const Vec2& p : cell.polygon.points) {
      min_x = std::min(min_x, p.x);
      max_x = std::max(max_x, p.x);
      min_y = std::min(min_y, p.y);
      max_y = std::max(max_y, p.y);
    }
  }
  box.points = {Vec2{.x = min_x, .y = min_y}, Vec2{.x = max_x, .y = min_y},
                Vec2{.x = max_x, .y = max_y}, Vec2{.x = min_x, .y = max_y}};
  return box;
}

auto DamageField::argmax_expected(float sigma, int grid) const -> Vec2 {
  (void)grid;  // no grid: exact Gaussian-weighted value + compass search
  if (cells.empty()) {
    return Vec2{};
  }
  if (sigma <= k_epsilon) {
    return argmax_damage();
  }
  // The pose kernel is isotropic in world angles; on the chart its yaw spread is
  // `sigma * yaw_scale(pitch)` (the same metric as `hit_probability`).
  return mass_ascent(cells, sigma, true, [&](const Vec2& aim) -> double {
    return static_cast<double>(expected_exact(aim, sigma * yaw_scale(aim.y), sigma));
  });
}

}  // namespace cs2::aim
