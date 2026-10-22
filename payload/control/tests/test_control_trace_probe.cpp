// The trace probe's host test (`docs/re/bullet-trace.md`).
//
// The probe's answer is only worth reading when four things hold: the probe's ray
// and the solver's ray are the **same** 3-D segment; the results container has the
// layout the **engine** reads; the difference logic turns the two answers into the
// right verdict; and the probe's own wiring uses those rules. The first three are
// pure, so they are pinned here; the last one is a **source** check (the service is
// not reachable from a host test — it needs the game).
//
// Every check below must be able to **fail**: a rule that is written down but not
// pinned is a rule nobody has to keep.

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "cs2/control/occlusion_query.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    ++failures;
    std::printf("FAIL: %s\n", what);
  }
}

[[nodiscard]] auto near(float a, float b, float tolerance = 1.0e-3F) noexcept -> bool {
  return std::fabs(a - b) <= tolerance;
}

[[nodiscard]] auto near(const cs2::aim::Vec3& a, const cs2::aim::Vec3& b,
                        float tolerance = 1.0e-3F) noexcept -> bool {
  return near(a.x, b.x, tolerance) && near(a.y, b.y, tolerance) && near(a.z, b.z, tolerance);
}

[[nodiscard]] auto vec_length(const cs2::aim::Vec3& v) noexcept -> float {
  return cs2::game::length(v);
}

constexpr float k_deg_to_rad = 0.01745329251994329577F;

// The chart's own construction of a ray: the absolute world yaw/pitch of the
// chart (`docs/ragebot.md`) — the Source pitch is positive downwards.
[[nodiscard]] auto chart_point(float yaw_degrees, float pitch_degrees) noexcept -> cs2::aim::Vec2 {
  return cs2::aim::Vec2{.x = yaw_degrees * k_deg_to_rad, .y = -pitch_degrees * k_deg_to_rad};
}

// The source of a file, or an empty string when it cannot be read.
[[nodiscard]] auto read_source(const std::filesystem::path& path) -> std::string {
  std::ifstream file(path);
  if (!file.good()) {
    return {};
  }
  return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

// The text between two markers (inclusive of the first, exclusive of the second).
// A missing marker yields an empty string, so a check on the region fails loudly
// instead of silently looking at the whole file.
[[nodiscard]] auto region_of(const std::string& source, const char* from, const char* to)
    -> std::string {
  const std::size_t begin = source.find(from);
  if (begin == std::string::npos) {
    return {};
  }
  const std::size_t end = to == nullptr ? std::string::npos : source.find(to, begin);
  if (end == std::string::npos) {
    return source.substr(begin);
  }
  return source.substr(begin, end - begin);
}

// The region's text with the whitespace collapsed, so a check does not depend on
// how the line was wrapped.
[[nodiscard]] auto flattened(const std::string& text) -> std::string {
  std::string flat;
  bool space = false;
  for (const char c : text) {
    if (c == '\n' || c == '\r' || c == '\t' || c == ' ') {
      space = true;
      continue;
    }
    if (space && !flat.empty()) {
      flat.push_back(' ');
    }
    space = false;
    flat.push_back(c);
  }
  return flat;
}

// --- 1. the ray construction ---------------------------------------------------
//
// **The test that keeps the comparison honest:** the probe's ray and the solver's
// ray must be the same segment for the same yaw/pitch.
void test_ray_construction() {
  const cs2::aim::Basis basis = cs2::aim::Basis::world();
  const cs2::aim::Vec3 eye{100.0F, 200.0F, 64.0F};
  const float yaw = 37.0F;
  const float pitch = -12.5F;
  const float depth = 1500.0F;

  const auto rays = cs2::control::build_trace_fan(eye, basis, yaw, pitch, 0.0F, 1, depth);
  expect(rays.size() == 1U, "a fan of one ray has one ray");
  if (rays.empty()) {
    return;
  }
  const cs2::control::TraceRay& ray = rays.front();
  const cs2::aim::Vec3 expected = basis.unproject(chart_point(yaw, pitch));
  expect(near(ray.direction, expected), "the probe's ray is the chart's ray");
  expect(near(ray.start, eye), "the probe's ray starts at the eye");
  expect(near(ray.end, eye + expected * depth), "the probe's ray ends at the eye plus the depth");
  expect(near(vec_length(ray.direction), 1.0F), "the probe's direction is a unit vector");
  expect(near(ray.depth, depth), "the probe's ray carries its depth");

  // The round trip through the chart: the ray's chart point is the angles it was
  // built from — so the operator can check the ray against the view.
  const std::optional<cs2::aim::Vec2> projected = basis.project(ray.direction);
  expect(projected.has_value(), "the probe's direction projects into the chart");
  if (projected.has_value()) {
    const cs2::aim::Vec2 point = chart_point(yaw, pitch);
    expect(near(projected->x, point.x) && near(projected->y, point.y),
           "the probe's ray is the chart point it was built from");
  }

  // The **3-D** half of the claim: the probe's ray and the solver's ray hit the
  // same solid at the same distance. The solver's answer comes from
  // `OccluderCell::ray_span` — the probe must not re-implement it.
  //
  // The ray is the chart's ray for yaw = 0, pitch = 0 — straight along +X — and
  // the box sits on it: x = 950..1050, y = 150..250, z = 14..114 (world space).
  const std::vector<cs2::control::TraceRay> axis_rays =
      cs2::control::build_trace_fan(eye, basis, 0.0F, 0.0F, 0.0F, 1, depth);
  expect(axis_rays.size() == 1U, "the axis-aligned fan has one ray");
  if (axis_rays.empty()) {
    return;
  }
  const cs2::aim::Vec3 axis_direction = axis_rays.front().direction;
  const cs2::aim::Vec3 box_centre{eye.x + 1000.0F, eye.y, eye.z};
  std::vector<cs2::aim::Vec3> vertices;
  for (int i = 0; i < 8; ++i) {
    vertices.push_back(cs2::aim::Vec3{.x = box_centre.x + ((i & 1) != 0 ? 50.0F : -50.0F),
                                      .y = box_centre.y + ((i & 2) != 0 ? 50.0F : -50.0F),
                                      .z = box_centre.z + ((i & 4) != 0 ? 50.0F : -50.0F)});
  }
  const std::vector<cs2::aim::OccluderPlane> planes = cs2::aim::convex_hull_3d(vertices);
  expect(!planes.empty(), "the test box has half-spaces");
  const std::optional<cs2::aim::OccluderCell> cell =
      cs2::aim::project_occluder(eye, basis, vertices, planes, 1.0F);
  expect(cell.has_value(), "the test box projects");
  if (cell.has_value()) {
    const std::optional<cs2::aim::RaySpan> span = cell->ray_span(eye, axis_direction);
    expect(span.has_value(), "the probe's ray hits the test box");
    if (span.has_value()) {
      expect(near(span->entry, 950.0F, 1.0F), "the solver's ray enters the box at 950 units");
      expect(near(span->exit, 1050.0F, 1.0F), "the solver's ray leaves the box at 1050 units");
    }
  }
}

// --- 2. the fan's order --------------------------------------------------------
void test_fan_order() {
  const cs2::aim::Basis basis = cs2::aim::Basis::world();
  const cs2::aim::Vec3 eye{0.0F, 0.0F, 0.0F};
  const float yaw = 10.0F;
  const float pitch = -5.0F;
  const float spread = 4.0F;
  const int count = 5;

  const auto rays = cs2::control::build_trace_fan(eye, basis, yaw, pitch, spread, count, 1000.0F);
  expect(rays.size() == static_cast<std::size_t>(count), "the fan has the requested rays");

  // Ray 0 is the view ray itself.
  expect(near(rays.front().direction, basis.unproject(chart_point(yaw, pitch))),
         "the fan's first ray is the view ray");

  // The fan is **deterministic**: two runs compare.
  const auto again = cs2::control::build_trace_fan(eye, basis, yaw, pitch, spread, count, 1000.0F);
  bool identical = again.size() == rays.size();
  for (std::size_t i = 0; identical && i < rays.size(); ++i) {
    identical = near(again[i].direction, rays[i].direction) && near(again[i].end, rays[i].end);
  }
  expect(identical, "the fan is deterministic");

  // Every ray is a unit direction inside the spread, and no two rays are the same.
  const cs2::aim::Vec2 centre = chart_point(yaw, pitch);
  bool inside = true;
  bool distinct = true;
  for (std::size_t i = 0; i < rays.size(); ++i) {
    if (!near(vec_length(rays[i].direction), 1.0F)) {
      inside = false;
    }
    const std::optional<cs2::aim::Vec2> point = basis.project(rays[i].direction);
    if (!point.has_value()) {
      inside = false;
      continue;
    }
    const float du = point->x - centre.x;
    const float dv = point->y - centre.y;
    if (std::sqrt(du * du + dv * dv) > spread * k_deg_to_rad + 1.0e-3F) {
      inside = false;
    }
    for (std::size_t j = 0; j < i; ++j) {
      if (near(rays[i].direction, rays[j].direction, 1.0e-4F)) {
        distinct = false;
      }
    }
  }
  expect(inside, "every fan ray is a unit direction inside the spread");
  expect(distinct, "no two fan rays are the same");

  // The **documented order**: ray `i` (i >= 1) is at the radius
  // `spread * sqrt(i / count)` and the angle `i * 137.5077°` around the view ray.
  // The order is part of the answer's contract (two runs compare), so it is
  // spelled out here rather than left to the implementation.
  constexpr float k_golden_angle_degrees = 137.50776405003785F;
  bool spiral = true;
  for (std::size_t i = 1; i < rays.size(); ++i) {
    const float t = static_cast<float>(i) / static_cast<float>(count);
    const float radius = spread * k_deg_to_rad * std::sqrt(t);
    const float theta = static_cast<float>(i) * k_golden_angle_degrees * k_deg_to_rad;
    const std::optional<cs2::aim::Vec2> point = basis.project(rays[i].direction);
    if (!point.has_value()) {
      spiral = false;
      break;
    }
    const float du = point->x - centre.x;
    const float dv = point->y - centre.y;
    if (!near(du, radius * std::cos(theta), 1.0e-3F) ||
        !near(dv, radius * std::sin(theta), 1.0e-3F)) {
      spiral = false;
      break;
    }
  }
  expect(spiral, "the fan's ray i is at the documented spiral point");

  // A fan of one ray is the view ray, whatever the spread.
  const auto single = cs2::control::build_trace_fan(eye, basis, yaw, pitch, spread, 1, 1000.0F);
  expect(single.size() == 1U && near(single.front().direction, rays.front().direction),
         "a fan of one ray is the view ray");
}

// --- 3. the interval record reader --------------------------------------------
void test_interval_reader() {
  std::uint8_t record[cs2::control::TRACE_INTERVAL_SIZE] = {};
  // The **literal** offsets: the record's layout is a property of the engine, not
  // of the constants, so the test places the values at `+0x0C` and `+0x32` and
  // reads them back. (A test that used the constants would pass with a wrong
  // constant — the reader would simply agree with itself.)
  constexpr std::size_t k_fraction_offset = 0x0C;
  constexpr std::size_t k_enter_exit_offset = 0x32;
  const float fraction = 0.25F;
  std::memcpy(record + k_fraction_offset, &fraction, sizeof(float));
  record[k_enter_exit_offset] = 1U;
  record[0] = 0xABU;  // a byte outside the pinned fields: the raw copy must keep it

  const cs2::control::TraceInterval interval = cs2::control::read_trace_interval(record, 800.0F);
  expect(near(interval.fraction, 0.25F), "the fraction is read from +0x0C");
  expect(near(interval.distance, 200.0F), "the distance is the fraction times the ray's length");
  expect(interval.enter_exit == 1U, "the enter/exit byte is read from +0x32");
  expect(interval.raw[0] == 0xABU, "the raw bytes keep the fields that are not mapped");
  expect(std::memcmp(interval.raw, record, cs2::control::TRACE_INTERVAL_SIZE) == 0,
         "the whole record is copied");

  // A "leave" record reads the other byte value.
  record[k_enter_exit_offset] = 0U;
  const cs2::control::TraceInterval leave = cs2::control::read_trace_interval(record, 800.0F);
  expect(leave.enter_exit == 0U, "a leave record reads zero at +0x32");

  // A missing record reads as empty (never a fault).
  const cs2::control::TraceInterval missing = cs2::control::read_trace_interval(nullptr, 800.0F);
  expect(missing.fraction == 0.0F && missing.distance == 0.0F && missing.enter_exit == 0U,
         "a missing record reads as empty");
}

// --- 4. the results container --------------------------------------------------
//
// The trace's results container is **not** the overlap query's `CUtlVector`: the
// live build's two writers read the record pointer at `+0x08` and the capacity at
// `+0x10`. This test pins that layout with the **literal** offsets, so a container
// with the overlap's layout (the record pointer at `+0x10`) cannot pass.
void test_result_vector_layout() {
  struct LiveLayout {
    std::int32_t count;
    std::int32_t reserved;
    void* data;
    std::int32_t capacity;
    std::int32_t flags;
  };
  static_assert(sizeof(LiveLayout) == 0x18, "the live build's header is 0x18 bytes");
  static_assert(offsetof(LiveLayout, count) == 0x00, "the count is at +0x00");
  static_assert(offsetof(LiveLayout, data) == 0x08, "the record pointer is at +0x08");
  static_assert(offsetof(LiveLayout, capacity) == 0x10, "the capacity is at +0x10");
  static_assert(offsetof(LiveLayout, flags) == 0x14, "the flags are at +0x14");

  // The payload's own type must be the same layout — and, above all, **not** the
  // overlap query's (whose record pointer is at +0x10).
  static_assert(sizeof(cs2::control::TraceResultVector) == 0x18,
                "the probe's header is 0x18 bytes");
  static_assert(offsetof(cs2::control::TraceResultVector, count) == 0x00, "the count is at +0x00");
  static_assert(offsetof(cs2::control::TraceResultVector, data) == 0x08,
                "the record pointer is at +0x08");
  static_assert(offsetof(cs2::control::TraceResultVector, capacity) == 0x10,
                "the capacity is at +0x10");
  static_assert(offsetof(cs2::control::TraceResultVector, flags) == 0x14, "the flags are at +0x14");
  static_assert(offsetof(cs2::control::TraceResultVector, data) != 0x10,
                "the record pointer is NOT at the overlap vector's +0x10");

  // The external-buffer bit is the engine's **bit 30** for this container. The
  // overlap vector's `0x80000000` belongs to a different type and means nothing
  // here: with it the engine would grow the array (and reach its allocator).
  constexpr std::int32_t k_live_external_bit = 0x40000000;
  constexpr std::int32_t k_overlap_external_bit = 0x80000000;
  expect(cs2::control::TRACE_VECTOR_EXTERNAL_BUFFER == k_live_external_bit,
         "the external-buffer bit is the live build's bit 30");
  expect(cs2::control::TRACE_VECTOR_EXTERNAL_BUFFER != k_overlap_external_bit,
         "the external-buffer bit is NOT the overlap vector's bit 31");
  expect((cs2::control::TRACE_VECTOR_EXTERNAL_BUFFER & k_live_external_bit) != 0,
         "the external-buffer bit sets bit 30");

  // The engine's own growth rule, reproduced: the writer grows the array only when
  // `count == capacity` **and** bit 30 of the flags is clear. With the bit set the
  // engine never grows — so it never reaches its allocator (the payload's thread
  // has no tier0 TLS, docs/handoff.md).
  const auto appends = [](cs2::control::TraceResultVector& vector, int* allocations) -> void {
    if (vector.count == vector.capacity && (vector.flags & k_live_external_bit) == 0) {
      ++(*allocations);
      vector.capacity += 1;  // the engine's own growth: the capacity plus one
    }
    ++vector.count;
  };
  cs2::control::TraceResultVector external;
  external.capacity = 4;
  external.flags =
      cs2::control::TRACE_VECTOR_EXTERNAL_BUFFER | cs2::control::TRACE_VECTOR_NOT_OWNED;
  int allocations = 0;
  for (int i = 0; i < 32; ++i) {
    appends(external, &allocations);
  }
  expect(allocations == 0, "an external buffer never makes the engine allocate");
  expect(external.count == 32, "the external buffer still takes every record");
  // ...which is why the buffer carries the slack: the engine appends **at**
  // `data[count]`, so it writes past the advertised capacity.
  expect(external.count > external.capacity,
         "an external buffer lets the engine write past the advertised capacity");

  cs2::control::TraceResultVector growing;
  growing.capacity = 4;
  growing.flags = 0;
  allocations = 0;
  for (int i = 0; i < 32; ++i) {
    appends(growing, &allocations);
  }
  expect(allocations > 0, "without the bit the engine grows (and allocates) at the capacity");
}

// --- 5. the difference logic ---------------------------------------------------
void test_difference() {
  const auto interval_at = [](float distance, std::uint8_t enter_exit) {
    cs2::control::TraceInterval interval;
    interval.fraction = distance;
    interval.distance = distance;
    interval.enter_exit = enter_exit;
    return interval;
  };
  const auto hit_at = [](float entry, float exit) {
    cs2::control::TraceOccluderHit hit;
    hit.entry = entry;
    hit.exit = exit;
    return hit;
  };
  constexpr float k_tolerance = 2.0F;

  // Nothing on either side: the two answers agree.
  {
    const cs2::control::TraceDiff diff = cs2::control::diff_trace({}, {}, k_tolerance, true);
    expect(diff.verdict == cs2::control::TraceVerdict::agrees,
           "no intervals and no occluders agree");
    expect(diff.engine_only.empty() && diff.ours_only.empty(), "the difference is empty");
  }

  // The engine found a solid we have nothing for.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const cs2::control::TraceDiff diff = cs2::control::diff_trace(intervals, {}, k_tolerance, true);
    expect(diff.verdict == cs2::control::TraceVerdict::engine_only,
           "an interval with no occluder is engine_only");
    expect(diff.engine_only.size() == 1U, "the unmatched span is reported");
    expect(diff.ours_only.empty(), "nothing is ours only");
    if (!diff.engine_only.empty()) {
      expect(near(diff.engine_only.front().entry, 100.0F) &&
                 near(diff.engine_only.front().exit, 120.0F),
             "the unmatched span carries the engine's distances");
    }
  }

  // We have a solid the engine did not report: the engine's answer is empty.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {};
    const std::vector<cs2::control::TraceOccluderHit> ours = {hit_at(400.0F, 430.0F)};
    const cs2::control::TraceDiff diff =
        cs2::control::diff_trace(intervals, ours, k_tolerance, true);
    expect(diff.verdict == cs2::control::TraceVerdict::ours_only,
           "an occluder with no interval is ours_only");
    expect(diff.ours_only.size() == 1U, "the unmatched occluder is reported");
    expect(diff.engine_only.empty(), "nothing is engine only");
  }

  // Both found the same solid: they agree.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> ours = {hit_at(101.0F, 119.0F)};
    const cs2::control::TraceDiff diff =
        cs2::control::diff_trace(intervals, ours, k_tolerance, true);
    expect(diff.verdict == cs2::control::TraceVerdict::agrees,
           "the same solid on both sides agrees");
    expect(diff.engine_only.empty() && diff.ours_only.empty(), "the difference is empty");
  }

  // The same solid at a **different depth**: our hull is thicker than the engine's
  // span, so it covers it — they are the same solid.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(110.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> ours = {hit_at(100.0F, 130.0F)};
    const cs2::control::TraceDiff diff =
        cs2::control::diff_trace(intervals, ours, k_tolerance, true);
    expect(diff.verdict == cs2::control::TraceVerdict::agrees,
           "the same solid at a different depth agrees");
  }

  // Two different solids: the difference is in both directions.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> ours = {hit_at(400.0F, 430.0F)};
    const cs2::control::TraceDiff diff =
        cs2::control::diff_trace(intervals, ours, k_tolerance, true);
    expect(diff.verdict == cs2::control::TraceVerdict::both, "two different solids are 'both'");
    expect(diff.engine_only.size() == 1U && diff.ours_only.size() == 1U,
           "both differences are reported");
  }

  // The tolerance compares the two spans' **positions**, it does not widen the
  // overlap test: an occluder that starts a unit after the engine's span is a
  // different solid, and one that ends a unit before it is too.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> close = {hit_at(101.0F, 119.0F)};
    const std::vector<cs2::control::TraceOccluderHit> far = {hit_at(130.0F, 150.0F)};
    expect(cs2::control::diff_trace(intervals, close, k_tolerance, true).verdict ==
               cs2::control::TraceVerdict::agrees,
           "an occluder within the tolerance is the same solid");
    expect(cs2::control::diff_trace(intervals, far, k_tolerance, true).verdict ==
               cs2::control::TraceVerdict::both,
           "an occluder beyond the tolerance is a different solid");
  }

  // **The real occlusion must never be reported as "agrees".** The engine found a
  // wall from 100 to 150 units; our set holds an unrelated prop from 98 to 99.5 —
  // a solid that does not cover the wall at all. The wall must stay in
  // `engine_only`: it must not be answered for by the prop.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(150.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> prop = {hit_at(98.0F, 99.5F)};
    const cs2::control::TraceDiff diff =
        cs2::control::diff_trace(intervals, prop, k_tolerance, true);
    expect(diff.verdict != cs2::control::TraceVerdict::agrees,
           "a real occlusion is never 'agrees'");
    expect(diff.engine_only.size() == 1U, "the wall is reported as the engine's alone");
    if (!diff.engine_only.empty()) {
      expect(near(diff.engine_only.front().entry, 100.0F) &&
                 near(diff.engine_only.front().exit, 150.0F),
             "the engine's wall is the span that is reported");
    }
    expect(diff.ours_only.size() == 1U, "the prop is reported as ours alone");
    // The same prop, three units further away: the same answer.
    const std::vector<cs2::control::TraceOccluderHit> further = {hit_at(95.0F, 96.5F)};
    expect(cs2::control::diff_trace(intervals, further, k_tolerance, true).verdict !=
               cs2::control::TraceVerdict::agrees,
           "a prop three units away does not hide the wall either");
  }

  // A prop that covers only the **first half** of the wall is not the wall: the
  // wall must stay `engine_only` (our set does not have it).
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(150.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> half = {hit_at(100.0F, 120.0F)};
    const cs2::control::TraceDiff diff =
        cs2::control::diff_trace(intervals, half, k_tolerance, true);
    expect(diff.verdict != cs2::control::TraceVerdict::agrees,
           "a solid that covers only part of the wall does not answer for it");
    expect(diff.engine_only.size() == 1U, "the wall is still the engine's alone");
    expect(diff.ours_only.size() == 1U, "the half-wall is ours alone");
  }

  // The engine's answer could **not** be read: the verdict is "unknown", never
  // "agrees" — the defect the probe exists to expose must not be reported
  // inverted.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {};
    const std::vector<cs2::control::TraceOccluderHit> ours = {hit_at(100.0F, 120.0F)};
    const cs2::control::TraceDiff diff =
        cs2::control::diff_trace(intervals, ours, k_tolerance, false);
    expect(diff.verdict == cs2::control::TraceVerdict::unknown,
           "a dropped engine answer is 'unknown', not 'agrees'");
    expect(!diff.engine_answer_usable, "the difference says the engine's answer was unusable");
    expect(diff.engine_only.empty() && diff.ours_only.empty(),
           "a dropped engine answer has no difference to report");
  }

  // The pairing: the records are paired into spans (an enter opens a span, the
  // next leave closes it), so a ray that crosses one solid gives one span.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const std::vector<cs2::control::TraceSpan> spans = cs2::control::pair_trace_spans(intervals);
    expect(spans.size() == 1U, "an enter/leave pair is one span");
    if (!spans.empty()) {
      expect(spans.front().enter_record == 0 && spans.front().exit_record == 1,
             "the span names the two records it was built from");
    }
    // A leave without an enter is not a span.
    const std::vector<cs2::control::TraceInterval> orphan = {interval_at(50.0F, 0U)};
    expect(cs2::control::pair_trace_spans(orphan).empty(),
           "a leave without an enter is not a span");
    // A ray that ends inside a solid still gives a span.
    const std::vector<cs2::control::TraceInterval> unfinished = {interval_at(50.0F, 1U)};
    expect(cs2::control::pair_trace_spans(unfinished).size() == 1U,
           "an unclosed enter is still a span");
  }
}

// --- 6. the bounds --------------------------------------------------------------
void test_bounds() {
  // The **documented** maxima (the command's error message and the docs quote
  // them): 8 rays in a fan, 64 published intervals per ray.
  expect(cs2::control::TRACE_MAX_RAYS == 8, "the probe's maximum number of rays is 8");
  expect(cs2::control::TRACE_MAX_INTERVALS == 64, "the probe's published interval maximum is 64");
  expect(cs2::control::trace_fan_size_ok(1), "one ray is in range");
  expect(cs2::control::trace_fan_size_ok(cs2::control::TRACE_MAX_RAYS),
         "the maximum number of rays is in range");
  expect(!cs2::control::trace_fan_size_ok(0), "no ray is out of range");
  expect(!cs2::control::trace_fan_size_ok(cs2::control::TRACE_MAX_RAYS + 1),
         "one ray over the maximum is out of range");
  expect(!cs2::control::trace_fan_size_ok(-1), "a negative count is out of range");

  // The fan builder never returns more than the maximum, whatever it is asked for.
  const auto rays = cs2::control::build_trace_fan(cs2::aim::Vec3{}, cs2::aim::Basis::world(), 0.0F,
                                                  0.0F, 1.0F, 1000, 100.0F);
  expect(rays.size() <= static_cast<std::size_t>(cs2::control::TRACE_MAX_RAYS),
         "the fan never exceeds the maximum");

  // The interval budget: the probe refuses to publish more intervals than its
  // maximum and says so.
  const cs2::control::TraceIntervalBudget small = cs2::control::trace_interval_budget(3);
  expect(small.published == 3 && !small.overflow, "a small count is published as it is");
  const cs2::control::TraceIntervalBudget exact =
      cs2::control::trace_interval_budget(cs2::control::TRACE_MAX_INTERVALS);
  expect(exact.published == cs2::control::TRACE_MAX_INTERVALS && !exact.overflow,
         "the maximum is published as it is");
  const cs2::control::TraceIntervalBudget over =
      cs2::control::trace_interval_budget(cs2::control::TRACE_MAX_INTERVALS + 10);
  expect(over.published == cs2::control::TRACE_MAX_INTERVALS,
         "the budget publishes at most the maximum");
  expect(over.overflow, "the budget reports the overflow");
  expect(cs2::control::TRACE_INTERVAL_CAPACITY >= cs2::control::TRACE_MAX_INTERVALS,
         "the results buffer is at least as large as the published maximum");
  // The buffer carries the slack the engine's "append at `data[count]`" needs.
  expect(cs2::control::TRACE_INTERVAL_SLACK > 0, "the results buffer carries the slack");
  // The probe's own work is bounded: the projections are capped too.
  expect(cs2::control::TRACE_MAX_OCCLUDERS > 0, "the probe's projection cap is set");
}

// --- 7. the enumeration's target box ------------------------------------------
void test_target_box() {
  const cs2::aim::Basis basis = cs2::aim::Basis::world();
  const cs2::aim::Vec3 eye{0.0F, 0.0F, 0.0F};
  constexpr float k_pad = cs2::control::TRACE_TARGET_PAD;

  // One ray: the far end is a **point**, and an unpadded box would be a degenerate
  // hull — the eye→box pyramid would have no face planes, and the pyramid query
  // keeps nothing at all, so the probe's own answer would come back empty.
  const auto one = cs2::control::build_trace_fan(eye, basis, 0.0F, 0.0F, 0.0F, 1, 1000.0F);
  const cs2::aim::Aabb single = cs2::control::trace_target_box(one, k_pad);
  expect(single.max.x > single.min.x && single.max.y > single.min.y && single.max.z > single.min.z,
         "a single ray's target box has volume (the pyramid is not degenerate)");
  expect(near(single.min.x, one.front().end.x - k_pad) &&
             near(single.max.x, one.front().end.x + k_pad),
         "the target box is the far end, padded");

  // A fan: the box covers every far end.
  const auto fan = cs2::control::build_trace_fan(eye, basis, 30.0F, 0.0F, 5.0F, 5, 1000.0F);
  const cs2::aim::Aabb spread = cs2::control::trace_target_box(fan, k_pad);
  bool covers = true;
  for (const cs2::control::TraceRay& ray : fan) {
    covers = covers && ray.end.x >= spread.min.x && ray.end.x <= spread.max.x &&
             ray.end.y >= spread.min.y && ray.end.y <= spread.max.y && ray.end.z >= spread.min.z &&
             ray.end.z <= spread.max.z;
  }
  expect(covers, "the fan's target box covers every far end");
}

// --- 9. the results buffer and its canary ---------------------------------------
//
// The engine appends **at** `data[count]` and its growth is switched off (the
// external-buffer bit), so a trace with more records than the buffer holds writes
// **past** the end of the records. The buffer therefore carries a **canary**,
// which is re-armed before every engine call and checked immediately after it —
// before the count is trusted.
//
// The bound itself is **not** proved (the engine has no hard limit): what the
// canary makes true is that an overrun is **visible** and **costs the answer**.
void test_the_results_buffer_and_its_canary() {
  // The buffer's layout: the records, then the canary, immediately after them.
  static_assert(offsetof(cs2::control::TraceIntervalBuffer, canary) ==
                    cs2::control::TRACE_INTERVAL_RECORDS * cs2::control::TRACE_INTERVAL_SIZE,
                "the canary sits immediately after the records");
  static_assert(sizeof(cs2::control::TraceIntervalBuffer) ==
                    cs2::control::TRACE_INTERVAL_RECORDS * cs2::control::TRACE_INTERVAL_SIZE +
                        cs2::control::TRACE_INTERVAL_CANARY_SIZE,
                "the buffer is the records and the canary, and nothing else");

  // The sizing: the records the buffer holds, against the engine's own bullet
  // path (64 entries, and it grows) and against the published maximum.
  expect(cs2::control::TRACE_INTERVAL_RECORDS ==
             cs2::control::TRACE_INTERVAL_CAPACITY + cs2::control::TRACE_INTERVAL_SLACK,
         "the buffer holds the advertised capacity plus the slack");
  expect(cs2::control::TRACE_INTERVAL_RECORDS >= cs2::control::TRACE_MAX_INTERVALS,
         "the buffer is at least as large as the published maximum");
  expect(cs2::control::TRACE_INTERVAL_RECORDS >= 8 * 64,
         "the buffer is at least eight times the engine's own bullet sizing (64)");
  expect(cs2::control::TRACE_INTERVAL_SLACK > 0, "the buffer carries the slack");
  expect(cs2::control::TRACE_INTERVAL_CANARY_SIZE > 0, "the buffer carries a canary");

  // The canary: armed, intact, and **tripped** by a write past the records.
  cs2::control::TraceIntervalBuffer buffer{};
  cs2::control::arm_trace_canary(&buffer);
  expect(cs2::control::trace_canary_intact(buffer), "an armed canary is intact");
  // A write one byte **past** the last record is the overrun the canary is for.
  buffer.canary[0] = 0x00U;
  expect(!cs2::control::trace_canary_intact(buffer), "a write past the records trips the canary");
  // ...and a write inside the records does not.
  cs2::control::arm_trace_canary(&buffer);
  buffer.records[cs2::control::TRACE_INTERVAL_RECORDS * cs2::control::TRACE_INTERVAL_SIZE - 1] =
      0x11U;
  expect(cs2::control::trace_canary_intact(buffer), "a write inside the records does not trip it");
  // The canary's pattern is a byte the engine never writes.
  expect(cs2::control::TRACE_INTERVAL_CANARY_BYTE != 0x00U, "the canary's pattern is not zero");
}

// --- 10. the cold-cache refusal -------------------------------------------------
//
// The probe **refuses** to answer while the world's static body is not parsed:
// paying the world's parse (10k brush shapes) inside the game's CreateMove hook is
// the one thing that could stall the game, and our own answer would be empty
// anyway — so the comparison the probe exists to make would be meaningless.
//
// The rule is pure, so it is pinned here; the refusal itself is checked against
// the source below (the probe needs the game).
void test_the_cold_cache_refusal() {
  expect(cs2::control::trace_refusal(true) == cs2::control::TraceRefusal::none,
         "a warm world's cache is no reason to refuse");
  expect(cs2::control::trace_refusal(false) == cs2::control::TraceRefusal::world_cache_cold,
         "a cold world's cache is a refusal");
  expect(std::string(cs2::control::to_string(cs2::control::TraceRefusal::world_cache_cold)) ==
             "world_cache_cold",
         "the refusal names the cold cache");
  expect(std::string(cs2::control::to_string(cs2::control::TraceRefusal::none)).empty(),
         "no refusal has no reason");

  // **What a refusal publishes.** Both sides are empty and the difference is
  // `unknown`: the probe must never report "agrees" over two empty lists, and it
  // must say the engine's answer is missing rather than quietly publishing an
  // empty one.
  cs2::control::TraceRay ray{};
  ray.start = cs2::aim::Vec3{.x = 1.0F, .y = 2.0F, .z = 3.0F};
  ray.direction = cs2::aim::Vec3{.x = 1.0F, .y = 0.0F, .z = 0.0F};
  const cs2::control::TraceRayResult refused = cs2::control::trace_refused_ray(ray);
  expect(refused.engine_answer_missing, "a refused ray's engine answer is missing");
  expect(refused.intervals.empty(), "a refused ray publishes no engine intervals");
  expect(refused.ours.empty(), "a refused ray publishes no occluders of ours");
  expect(refused.diff.verdict == cs2::control::TraceVerdict::unknown,
         "a refused ray's difference is 'unknown'");
  expect(!refused.diff.engine_answer_usable, "a refused ray's difference is not usable");
  expect(refused.diff.engine_only.empty() && refused.diff.ours_only.empty(),
         "a refused ray has no difference to report");
  expect(refused.ray.direction.x == 1.0F, "a refused ray still carries its ray");
}

// --- 8. the service's wiring ----------------------------------------------------
//
// The service is not reachable from a host test (it needs the game), so the wiring
// is checked against the **source** — but the check is scoped to the probe's **own**
// region, so that a token which occurs everywhere (the overlap query's own
// `vector.flags = 0x80000000U`) cannot satisfy it.
//
// Each of these must fail when the corresponding code is broken:
//   * the probe's engine call deleted → `run_trace_query(` is gone;
//   * the external-buffer flag deleted → `vector.flags =` is gone;
//   * the trace's container replaced by the overlap's → `TraceResultVector` is gone.
void test_service_uses_the_rules(const char* root) {
  const std::filesystem::path base(root);
  const std::filesystem::path service_path = base / "payload" / "control" / "src" / "service.cpp";
  const std::filesystem::path query_path =
      base / "payload" / "control" / "src" / "occlusion_query.cpp";

  const std::string service = read_source(service_path);
  expect(!service.empty(), "the service's source is readable");
  const std::string query = read_source(query_path);
  expect(!query.empty(), "the occlusion query's source is readable");

  // The command itself.
  expect(service.find("do_trace_probe") != std::string::npos,
         "the service has the trace_probe command");
  expect(service.find("trace_fan_size_ok(") != std::string::npos,
         "the command refuses a fan over the maximum with the rule");
  expect(service.find("trace_probe_armed_") != std::string::npos,
         "the command and the game thread hand the request over");

  // The probe's own region: from `probe_trace` to the end of the file.
  const std::string probe = region_of(query, "auto probe_trace(", nullptr);
  expect(!probe.empty(), "the probe's own region is found in the occlusion query");
  expect(probe.find("run_trace_query(") != std::string::npos,
         "the probe calls the engine through the guarded query");
  expect(probe.find("TraceResultVector") != std::string::npos,
         "the probe hands the engine the trace's own results container");
  expect(probe.find("vector.flags =") != std::string::npos, "the probe sets the container's flags");
  expect(probe.find("TRACE_VECTOR_EXTERNAL_BUFFER") != std::string::npos,
         "the probe marks the buffer external: the engine must not allocate");
  expect(probe.find("vector.data =") != std::string::npos &&
             probe.find("vector.capacity =") != std::string::npos,
         "the probe hands the engine its own buffer and capacity");
  expect(probe.find("diff_trace(") != std::string::npos,
         "the difference is computed with the rule");
  expect(probe.find("engine_answer_missing") != std::string::npos,
         "the probe says when the engine's answer could not be read");
  // The flag must be **computed** from the call's outcome, not merely named: a
  // probe that always says "the answer was read" is the defect this task is
  // about.
  const std::string flat_probe = flattened(probe);
  expect(flat_probe.find("engine_answer_missing = !ok") != std::string::npos,
         "the dropped-answer flag is computed from the engine call's outcome");
  expect(flat_probe.find("!result.engine_answer_missing") != std::string::npos,
         "the verdict is computed only when the engine's answer was read");
  expect(probe.find("read_trace_interval(") != std::string::npos,
         "the interval records are read with the reader");
  expect(probe.find("set_occlusion_exclude_pawns(") != std::string::npos,
         "the probe excludes the players itself");

  // The results buffer: **not** a local array on the game's stack (the probe runs
  // on the game's main thread, in its input path), and the canary armed before
  // the engine call and checked after it.
  expect(probe.find("unsigned char interval_buffer[") == std::string::npos,
         "the results buffer is not a local array on the game's stack");
  expect(probe.find("trace_interval_buffer()") != std::string::npos,
         "the probe takes the shared results buffer");
  expect(probe.find("vector.data = buffer.records") != std::string::npos,
         "the probe hands the engine the buffer's records");
  const std::size_t arm_at = flat_probe.find("arm_trace_canary(");
  const std::size_t engine_call_at = flat_probe.find("run_trace_query(");
  const std::size_t check_at = flat_probe.find("trace_canary_intact(");
  expect(arm_at != std::string::npos, "the probe arms the canary");
  expect(engine_call_at != std::string::npos, "the probe calls the engine");
  expect(check_at != std::string::npos, "the probe checks the canary");
  expect(
      arm_at != std::string::npos && engine_call_at != std::string::npos && arm_at < engine_call_at,
      "the canary is armed **before** the engine call");
  expect(check_at != std::string::npos && engine_call_at != std::string::npos &&
             check_at > engine_call_at,
         "the canary is checked **after** the engine call");
  // The count is checked against the buffer's **real** size, and an overrun
  // **costs the answer** — the probe cannot publish an answer it cannot vouch
  // for.
  expect(flat_probe.find("count <= TRACE_INTERVAL_RECORDS") != std::string::npos,
         "the count is checked against the buffer's real size");
  expect(flat_probe.find(
             "engine_answer_missing = !ok || !buffer_kept || !count_usable || !buffer_intact") !=
             std::string::npos,
         "an overrun refuses the answer");
  expect(flat_probe.find("run.overrun = run.overrun || !buffer_intact") != std::string::npos,
         "an overrun is published");
  expect(probe.find("occlusion_world_cache_ready()") != std::string::npos,
         "the probe reads whether the world's cache is cold");

  // The **refusal**: the probe must decide to refuse **before** it enumerates, so
  // it never pays the world's parse inside the input hook. The order is the whole
  // point, so it is checked as an order.
  const std::size_t refusal_at = flat_probe.find("trace_refusal(");
  const std::size_t enumeration_at = flat_probe.find("query_world_occluders(");
  expect(refusal_at != std::string::npos, "the probe asks whether it must refuse");
  expect(enumeration_at != std::string::npos, "the probe enumerates the world's occluders");
  expect(refusal_at != std::string::npos && enumeration_at != std::string::npos &&
             refusal_at < enumeration_at,
         "the refusal comes **before** the enumeration: the probe does not parse the world body");
  const std::string refusal_region =
      (refusal_at != std::string::npos && enumeration_at != std::string::npos)
          ? flat_probe.substr(refusal_at, enumeration_at - refusal_at)
          : std::string{};
  expect(refusal_region.find("run.refusal = refusal") != std::string::npos,
         "the refusal is published");
  expect(refusal_region.find("trace_refused_ray(") != std::string::npos,
         "the refusal publishes the refused ray's answer");

  // The guarded call: the engine call is behind the tree's **one** guard. The probe
  // used to carry its own copy of it; `cs2::diag` owns the tree's only recovery
  // mechanism now, and the tree's `diag-guard` test fails when a second one
  // appears.
  const std::string guarded = region_of(query, "auto run_trace_query(", "}  // namespace");
  expect(!guarded.empty(), "the guarded query is found in the occlusion query");
  expect(guarded.find("cs2::diag::call_guarded(") != std::string::npos,
         "the engine's trace call is fault-guarded by the tree's one guard");
  expect(guarded.find("trace_call_guarded(") == std::string::npos,
         "the probe does not carry its own guard");

  // The publication: the answer carries the probe's own health.
  const std::string publication = region_of(service, "void Service::do_trace_probe", nullptr);
  expect(!publication.empty(), "the command's publication is found in the service");
  expect(publication.find("answer_missing") != std::string::npos,
         "the answer says when the engine's answer was dropped");
  expect(publication.find("engine_answer_usable") != std::string::npos,
         "the answer says whether the difference could be computed");
  expect(publication.find("trace_probe_armed_") != std::string::npos,
         "the command arms the request for the game thread");
  expect(publication.find("\"refused\"") != std::string::npos,
         "the answer says that the probe refused");
  expect(publication.find("\"refusal_reason\"") != std::string::npos,
         "the answer says why the probe refused");

  // The engine call runs in the **game thread's sink** — the CreateMove hook — and
  // not in the control thread (an engine call that reaches tier0's allocator faults
  // on the payload's own thread, docs/handoff.md).
  const std::string sink = region_of(service, "void Service::on_game_thread()", nullptr);
  expect(!sink.empty(), "the game thread's sink is found in the service");
  expect(sink.find("probe_trace(") != std::string::npos,
         "the command runs the probe on the game thread");
}

}  // namespace

auto main(int argc, char** argv) -> int {
  test_ray_construction();
  test_fan_order();
  test_interval_reader();
  test_result_vector_layout();
  test_difference();
  test_bounds();
  test_target_box();
  test_the_results_buffer_and_its_canary();
  test_the_cold_cache_refusal();
  if (argc >= 2) {
    test_service_uses_the_rules(argv[1]);
  }

  if (failures == 0) {
    std::printf("control-trace-probe: ok\n");
    return 0;
  }
  return 1;
}
