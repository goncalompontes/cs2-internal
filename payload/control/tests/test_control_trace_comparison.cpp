// Host tests for the trace probe's **comparison** — the engine's answer replayed
// through the payload's own `pair_trace_spans` / `diff_trace`
// (`cs2/control/occlusion_query.hpp`).
//
// **Why a fixture.** The probe computes the difference live, and until now nothing
// asserted what it computed: a change to the pairing or to the matching rule would
// pass every review and only show up as a wrong reading in the next live run — which
// is exactly how the enter/leave polarity was misread (`docs/re/occlusion.md`
// §"The interval record's polarity"). The fixture is one **recorded** ray, engine half
// verbatim (raw interval bytes included), and the test replays it with no game.
//
// The fixture is also a **measurement**: it is the last ray recorded before the
// multi-surface mesh fix (`86275be`), so its three genuine `engine_only` spans are the
// baseline the live run after that fix is compared against.

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "cs2/control/json.hpp"
#include "cs2/control/occlusion_query.hpp"

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
  if (!condition) {
    std::printf("FAIL: %s\n", what);
    ++failures;
  }
}

auto near(float a, float b, float tolerance = 0.05F) -> bool {
  return std::fabs(a - b) <= tolerance;
}

// A lowercase hex byte pair -> value.
auto hex_byte(char high, char low) -> std::uint8_t {
  const auto digit = [](char c) -> int {
    if (c >= '0' && c <= '9') {
      return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
      return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
      return c - 'A' + 10;
    }
    return -1;
  };
  const int h = digit(high);
  const int l = digit(low);
  if (h < 0 || l < 0) {
    return 0;
  }
  return static_cast<std::uint8_t>((h << 4) | l);
}

auto read_fixture(const std::string& path) -> std::string {
  std::ifstream file(path);
  std::ostringstream text;
  text << file.rdbuf();
  return text.str();
}

auto kind_from_name(std::string_view name) -> cs2::aim::SolidKind {
  return name == "mesh" ? cs2::aim::SolidKind::mesh : cs2::aim::SolidKind::polytope;
}

// One engine interval, from the fixture's own fields. The distance is recomputed from
// the fraction and the recorded depth, exactly as `read_trace_interval` does, so a
// changed calculation shows up here.
auto interval_from_json(const cs2::control::Json& item, float depth)
    -> cs2::control::TraceInterval {
  cs2::control::TraceInterval interval;
  interval.fraction = static_cast<float>(item.find("fraction")->as_number());
  interval.distance = interval.fraction * depth;
  interval.enter_exit = static_cast<std::uint8_t>(item.find("enter_exit")->as_int());
  const std::string_view raw = item.find("raw")->as_string();
  for (std::size_t i = 0; i + 1 < raw.size() && i / 2 < cs2::control::TRACE_INTERVAL_SIZE; i += 2) {
    interval.raw[i / 2] = hex_byte(raw[i], raw[i + 1]);
  }
  return interval;
}

auto occluder_from_json(const cs2::control::Json& item) -> cs2::control::TraceOccluderHit {
  cs2::control::TraceOccluderHit hit;
  hit.index = static_cast<int>(item.find("index")->as_int());
  hit.cluster = static_cast<int>(item.find("cluster")->as_int());
  hit.kind = kind_from_name(item.find("kind")->as_string());
  hit.entry = static_cast<float>(item.find("entry")->as_number());
  hit.exit = static_cast<float>(item.find("exit")->as_number());
  return hit;
}

// Is there a span entered/left at these two distances?
auto has_span(const std::vector<cs2::control::TraceSpan>& spans, float entry, float exit) -> bool {
  for (const cs2::control::TraceSpan& span : spans) {
    if (near(span.entry, entry) && near(span.exit, exit)) {
      return true;
    }
  }
  return false;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  if (argc < 2) {
    std::printf("FAIL: the fixture path is the first argument\n");
    return 1;
  }
  const std::string source = argv[1];
  const std::string fixture_path =
      source + "/payload/control/tests/fixtures/trace-comparison-dust2-wall.json";
  const std::optional<cs2::control::Json> parsed =
      cs2::control::Json::parse(read_fixture(fixture_path));
  if (!parsed.has_value()) {
    std::printf("FAIL: the fixture does not parse (%s)\n", fixture_path.c_str());
    return 1;
  }
  const cs2::control::Json& fixture = *parsed;
  const float depth = static_cast<float>(fixture.find("depth")->as_number());
  const float tolerance = static_cast<float>(fixture.find("tolerance")->as_number());
  expect(near(depth, 8192.0F), "the fixture records the 8192-unit trace");

  std::vector<cs2::control::TraceInterval> intervals;
  for (const cs2::control::Json& item : fixture.find("engine")->find("intervals")->items()) {
    intervals.push_back(interval_from_json(item, depth));
  }
  std::vector<cs2::control::TraceOccluderHit> ours;
  for (const cs2::control::Json& item : fixture.find("ours")->items()) {
    ours.push_back(occluder_from_json(item));
  }
  expect(intervals.size() == 17, "the engine's answer has 17 interval records");
  expect(ours.size() == 12, "our answer has 12 occluder crossings");

  // The published distance is the fraction times the depth, within float rounding.
  for (std::size_t i = 0; i < intervals.size(); ++i) {
    const cs2::control::Json& item = fixture.find("engine")->find("intervals")->items()[i];
    expect(
        near(intervals[i].distance, static_cast<float>(item.find("distance")->as_number()), 0.01F),
        "the interval's distance is fraction * depth");
  }

  // 1. The polarity: `0` is the entry, `1` is the leave. The recorded hull
  //    `266.09 -> 273.30` is entered and left at the engine's records `266.02` (`0`)
  //    and `273.03` (`1`) — the pairing must reproduce that span.
  expect(intervals[5].enter_exit == 0 && near(intervals[5].distance, 266.022F),
         "the first hull's entry record carries 0");
  expect(intervals[6].enter_exit == 1 && near(intervals[6].distance, 273.029F),
         "the first hull's leave record carries 1");
  const std::vector<cs2::control::TraceSpan> spans = cs2::control::pair_trace_spans(intervals);
  expect(spans.size() == 9, "the 17 records pair into 9 spans");
  expect(has_span(spans, 266.022F, 273.029F), "the first hull's span is 266.02 -> 273.03");
  expect(has_span(spans, 1136.666F, 1138.884F), "the second hull's span pairs");
  expect(has_span(spans, 1139.248F, 1145.174F), "the third hull's span pairs");
  expect(has_span(spans, 1.04611182F, 9.64404297F), "the player's own body span pairs");
  expect(has_span(spans, 94.7949982F, 148.944336F), "the first mesh slab pairs");
  expect(has_span(spans, 205.570496F, 517.872009F),
         "the brush volume pairs around the hull nested inside it (last in, first out)");
  expect(has_span(spans, 2887.15308F, 2887.15308F),
         "a span the ray never leaves is a point at its entry");

  // 2. The polarity is not a free choice: reading `1` as the entry moves every pair by
  //    one record and no longer produces the hull's span. This is the misreading the
  //    fixture exists to prevent.
  std::vector<cs2::control::TraceInterval> flipped = intervals;
  for (cs2::control::TraceInterval& interval : flipped) {
    interval.enter_exit = interval.enter_exit == 0 ? 1 : 0;
  }
  expect(!has_span(cs2::control::pair_trace_spans(flipped), 266.022F, 273.029F),
         "the opposite polarity cannot reproduce our own hull's span");

  // 3. The difference as the probe publishes it. The mesh slabs are ours as **two
  //    surfaces** (an open sheet has no volume), so they are answered by the
  //    surface-bounds rule, not by a solid covering the span.
  const cs2::control::TraceDiff diff = cs2::control::diff_trace(intervals, ours, tolerance, true);
  expect(diff.verdict == cs2::control::TraceVerdict::both,
         "the recorded ray reads `both` — the two answers differ in both directions");
  expect(diff.spans_bounded_by_surfaces == 3,
         "three of the engine's spans are answered by our surfaces");
  expect(diff.engine_only.size() == 3, "three engine spans have no geometry of ours");
  expect(has_span(diff.engine_only, 1.04611182F, 9.64404297F),
         "the local player's own body is (by design) not in the world set");
  expect(has_span(diff.engine_only, 205.570496F, 517.872009F),
         "the volume the recorded build had no faces for is still reported");
  expect(has_span(diff.engine_only, 3058.41309F, 4058.93335F),
         "the far mesh slab's far face is still reported");
  expect(diff.ours_only.size() == 4, "four of our surfaces sit where the engine has no boundary");

  // 4. The comparison must be able to fail: delete one of our hulls and its span moves
  //    into `engine_only`.
  std::vector<cs2::control::TraceOccluderHit> without_hull = ours;
  int hull_index = -1;
  for (std::size_t i = 0; i < without_hull.size(); ++i) {
    if (without_hull[i].kind != cs2::aim::SolidKind::mesh && near(without_hull[i].entry, 266.09F)) {
      hull_index = static_cast<int>(i);
    }
  }
  expect(hull_index >= 0, "the fixture holds the hull at 266.09");
  if (hull_index >= 0) {
    without_hull.erase(without_hull.begin() + hull_index);
  }
  const cs2::control::TraceDiff dropped =
      cs2::control::diff_trace(intervals, without_hull, tolerance, true);
  expect(has_span(dropped.engine_only, 266.022F, 273.029F),
         "deleting our hull makes the engine's span engine-only");

  // 5. The invariant the handoff calls out: an unusable engine answer is `unknown`
  //    with both lists empty — never `agrees` over nothing.
  const cs2::control::TraceDiff unknown =
      cs2::control::diff_trace(intervals, ours, tolerance, false);
  expect(unknown.verdict == cs2::control::TraceVerdict::unknown,
         "an unusable engine answer is `unknown`");
  expect(unknown.engine_only.empty() && unknown.ours_only.empty(),
         "an unusable engine answer publishes no difference");

  // 6. The world mesh claim: the engine's boundaries are the world body's mesh
  //    surfaces, and our answer contains them (`kind == mesh`).
  int mesh_crossings = 0;
  for (const cs2::control::TraceOccluderHit& hit : ours) {
    if (hit.kind == cs2::aim::SolidKind::mesh) {
      ++mesh_crossings;
    }
  }
  expect(mesh_crossings == 9, "nine of our twelve crossings are mesh surfaces");
  bool mesh_at_engine_slab = false;
  for (const cs2::control::TraceOccluderHit& hit : ours) {
    if (hit.kind == cs2::aim::SolidKind::mesh && near(hit.entry, 94.7949982F)) {
      mesh_at_engine_slab = true;
    }
  }
  expect(mesh_at_engine_slab, "our mesh surface lands on the engine's own slab boundary");

  if (failures != 0) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("ok: the recorded trace replays through the probe's own comparison\n");
  return 0;
}
