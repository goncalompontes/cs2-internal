// The trace probe's host test (`docs/re/bullet-trace.md`).
//
// The probe's answer is only worth reading when two things hold: the probe's ray
// and the solver's ray are the **same** 3-D segment, and the difference logic
// turns the two answers into the right verdict. Both are pure, so they are pinned
// here; the engine call itself needs the game.
//
// The service is not reachable from a host test (it needs the game), so the last
// check is a **source** check: the command's own wiring uses the rules the header
// defines (the same pattern as `control-rage-objective`).

#include <cmath>
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

  // A fan of one ray is the view ray, whatever the spread.
  const auto single = cs2::control::build_trace_fan(eye, basis, yaw, pitch, spread, 1, 1000.0F);
  expect(single.size() == 1U && near(single.front().direction, rays.front().direction),
         "a fan of one ray is the view ray");
}

// --- 3. the interval record reader --------------------------------------------
void test_interval_reader() {
  std::uint8_t record[cs2::control::TRACE_INTERVAL_SIZE] = {};
  const float fraction = 0.25F;
  std::memcpy(record + cs2::control::TRACE_INTERVAL_FRACTION, &fraction, sizeof(float));
  record[cs2::control::TRACE_INTERVAL_ENTER_EXIT] = 1U;
  record[0] = 0xABU;  // a byte outside the pinned fields: the raw copy must keep it

  const cs2::control::TraceInterval interval = cs2::control::read_trace_interval(record, 800.0F);
  expect(near(interval.fraction, 0.25F), "the fraction is read from +0x0C");
  expect(near(interval.distance, 200.0F), "the distance is the fraction times the ray's length");
  expect(interval.enter_exit == 1U, "the enter/exit byte is read from +0x32");
  expect(interval.raw[0] == 0xABU, "the raw bytes keep the fields that are not mapped");
  expect(std::memcmp(interval.raw, record, cs2::control::TRACE_INTERVAL_SIZE) == 0,
         "the whole record is copied");

  // A "leave" record reads the other byte value.
  record[cs2::control::TRACE_INTERVAL_ENTER_EXIT] = 0U;
  const cs2::control::TraceInterval leave = cs2::control::read_trace_interval(record, 800.0F);
  expect(leave.enter_exit == 0U, "a leave record reads zero at +0x32");

  // A missing record reads as empty (never a fault).
  const cs2::control::TraceInterval missing = cs2::control::read_trace_interval(nullptr, 800.0F);
  expect(missing.fraction == 0.0F && missing.distance == 0.0F && missing.enter_exit == 0U,
         "a missing record reads as empty");
}

// --- 4. the difference logic ---------------------------------------------------
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

  // Nothing on either side: the two answers agree.
  {
    const cs2::control::TraceDiff diff = cs2::control::diff_trace({}, {}, 2.0F);
    expect(diff.verdict == cs2::control::TraceVerdict::agrees,
           "no intervals and no occluders agree");
    expect(diff.engine_only.empty() && diff.ours_only.empty(), "the difference is empty");
  }

  // The engine found a solid we have nothing for.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const cs2::control::TraceDiff diff = cs2::control::diff_trace(intervals, {}, 2.0F);
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
    const cs2::control::TraceDiff diff = cs2::control::diff_trace(intervals, ours, 2.0F);
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
    const cs2::control::TraceDiff diff = cs2::control::diff_trace(intervals, ours, 2.0F);
    expect(diff.verdict == cs2::control::TraceVerdict::agrees,
           "the same solid on both sides agrees");
    expect(diff.engine_only.empty() && diff.ours_only.empty(), "the difference is empty");
  }

  // The same solid at a **different depth**: the engine's span and our occluder
  // overlap, so they are the same solid — our thickness is simply bigger.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(110.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> ours = {hit_at(100.0F, 130.0F)};
    const cs2::control::TraceDiff diff = cs2::control::diff_trace(intervals, ours, 2.0F);
    expect(diff.verdict == cs2::control::TraceVerdict::agrees,
           "the same solid at a different depth agrees");
  }

  // Two different solids: the difference is in both directions.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> ours = {hit_at(400.0F, 430.0F)};
    const cs2::control::TraceDiff diff = cs2::control::diff_trace(intervals, ours, 2.0F);
    expect(diff.verdict == cs2::control::TraceVerdict::both, "two different solids are 'both'");
    expect(diff.engine_only.size() == 1U && diff.ours_only.size() == 1U,
           "both differences are reported");
  }

  // The tolerance: an occluder a unit away is the same solid, one ten units away
  // is not.
  {
    const std::vector<cs2::control::TraceInterval> intervals = {interval_at(100.0F, 1U),
                                                                interval_at(120.0F, 0U)};
    const std::vector<cs2::control::TraceOccluderHit> close = {hit_at(101.0F, 119.0F)};
    const std::vector<cs2::control::TraceOccluderHit> far = {hit_at(130.0F, 150.0F)};
    expect(cs2::control::diff_trace(intervals, close, 2.0F).verdict ==
               cs2::control::TraceVerdict::agrees,
           "an occluder within the tolerance is the same solid");
    expect(
        cs2::control::diff_trace(intervals, far, 2.0F).verdict == cs2::control::TraceVerdict::both,
        "an occluder beyond the tolerance is a different solid");
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

// --- 5. the bounds --------------------------------------------------------------
void test_bounds() {
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
}

// --- 6. the service's wiring ----------------------------------------------------
void test_service_uses_the_rules(const char* root) {
  const std::filesystem::path service_path =
      std::filesystem::path(root) / "payload" / "control" / "src" / "service.cpp";
  std::ifstream service_file(service_path);
  expect(service_file.good(), "the service's source is readable");
  if (!service_file.good()) {
    return;
  }
  const std::string service((std::istreambuf_iterator<char>(service_file)),
                            std::istreambuf_iterator<char>());
  expect(service.find("do_trace_probe") != std::string::npos,
         "the service has the trace_probe command");
  expect(service.find("trace_fan_size_ok(") != std::string::npos,
         "the command refuses a fan over the maximum with the rule");
  expect(service.find("probe_trace(rays, true, abi)") != std::string::npos,
         "the engine call runs on the game thread (in the game-thread sink)");
  expect(service.find("trace_probe_armed_") != std::string::npos,
         "the command and the game thread hand the request over");

  const std::filesystem::path query_path =
      std::filesystem::path(root) / "payload" / "control" / "src" / "occlusion_query.cpp";
  std::ifstream query_file(query_path);
  expect(query_file.good(), "the occlusion query's source is readable");
  if (!query_file.good()) {
    return;
  }
  const std::string query((std::istreambuf_iterator<char>(query_file)),
                          std::istreambuf_iterator<char>());
  expect(query.find("trace_call_guarded(&invoke_trace") != std::string::npos,
         "the engine's trace call is fault-guarded");
  expect(query.find("0x80000000U") != std::string::npos,
         "the results vector is external: the engine must not allocate");
  expect(query.find("read_trace_interval(") != std::string::npos,
         "the interval records are read with the reader");
  expect(query.find("diff_trace(") != std::string::npos,
         "the difference is computed with the rule");
}

}  // namespace

auto main(int argc, char** argv) -> int {
  test_ray_construction();
  test_fan_order();
  test_interval_reader();
  test_difference();
  test_bounds();
  if (argc >= 2) {
    test_service_uses_the_rules(argv[1]);
  }

  if (failures == 0) {
    std::printf("control-trace-probe: ok\n");
    return 0;
  }
  return 1;
}
