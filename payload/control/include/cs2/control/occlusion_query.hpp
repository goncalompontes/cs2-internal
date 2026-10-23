#pragma once

// World-solids enumeration for the occlusion heatmap (docs/re/occlusion.md).
//
// Calls `CGamePhysicsQueryInterface::EntitiesInBox` once for the box spanning the
// eye→target segment and parses the returned overlap records into world-space
// convex hulls, which `aim::project_occluder` then projects into the chart. The
// engine call is wrapped in `diag::call_guarded`, so a wrong ABI faults back here
// instead of killing the process (the payload has no exception tables).

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <vector>

#include "cs2/aim/bvh.hpp"
#include "cs2/aim/geometry.hpp"
#include "cs2/aim/occlusion.hpp"

namespace cs2::control {

// A world solid enumerated from the engine's physics query. A brush/hull is a
// polytope (world-space convex vertices + inward planes); a sphere/capsule prop is
// carried analytically (`docs/ragebot.md` §10 — "sphere/capsule (+ as hulls of their
// extremes)"), so `service` can project either.
struct OcclusionHull {
  cs2::aim::SolidKind kind = cs2::aim::SolidKind::polytope;
  std::vector<cs2::aim::Vec3> vertices;         // polytope: world-space convex hull vertices
  std::vector<cs2::aim::OccluderPlane> planes;  // polytope: inward half-spaces
  // mesh: a triangle soup, flat triples of world-space points (3 per triangle). A
  // collision **mesh** is not convex, so `service` projects it via
  // `project_mesh_occluder` and `ray_span` intersects its triangles
  // (`docs/re/vphysics2.md`).
  std::vector<cs2::aim::Vec3> triangles;
  // mesh: the triangle BVH, built once at parse time so `ray_span` is O(log n) instead
  // of scanning the soup (`docs/ragebot-perf.md` §3.7). Empty for non-meshes.
  std::shared_ptr<const cs2::aim::MeshBvh> mesh_bvh;
  cs2::aim::Vec3 a{};   // sphere: centre; capsule: endpoint a; mesh AABB min
  cs2::aim::Vec3 b{};   // capsule: endpoint b; mesh AABB max
  float radius = 0.0F;  // sphere / capsule radius
  // The solid's world-space AABB, from the `RnHull_t` header (`+0x14`/`+0x20`) when
  // available — cached so the per-query broad phase is O(1) per solid instead of
  // O(vertices) (`docs/re/vphysics2.md` P1.7).
  cs2::aim::Vec3 bounds_min{};
  cs2::aim::Vec3 bounds_max{};
  bool has_bounds = false;
  // The target (enemy pyramid) this solid was enumerated for: a solid can only attenuate
  // the hitboxes of the enemy whose eye→enemy pyramid contained it, so the solver feeds
  // each hitbox only its own target's occluders plus the untagged (`-1`) shared ones.
  // Without this, every hitbox was split by every enemy's occluders, which is what made
  // `occlude_polygon` explode into tens of thousands of fragments (`docs/ragebot-perf.md`).
  int target = -1;
  // The shape's real surface (`CGameSurfaceProperties`, docs/re/penetration.md).
  float surface_modifier = 1.0F;  // bulletPenetrationDistanceModifier
  float damage_modifier = 1.0F;   // bulletPenetrationDamageModifier
  int material = -1;              // gamematerial (CHAR_TEX_*)
  bool grate = false;             // CONTENTS_GRATE span
};

struct OcclusionQueryStats {
  bool complete = false;  // false if query, geometry, or penetration-surface data is incomplete
  std::uint32_t incomplete_reasons = 0;  // bitset of OcclusionQueryIssue
  bool ok = false;                       // the engine query returned (no fault)
  bool faulted = false;                  // a guarded fault was recovered
  int records = 0;                       // overlap records the query returned
  int bodies = 0;                        // records whose +0x58 resolved to a CPhysicsBody
  int shapes = 0;                        // collision shapes visited
  int hulls = 0;                         // convex hulls parsed
  int type_counts[8] =
      {};  // histogram of shape types seen (0 sphere, 1 capsule, 2 hull, 3 mesh, 4 compound)
  // Read-only diagnostics (filled even when the call is not invoked).
  std::uintptr_t iface = 0;          // *(client+0x2227058)
  std::uintptr_t world = 0;          // *(void**)iface
  std::uintptr_t filter = 0;         // the filter object we would pass
  std::uintptr_t filter_vtable = 0;  // *(void**)filter
  // The custom bullet filter (`build_bullet_filter`): whether the engine object was
  // copied, and the mask/group/flags actually handed to the world query (the mask lives
  // at `filter+8`, per the `EntitiesInBox` RE — docs/re/bullet-trace.md).
  bool filter_custom = false;
  std::int64_t filter_mask = 0;
  int filter_group = 0;
  int filter_flags = 0;
  int mesh_clusters = 0;  // collision-mesh clusters emitted (a big mesh is split)
  // RE probe (docs/re/occlusion.md): the first overlap record's `+0x50` material
  // token — the value the record build stores from `*(hit->vtbl[0x30]() + 0x28)`.
  // Its type is unresolved (an 8-byte load; could be a float pair or a pointer), so a
  // live run reports it raw so the type can be fixed without guessing.
  std::uintptr_t material_token = 0;
  int material_records = 0;
  // The resolved per-shape surface of the first shape parsed (docs/re/penetration.md):
  // the surface-table index and its `bulletPenetrationDistanceModifier`/`gamematerial`,
  // so a live run can confirm the surface read.
  int surface_index = -1;
  float surface_distance = 0.0F;
  int surface_material = -1;
  int surface_hits = 0;
  std::uintptr_t surface_array = 0;  // the resolved SurfacePropertiesList array base
  // RE probe: the first compound (shape type 4) seen — its `RnCompound_t*` and the
  // per-kind group counts, so a live run can finish the descriptor map
  // (`docs/re/vphysics2.md` §"Shape geometry offsets").
  std::uintptr_t compound = 0;
  int compound_instances = 0;  // `shape+0xD0` (0x30-byte instance records)
  int compound_meshes = 0;     // `cmp+0x28` (RnMesh_t, stride 0xC0)
  int compound_hulls = 0;      // `cmp+0xF0` (RnHull_t, stride 0xF8)
  int compound_g100 = 0;       // `cmp+0x100`
  int compound_g110 = 0;       // `cmp+0x110` (stride 0x10 — sphere?)
  int compound_g120 = 0;       // `cmp+0x120` (stride 0x1C — capsule?)
  // Player exclusion: records whose handle index matches a player are skipped (a
  // player is a target, not an occluder).
  int excluded_records = 0;
  // World-cache / world-body diagnostics (the static world is cached once).
  std::uintptr_t world_cache_body = 0;
  int world_cache_hulls = 0;   // cached hulls in the static world body
  int world_box_kept = 0;      // cached hulls whose AABB overlaps the query box
  int world_box_rejected = 0;  // cached hulls rejected by the AABB filter
  int hulls_no_planes = 0;     // hulls whose half-spaces were rebuilt from vertices
  // Diagnostic (`docs/ragebot-perf.md` §2.1): when the enumeration verifies itself, the
  // single-AABB query is also run on the *same* tick. The AABB volume contains the
  // pyramid union, so the pyramid's set must be a **subset** of the AABB's;
  // `enum_outside_box` (pyramid hulls the AABB missed) must be 0. The complementary
  // guarantee — that the pyramid keeps every solid that can really occlude — is proven by
  // the `aim-bvh` host test.
  int enum_box_kept = 0;
  int enum_pyramid_kept = 0;
  int enum_outside_box = 0;
  // The decisive one: hulls the AABB kept and the pyramid dropped **that are inside an
  // eye→target pyramid** — a real occluder lost. Must be 0.
  int enum_box_only_between = 0;
  // How many pyramid bases the enumeration was given, and the farthest one's distance
  // from the eye: if a nearby enemy is missing from this count, the pyramid covers the
  // wrong volume (and would prune the walls in the real line of sight).
  int enum_targets = 0;
  float enum_target_max_distance =
      0.0F;  // The first records' raw `+0x18` handles (hex), for the player-exclusion check.
  std::uint32_t record_handles[8] = {};
  int record_handle_count = 0;
};

// A query can return useful geometry and still be incomplete. Keep this result separate
// from the per-hitbox candidate cap: these issues describe omissions upstream of the
// solver and must fail closed for rage solves.
enum class OcclusionQueryIssue : std::uint32_t {
  not_invoked = 1U << 0,
  query_unavailable = 1U << 1,
  query_fault = 1U << 2,
  record_capacity = 1U << 3,
  record_scan_limit = 1U << 4,
  body_capacity = 1U << 5,
  parser_failure = 1U << 6,
  parser_limit = 1U << 7,
  mesh_cluster_limit = 1U << 8,
  unsupported_compound = 1U << 9,
  occluder_capacity = 1U << 10,
  target_capacity = 1U << 11,
  surface_metadata = 1U << 12,
};

struct OcclusionQueryResult {
  std::vector<OcclusionHull> hulls;
  OcclusionQueryStats stats;

  [[nodiscard]] auto complete() const noexcept -> bool {
    return stats.complete;
  }
};

// Set the entity pointers the payload's trace filter must reject (the players) — the
// engine calls the filter's `ShouldHitEntity` for every non-worldspawn overlap, so the
// filter removes a player's collision body (a target, not a solid) from the
// enumeration. Called on the game thread immediately before `query_world_occluders`.
void set_occlusion_exclude_pawns(const std::uintptr_t* pawns, int count) noexcept;

// Enumerate the solids that could lie between the eye (`from`) and the targets — the
// axis-aligned box `box_min`/`box_max` (the caller builds it from the eye and every
// target's eye position), plus a small margin, which is guaranteed to contain every
// solid any eye→hitbox ray can enter. Which of them actually occlude is decided per
// cell by depth in `build_heatmap` (`docs/ragebot.md` §3), so the enumeration can be a
// coarse box. It is deliberately **independent of the camera angles**: the solids
// between the eye and the targets do not depend on where the player is looking. With
// `invoke == false` it only fills the read-only diagnostics (no engine call).
//
// `exclude`/`exclude_count` are entity-handle indices (`handle & 0x7fff`) to skip:
// the players, which are targets, not occluders (the worldspawn, handle `0x8000`, is
// always kept). Pass `0`/`nullptr` to keep everything.
//
// `targets`/`target_count` are the target hitboxes' world AABBs. When given, the static
// world is enumerated as the **union of the eye→target pyramids** instead of the single
// eye→all-targets box: a solid can only occlude a hitbox if it meets a segment from the
// eye into that hitbox, so it must lie inside that hull (`AabbBvh::query_pyramid`). This
// is exact — a conservative superset of the true occluders, never a subset — and it is
// what keeps one distant enemy from making the query map-wide (`docs/ragebot-perf.md`
// §2.1). `box_min`/`box_max` remain the outer bound (and the entity/prop query box).
// `target_ids` names the entity behind each `targets[i]` box. The solid's
// `OcclusionHull::target` is set from it, so the solver can feed a hitbox only the
// solids its own target's pyramid kept. **Without it the tag is `-1` (shared)**: a
// tag that does not name the entity is worse than no tag, because every consumer
// compares it against `Hitbox::target`.
[[nodiscard]] auto query_world_occluders(const cs2::aim::Vec3& from, const cs2::aim::Vec3& box_min,
                                         const cs2::aim::Vec3& box_max, bool invoke,
                                         const std::uint16_t* exclude = nullptr,
                                         int exclude_count = 0,
                                         const cs2::aim::Aabb* targets = nullptr,
                                         int target_count = 0,
                                         const int* target_ids = nullptr,
                                         bool verify_enum = false)
    -> OcclusionQueryResult;

// --- the trace probe (docs/re/bullet-trace.md) ---------------------------------//
// The probe asks the engine **its own** question — `TraceLineIntervals`, the call the
// real bullet makes — for a given ray, and publishes **both** answers side by side plus
// the difference: what the engine's trace hit, what our occluder set says about the
// same ray, and where the two disagree.
//
// The engine's interval record is 0x38 bytes and only two of its fields are pinned
// (`docs/re/bullet-trace.md` §"The interval record"): the fraction at `+0x0C` and the
// enter/exit byte at `+0x32`. The probe therefore publishes the record's **raw bytes**
// — so a later RE pass can map the rest from the probe's own output — and names the
// surface `"unmapped"` rather than inventing a field's meaning.

// The interval record's size and its two pinned fields.
inline constexpr std::size_t TRACE_INTERVAL_SIZE = 0x38;
inline constexpr std::size_t TRACE_INTERVAL_FRACTION = 0x0C;
inline constexpr std::size_t TRACE_INTERVAL_ENTER_EXIT = 0x32;

// The probe's bounds: a diagnostic must not be able to stall the game.
inline constexpr int TRACE_MAX_RAYS = 8;
inline constexpr int TRACE_MAX_INTERVALS = 64;
// The results buffer's **advertised** capacity, in interval records. The engine's
// own bullet path sizes the same container for 64 entries and grows it; the probe
// cannot let it grow (the external-buffer bit switches the growth path off), so
// the probe provides the room itself.
inline constexpr int TRACE_INTERVAL_CAPACITY = 256;
// The slack past the advertised capacity. The engine appends **at** `data[count]`
// (it increments the count afterwards), so a trace that produced more records than
// the capacity would write past the end of the records. The slack absorbs that,
// and the **canary** (below) is what makes an overrun visible instead of silent.
inline constexpr int TRACE_INTERVAL_SLACK = 256;
// The records the buffer holds, in total.
//
// **The sizing.** The engine's own bullet path starts the same container at **64**
// records and grows it — so the engine expects a ray to cross a few dozen solids
// (each crossed solid contributes an "enter" and a "leave" record). 512 records
// are **eight times** that: 256 solids crossed, which is two orders of magnitude
// beyond what a bullet ray does. It is **not** a proof — the engine has no hard
// limit, and its growth is switched off, so a pathological trace could still
// overrun. That is what the canary is for: the bound is not proved, it is
// **watched**.
inline constexpr int TRACE_INTERVAL_RECORDS = TRACE_INTERVAL_CAPACITY + TRACE_INTERVAL_SLACK;
// The canary's size, in bytes. It sits **after** the records, so it is both the
// detector (the first byte past the records changes) and a short runway that
// keeps a modest overshoot inside the payload's own image instead of letting it
// run into whatever follows.
inline constexpr std::size_t TRACE_INTERVAL_CANARY_SIZE = 4096;
// The canary's pattern. A byte the engine never writes, so any change to it is
// the engine's.
inline constexpr std::uint8_t TRACE_INTERVAL_CANARY_BYTE = 0xA5;
// How many of **our** solids the probe projects for a ray. The projection is the
// expensive half of the probe's work (a convex hull per solid), so it is bounded
// like the rest of the probe.
inline constexpr int TRACE_MAX_OCCLUDERS = 256;
// The pad the probe's target box gets, in world units. It is **not** cosmetic: the
// pad decides the pyramid's face planes, i.e. **which solids the enumeration
// keeps**, so it is part of what "our answer" contains and it is published with the
// answer. A box that is a single point (one ray, no fan) is a degenerate hull — the
// eye→box pyramid has no face planes and `AabbBvh::query_pyramid` keeps nothing at
// all — so the pad is what makes the probe's own answer non-empty.
inline constexpr float TRACE_TARGET_PAD = 8.0F;
// How far apart the engine's span and our occluder's span may be and still be the
// same solid (world units). Both are measured along the same ray, so a matching pair
// lines up to within the solids' own size.
//
// The tolerance compares the two spans' **positions** (where each is entered); it is
// **not** a widening of the overlap test. A solid that merely sits near the engine's
// span, or that covers only a part of it, is a **different** solid — it must not be
// allowed to answer for it, or a real occlusion would be reported as "agrees".
inline constexpr float TRACE_MATCH_TOLERANCE = 2.0F;

// --- the engine's **trace** results container (docs/re/bullet-trace.md) ---------
//
// `TraceLineIntervals`'s second argument is **not** the overlap query's
// `CUtlVector`: it is a different type with a different header. The layout below
// was re-derived from the **live** build (client.dll sha256 `5a46a09b…`,
// 2026-10-22) from the two functions that write it:
//
// * the physics world's own query (`vphysics2.dll`, the world's vtable `+0x520`,
//   which `TraceLineIntervals` calls) and
// * the append helper the second query kind uses (`client.dll`'s
//   `FUN_18084d1a0`).
//
// Both read the count at `+0x00` and the record pointer at `+0x08`, both compare the
// count against the **capacity** at `+0x10`, and both grow the array **only** when
// `count == capacity` **and** bit 30 of the **flags** at `+0x14` is clear:
//
// ```
// movsxd r13, dword ptr [r15]              ; count    @ +0x00
// cmp    r13d, dword ptr [r15 + 0x10]      ; capacity @ +0x10
// jne    skip_growth
// test   dword ptr [r15 + 0x14], 0x40000000 ; flags    @ +0x14, bit 30
// jne    skip_growth                        ; <- an external buffer never grows
// ```
//
// So bit 30 (`0x40000000`) is this type's **external buffer** bit, and bit 31
// (`0x80000000`) says the buffer is not ours to free (the growth path tests both
// bits before it frees the old buffer). The overlap vector's `0x80000000` at
// `+0x1C` belongs to a **different** type and means nothing here.
struct TraceResultVector {
  std::int32_t count = 0;
  std::int32_t reserved = 0;
  void* data = nullptr;
  std::int32_t capacity = 0;
  std::int32_t flags = 0;
};
static_assert(sizeof(TraceResultVector) == 0x18, "the trace's results header is 0x18 bytes");

// The header's fields, at the offsets the live build reads them from.
inline constexpr std::size_t TRACE_VECTOR_COUNT = 0x00;
inline constexpr std::size_t TRACE_VECTOR_DATA = 0x08;
inline constexpr std::size_t TRACE_VECTOR_CAPACITY = 0x10;
inline constexpr std::size_t TRACE_VECTOR_FLAGS = 0x14;
// The flags' two top bits (see above).
inline constexpr std::int32_t TRACE_VECTOR_EXTERNAL_BUFFER = 0x40000000;
inline constexpr std::int32_t TRACE_VECTOR_NOT_OWNED = 0x80000000;

// The engine's results buffer: the interval records, then the canary.
//
// **Not on the game's stack.** The probe runs on the game's **main thread**, in
// its input path, and tens of kilobytes of stack are not the payload's to spend —
// and an overrun of a stack buffer is a silent corruption of the game's input
// path, which the fault guard **cannot** catch (it is not an unmapped page).
//
// **Not a local either.** It is a function-local static behind a typed accessor:
// the ADR-0010 escape hatch the payload already uses for the ESP's provider
// registry (`cs2/features/esp_cache.hpp`) — constant-initialised (the mapped
// image never runs the CRT's static-init guard) and trivially destructible (no
// atexit handler is registered). The injector maps the whole image with
// `MEM_COMMIT`, so the OS zeroes it.
//
// The **canary** sits immediately after the records. The engine's growth is
// switched off (the external-buffer bit), so a trace with more records than the
// buffer holds writes past the end of the records. The canary is re-armed before
// every engine call and checked immediately after it — **before** the count is
// trusted — so an overrun is visible and costs the answer.
struct alignas(16) TraceIntervalBuffer {
  unsigned char records[TRACE_INTERVAL_RECORDS * TRACE_INTERVAL_SIZE];
  unsigned char canary[TRACE_INTERVAL_CANARY_SIZE];
};
static_assert(offsetof(TraceIntervalBuffer, canary) == TRACE_INTERVAL_RECORDS * TRACE_INTERVAL_SIZE,
              "the canary sits immediately after the records");
static_assert(sizeof(TraceIntervalBuffer) ==
                  TRACE_INTERVAL_RECORDS * TRACE_INTERVAL_SIZE + TRACE_INTERVAL_CANARY_SIZE,
              "the buffer is the records and the canary, and nothing else");

// The buffer's storage: a function-local static behind a typed accessor (the
// ADR-0010 escape hatch, `cs2/features/esp_cache.hpp`). The accessor is defined
// in the query's own source, so there is exactly one buffer in the image.
[[nodiscard]] auto trace_interval_buffer() noexcept -> TraceIntervalBuffer&;

// Arm the canary: the bytes after the records carry the pattern, so a write past
// the end of the records is visible.
inline void arm_trace_canary(TraceIntervalBuffer* buffer) noexcept {
  if (buffer == nullptr) {
    return;
  }
  for (std::size_t i = 0; i < TRACE_INTERVAL_CANARY_SIZE; ++i) {
    buffer->canary[i] = TRACE_INTERVAL_CANARY_BYTE;
  }
}

// Is the canary still intact? A write past the end of the records changed it.
//
// This is the check that makes the "bounded" claim true in the only sense that
// can be made true: the probe **cannot publish an answer it cannot vouch for**.
[[nodiscard]] inline auto trace_canary_intact(const TraceIntervalBuffer& buffer) noexcept -> bool {
  for (std::size_t i = 0; i < TRACE_INTERVAL_CANARY_SIZE; ++i) {
    if (buffer.canary[i] != TRACE_INTERVAL_CANARY_BYTE) {
      return false;
    }
  }
  return true;
}

// The engine's answer for one ray: one interval record.
struct TraceInterval {
  // The record's `+0x0C`: the **fraction** along the ray (0..1). The engine's own
  // consumer multiplies the fraction difference by the ray's length to get the wall
  // thickness (`FUN_180886090`), so the fraction is relative to the ray, not a
  // distance.
  float fraction = 0.0F;
  // The fraction scaled by the ray's length: the same number in world units.
  float distance = 0.0F;
  // The record's `+0x32`: 0 = the ray **enters** a solid, non-zero = it leaves one.
  //
  // The polarity is the opposite of the first reading of the byte (`docs/re/occlusion.md`
  // used to call `1` the entry). It is pinned by the **recorded** ray in
  // `payload/control/tests/fixtures/trace-comparison-dust2-wall.json`: our own convex
  // hull is entered at `266.09` and left at `273.30`, and the engine's records at
  // `266.02` and `273.03` carry `0` and `1` respectively. The two other hulls on the
  // same ray (`1136.7 → 1138.9`, `1139.3 → 1145.2`) repeat the pattern, and the mesh
  // slabs' faces land on the same `0`/`1` boundaries. The engine's own consumer pairs
  // them in this order (see `pair_trace_spans`).
  std::uint8_t enter_exit = 0;
  // The record's raw bytes (the surface is unmapped — see above).
  std::uint8_t raw[TRACE_INTERVAL_SIZE] = {};
};

// One solid the enumeration kept, as the probe reports it: what kind it is and
// where it is. This is what attributes a missing wall — the per-ray lists only say
// that the ray hits nothing, not whether the solid was ever a candidate.
struct TraceHullInfo {
  cs2::aim::SolidKind kind = cs2::aim::SolidKind::polytope;
  cs2::aim::Vec3 min{};
  cs2::aim::Vec3 max{};
  int target = -1;
};

// One of **our** occluders the ray enters — the solver's own answer
// (`OccluderCell::ray_span`, not a re-implementation of it).
struct TraceOccluderHit {
  float entry = 0.0F;  // the distance from the eye where the ray enters the solid
  float exit = 0.0F;   // the distance where it leaves it
  cs2::aim::SolidKind kind = cs2::aim::SolidKind::polytope;
  int index = -1;    // the solid's index in the enumerated set
  int cluster = -1;  // the mesh cluster's ordinal (-1 for the other kinds)
};

// A span of the engine's answer: an enter/exit pair of interval records (the solid
// the ray crossed).
struct TraceSpan {
  float entry = 0.0F;
  float exit = 0.0F;
  int enter_record = -1;  // the index into the ray's interval records
  int exit_record = -1;
};

// How the two answers compare for one ray.
//
// `unknown` is the **honest** answer when the engine's answer could not be read
// (the call faulted, or the container did not hold the buffer the probe handed it):
// the difference cannot be computed, and computing it over an empty engine list
// would report "agrees" for a trace the engine never answered.
enum class TraceVerdict : std::uint8_t { agrees, engine_only, ours_only, both, unknown };

// The difference between the engine's answer and ours for one ray.
struct TraceDiff {
  std::vector<TraceSpan> engine_only;       // the spans our set has nothing for
  std::vector<TraceOccluderHit> ours_only;  // our solids the engine did not report
  TraceVerdict verdict = TraceVerdict::agrees;
  // How many of the engine's spans our **surfaces** answered for (a mesh slab's two
  // faces) instead of one of our solids covering the span. Published so the two cases
  // stay distinguishable: it means "we have the wall as sheets, not as a volume".
  int spans_bounded_by_surfaces = 0;
  // Whether the engine's answer could be read at all. When it is false the verdict
  // is `unknown` and the two lists are empty: the engine's answer was dropped, and
  // saying "agrees" would be a lie.
  bool engine_answer_usable = true;
};

// The probe's ray: an origin, a unit direction and the endpoints, so the operator
// can check that the probe's ray and the solver's ray are the same 3-D segment.
struct TraceRay {
  cs2::aim::Vec3 start{};
  cs2::aim::Vec3 direction{};  // unit
  cs2::aim::Vec3 end{};
  float depth = 0.0F;  // the ray's length in world units
};

// How the engine is called. The **correct** mode is the call form the RE pinned;
// the other two are deliberately wrong and exist to prove the probe can fail: a
// wrong argument order and a wrong filter must produce a visibly wrong answer or a
// **caught** fault — never a crash (the payload has no exception tables).
enum class TraceAbi : std::uint8_t {
  correct,       // the pinned call form
  swapped,       // the start and the delta are swapped (a wrong argument order)
  static_filter  // the engine's static default filter (the ~0 mask) instead of the bullet's
};

[[nodiscard]] inline auto to_string(TraceAbi abi) noexcept -> const char* {
  switch (abi) {
    case TraceAbi::correct:
      return "correct";
    case TraceAbi::swapped:
      return "swapped";
    case TraceAbi::static_filter:
      return "static_filter";
  }
  return "correct";
}

// Is a fan of `count` rays within the probe's bounds? The command **refuses** a
// fan that is not: a diagnostic must not be able to stall the game.
[[nodiscard]] inline auto trace_fan_size_ok(int count) noexcept -> bool {
  return count >= 1 && count <= TRACE_MAX_RAYS;
}

// Why the probe **refuses** to answer.
//
// The probe's "our answer" is the solver's own `ray_span` over the world's
// occluders, and the world's static body is parsed **once** and cached. When that
// cache is cold:
//
//   * parsing ~10k brush shapes **inside the game's CreateMove hook** is the one
//     thing that could stall the game (the probe runs on the game's main thread,
//     in its input path), and
//   * our own answer would be **empty** anyway — the comparison the probe exists
//     to make would be meaningless.
//
// So the probe refuses: it publishes the refusal and an empty answer, and the
// operator warms the cache first (the heatmap, or the ragebot's own enumeration
// — either populates the same cache).
enum class TraceRefusal : std::uint8_t {
  none,              // the probe answered
  world_cache_cold,  // the world's static body is not parsed yet
};

// The refusal the probe owes the operator, given whether the world's cache was
// warm when it ran. The rule is **pure** so it can be pinned by a host test.
[[nodiscard]] inline auto trace_refusal(bool world_cached) noexcept -> TraceRefusal {
  return world_cached ? TraceRefusal::none : TraceRefusal::world_cache_cold;
}

// The refusal's name, as the answer publishes it (empty when there is none).
[[nodiscard]] inline auto to_string(TraceRefusal refusal) noexcept -> const char* {
  switch (refusal) {
    case TraceRefusal::none:
      return "";
    case TraceRefusal::world_cache_cold:
      return "world_cache_cold";
  }
  return "";
}

// The interval budget: how many of the engine's intervals the probe publishes, and
// whether the engine returned more than the probe publishes (it refuses to publish
// them all and says so).
struct TraceIntervalBudget {
  int published = 0;
  bool overflow = false;
};

[[nodiscard]] inline auto trace_interval_budget(int count) noexcept -> TraceIntervalBudget {
  if (count < 0) {
    return TraceIntervalBudget{};
  }
  return TraceIntervalBudget{.published = count < TRACE_MAX_INTERVALS ? count : TRACE_MAX_INTERVALS,
                             .overflow = count > TRACE_MAX_INTERVALS};
}

// One ray's answer: the engine's intervals, our solids and the difference.
struct TraceRayResult {
  TraceRay ray{};
  bool engine_ok = false;       // the engine call returned (no fault)
  bool engine_faulted = false;  // a guarded fault was recovered
  // The engine wrote **past** the results buffer (the canary tripped): the answer
  // cannot be vouched for, and it is refused like a dropped one.
  bool engine_overrun = false;
  int interval_count = 0;           // the intervals the engine returned
  bool intervals_overflow = false;  // more intervals than the probe publishes
  // Our own list was cut at the per-ray bound: a mesh cluster can put dozens of
  // surfaces on one ray, and a diagnostic must stay bounded. The answer says so
  // instead of silently publishing a short list.
  bool ours_truncated = false;
  // The engine's answer could **not** be read: the call faulted, the container did
  // not hold the buffer the probe handed it (the engine reallocated — which the
  // external-buffer flag forbids), the count was out of range, or the buffer was
  // overrun. The verdict is then `unknown`; it is never computed over an empty
  // engine list.
  bool engine_answer_missing = false;
  std::vector<TraceInterval> intervals;
  std::vector<TraceOccluderHit> ours;
  TraceDiff diff;
};

// The whole probe: the engine's health, our occluder set and the per-ray answers.
struct TraceProbeRun {
  bool ok = false;  // at least one ray's engine call returned
  // The **instrument** failed: a ray's engine call faulted, **or** the engine wrote
  // past the results buffer (the canary tripped). Both mean the answer cannot be
  // trusted — `overrun` says which.
  bool faulted = false;
  // At least one ray's engine wrote past the results buffer.
  bool overrun = false;
  int rays_ok = 0;                   // the rays whose engine call returned
  int rays_faulted = 0;              // the rays whose engine call faulted
  int rays_overrun = 0;              // the rays whose results buffer was overrun
  TraceAbi abi = TraceAbi::correct;  // how the engine was called
  std::uintptr_t iface = 0;
  std::uintptr_t filter = 0;
  std::int64_t filter_mask = 0;
  int filter_group = 0;
  int filter_flags = 0;
  int occluders = 0;  // the solids our enumeration produced
  int occluder_hits = 0;
  // How many of the enumerated solids the probe could **project** (the solver's
  // own projection). Fewer than `occluders` means the projection dropped them —
  // a different failure from "the ray does not cross them".
  int occluders_projected = 0;
  // Whether the enumeration was the per-target pyramid union or the single AABB
  // (`TraceEnum`); the two differ only in which solids they keep, so the
  // difference between their answers localises a selection bug.
  int enum_box = 0;
  // The kept solids (bounded), so a missing wall can be looked up by position.
  std::vector<TraceHullInfo> kept;
  static constexpr int k_max_kept_published = 64;
  // The cached world solids whose AABB **contains the probe's `at` point**, regardless
  // of any pyramid or ray. This is what tells "the solid is not in the cache" apart
  // from "the cache has it and the selection dropped it".
  std::vector<TraceHullInfo> at_point;
  int at_total = 0;
  bool at_queried = false;
  // The first records' raw entity handles (hex), so the answer says **which
  // bodies** the box query returned: `8000` is the worldspawn, anything else is an
  // entity body (a prop, a door, a brush entity).
  std::uint32_t record_handles[8] = {};
  int record_handle_count = 0;
  // The probe's own bounds: how many solids it projects at most, and whether the
  // enumeration produced more than that (the answer is then incomplete).
  int occluder_cap = TRACE_MAX_OCCLUDERS;
  bool occluders_truncated = false;
  // Whether the static world body was already parsed when the probe ran. When it
  // was not, the probe **refuses** to answer (`refusal`): paying the world's parse
  // (10k brush shapes) inside the input hook could stall the game, and our own
  // answer would be empty anyway.
  bool world_cached = false;
  // The probe's refusal, when it refused: `none` when it answered.
  TraceRefusal refusal = TraceRefusal::none;
  // The enumeration behind **our** answer: what the world body parsed into (the
  // shape-type histogram and the mesh clusters the walls are split into), the
  // records/bodies/shapes visited, and the query's own completeness. This is what
  // tells "the mesh was never enumerated" apart from "the mesh is enumerated but
  // does not cover the ray", which the per-ray lists alone cannot.
  OcclusionQueryStats stats;
  std::vector<TraceRayResult> rays;
};

// The verdict's name (the JSON's one-line answer).
[[nodiscard]] inline auto to_string(TraceVerdict verdict) noexcept -> const char* {
  switch (verdict) {
    case TraceVerdict::agrees:
      return "agrees";
    case TraceVerdict::engine_only:
      return "engine_only";
    case TraceVerdict::ours_only:
      return "ours_only";
    case TraceVerdict::both:
      return "both";
    case TraceVerdict::unknown:
      return "unknown";
  }
  return "unknown";
}

// The solid kind's name (a brush hull is a `polytope`; the payload calls it a hull).
[[nodiscard]] inline auto solid_kind_name(cs2::aim::SolidKind kind) noexcept -> const char* {
  switch (kind) {
    case cs2::aim::SolidKind::polytope:
      return "hull";
    case cs2::aim::SolidKind::sphere:
      return "sphere";
    case cs2::aim::SolidKind::capsule:
      return "capsule";
    case cs2::aim::SolidKind::mesh:
      return "mesh";
  }
  return "unknown";
}

// Read one interval record: the fraction at `+0x0C`, the enter/exit byte at `+0x32`
// and the raw bytes. `length` is the ray's length — the fraction is relative to it.
[[nodiscard]] inline auto read_trace_interval(const std::uint8_t* record, float length) noexcept
    -> TraceInterval {
  TraceInterval interval;
  if (record == nullptr) {
    return interval;
  }
  std::memcpy(&interval.fraction, record + TRACE_INTERVAL_FRACTION, sizeof(float));
  interval.distance = interval.fraction * length;
  interval.enter_exit = record[TRACE_INTERVAL_ENTER_EXIT];
  std::memcpy(interval.raw, record, TRACE_INTERVAL_SIZE);
  return interval;
}

// The record's **role**, as the answer publishes it next to the raw byte: `0` is the
// entry (`"enter"`), non-zero is the leave (`"leave"`). It exists so the polarity is
// readable in the answer without knowing it — it was misread once.
[[nodiscard]] inline auto trace_interval_role(std::uint8_t enter_exit) noexcept -> const char* {
  return enter_exit == 0 ? "enter" : "leave";
}

// Pair the interval records into spans: a record with the **entry** byte (`0`) opens a
// span and the next record that leaves a solid closes it (the engine's own consumer
// pairs them the same way — `FUN_180886090` reads the enter/exit byte to do it). The
// pairs are nested (last in, first out), so a ray that enters two solids at once gets
// both spans. A span the ray never leaves ends at the ray's end.
//
// **The polarity is `0` = enter, non-zero = leave.** It was read the other way round
// for the first measurement, which shifted every pair by one record: an exit got
// paired with the *next* solid's entry, producing spans that no geometry can explain
// (`9.6 → 94.8`, and a 619-unit "solid" the bullet could not cross). The recorded ray
// in `payload/control/tests/fixtures/trace-comparison-dust2-wall.json` pins it: our
// hull `266.09 → 273.30` matches the engine's records at `266.02` (byte `0`) and
// `273.03` (byte `1`). See `docs/re/occlusion.md` §"The interval record's polarity".
[[nodiscard]] inline auto pair_trace_spans(const std::vector<TraceInterval>& intervals)
    -> std::vector<TraceSpan> {
  std::vector<TraceSpan> spans;
  std::vector<int> open;
  for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
    const TraceInterval& interval = intervals[static_cast<std::size_t>(i)];
    if (interval.enter_exit == 0) {
      open.push_back(i);
      continue;
    }
    if (open.empty()) {
      continue;  // a leave without an enter: nothing to pair it with
    }
    const int enter = open.back();
    open.pop_back();
    spans.push_back(TraceSpan{.entry = intervals[static_cast<std::size_t>(enter)].distance,
                              .exit = interval.distance,
                              .enter_record = enter,
                              .exit_record = i});
  }
  for (const int enter : open) {
    spans.push_back(TraceSpan{.entry = intervals[static_cast<std::size_t>(enter)].distance,
                              .exit = intervals[static_cast<std::size_t>(enter)].distance,
                              .enter_record = enter,
                              .exit_record = -1});
  }
  return spans;
}

// Do a span and an occluder describe the same solid?
//
// Both answers are measured along the same ray, so the same solid is **entered at
// the same place** and our occluder **covers** the stretch of the ray the engine
// crossed. Both conditions are required:
//
// * our occluder must cover the engine's span — it starts no later than the span
//   (within `tolerance`) and ends no earlier (within `tolerance`). A solid that
//   only covers a *part* of the engine's span is a different solid: it must not be
//   allowed to answer for it. This is what keeps a **real occlusion** from being
//   reported as "agrees" — the engine's wall and our prop 0.5 units in front of it
//   do not cover each other, so the wall stays `engine_only`;
// * the two spans must share a stretch of the ray (the overlap is not empty).
//
// The `tolerance` is the **position** tolerance (world units): our hull is an
// approximation of the engine's shape, so the two entry points need not be
// identical. It is **not** a widening of the overlap test — a solid that merely
// abuts the engine's span does not match it.
[[nodiscard]] inline auto trace_span_matches(const TraceSpan& span, const TraceOccluderHit& hit,
                                             float tolerance) noexcept -> bool {
  if (hit.entry > span.entry + tolerance) {
    return false;  // our solid starts after the engine's span: a different solid
  }
  if (span.exit > hit.exit + tolerance) {
    return false;  // our solid ends before the engine's span: it does not cover it
  }
  return std::min(span.exit, hit.exit) > std::max(span.entry, hit.entry);
}

// The slack under which one of our hits is a **surface** rather than a solid: a
// collision mesh is an open sheet, so `ray_span` reports `entry == exit` for it
// (`docs/re/occlusion.md` §"the measurement").
inline constexpr float TRACE_SURFACE_SLACK = 0.01F;

[[nodiscard]] inline auto trace_hit_is_surface(const TraceOccluderHit& hit) noexcept -> bool {
  return hit.exit - hit.entry <= TRACE_SURFACE_SLACK;
}

// The ordinal of one of our **surfaces** within `tolerance` of `distance`, or -1.
[[nodiscard]] inline auto trace_surface_at(const std::vector<TraceOccluderHit>& ours,
                                           float distance, float tolerance) noexcept -> int {
  for (int i = 0; i < static_cast<int>(ours.size()); ++i) {
    const TraceOccluderHit& hit = ours[static_cast<std::size_t>(i)];
    if (!trace_hit_is_surface(hit)) {
      continue;
    }
    if (std::abs(hit.entry - distance) <= tolerance) {
      return i;
    }
  }
  return -1;
}

// A span the engine reports as a **solid volume** is represented on our side by the
// **two surfaces** that bound it when the world's geometry for it is a collision mesh:
// our parse yields one crossing per face (two zero-thickness occluders), the engine
// yields one enter/exit pair for the volume between them. They are the same wall in two
// representations, so the difference must not call it missing.
//
// The test is deliberately strict: a surface must sit at **each** boundary, within the
// position tolerance. A single face in front of the engine's span is still a different
// solid — that is the case the "a solid must cover the span" rule protects, and this
// one does not weaken it.
//
// `entry_out` / `exit_out` receive the occluders that answered for the two boundaries.
[[nodiscard]] inline auto trace_span_bounded_by_surfaces(const TraceSpan& span,
                                                         const std::vector<TraceOccluderHit>& ours,
                                                         float tolerance, int* entry_out,
                                                         int* exit_out) noexcept -> bool {
  if (entry_out == nullptr || exit_out == nullptr) {
    return false;
  }
  const int entry = trace_surface_at(ours, span.entry, tolerance);
  if (entry < 0) {
    return false;
  }
  const int exit = trace_surface_at(ours, span.exit, tolerance);
  if (exit < 0) {
    return false;
  }
  // A zero-thickness engine span (a sheet the engine reports as a single solid) may be
  // answered by one surface; a volume needs two distinct ones.
  if (entry == exit && span.exit - span.entry > tolerance) {
    return false;
  }
  *entry_out = entry;
  *exit_out = exit;
  return true;
}

// The difference between the engine's intervals and our solids for one ray: the
// spans our set has nothing for, the solids the engine did not report, and the
// one-line verdict.
//
// `engine_answer_usable` says whether the engine's answer could be read at all. When
// it is false the verdict is `unknown` and the lists are empty — the difference
// cannot be computed over an answer the probe never got.
[[nodiscard]] inline auto diff_trace(const std::vector<TraceInterval>& intervals,
                                     const std::vector<TraceOccluderHit>& ours, float tolerance,
                                     bool engine_answer_usable) -> TraceDiff {
  TraceDiff diff;
  diff.engine_answer_usable = engine_answer_usable;
  if (!engine_answer_usable) {
    diff.verdict = TraceVerdict::unknown;
    return diff;
  }
  const std::vector<TraceSpan> spans = pair_trace_spans(intervals);
  std::vector<bool> span_matched(spans.size(), false);
  std::vector<bool> ours_matched(ours.size(), false);
  for (std::size_t s = 0; s < spans.size(); ++s) {
    for (std::size_t o = 0; o < ours.size(); ++o) {
      if (!trace_span_matches(spans[s], ours[o], tolerance)) {
        continue;
      }
      span_matched[s] = true;
      ours_matched[o] = true;
    }
    if (span_matched[s]) {
      continue;
    }
    // The same wall in our representation: the two surfaces that bound the engine's
    // solid volume. Only when no solid of ours covers the span.
    int entry_hit = -1;
    int exit_hit = -1;
    if (!trace_span_bounded_by_surfaces(spans[s], ours, tolerance, &entry_hit, &exit_hit)) {
      continue;
    }
    span_matched[s] = true;
    ours_matched[static_cast<std::size_t>(entry_hit)] = true;
    ours_matched[static_cast<std::size_t>(exit_hit)] = true;
    ++diff.spans_bounded_by_surfaces;
  }
  for (std::size_t s = 0; s < spans.size(); ++s) {
    if (!span_matched[s]) {
      diff.engine_only.push_back(spans[s]);
    }
  }
  for (std::size_t o = 0; o < ours.size(); ++o) {
    if (!ours_matched[o]) {
      diff.ours_only.push_back(ours[o]);
    }
  }
  const bool engine_extra = !diff.engine_only.empty();
  const bool ours_extra = !diff.ours_only.empty();
  if (engine_extra && ours_extra) {
    diff.verdict = TraceVerdict::both;
  } else if (engine_extra) {
    diff.verdict = TraceVerdict::engine_only;
  } else if (ours_extra) {
    diff.verdict = TraceVerdict::ours_only;
  } else {
    diff.verdict = TraceVerdict::agrees;
  }
  return diff;
}

// The answer a **refused** ray publishes.
//
// The probe refuses (see `trace_refusal`) when our own answer would be empty
// anyway, so the ray carries **no** engine intervals and **no** occluders of ours:
// both lists are empty, and the difference is `unknown` — never "agrees" over two
// empty lists. The rule is pure so a host test can pin what a refusal publishes.
[[nodiscard]] inline auto trace_refused_ray(const TraceRay& ray) -> TraceRayResult {
  TraceRayResult result;
  result.ray = ray;
  // The engine was never asked, so its answer is missing.
  result.engine_answer_missing = true;
  result.diff = diff_trace(result.intervals, result.ours, TRACE_MATCH_TOLERANCE, false);
  return result;
}

// The probe's fan: `count` rays around the view ray, in a **documented, deterministic**
// order so two runs compare. Ray 0 is the view ray itself; the rest are the
// golden-angle spiral points of the disc of radius `spread` degrees around it.
//
// The direction comes from the chart's own convention (the absolute world yaw/pitch
// of `docs/ragebot.md`): `yaw`/`pitch` are **degrees**, the Source pitch is positive
// downwards, and the direction is `Basis::unproject` of the chart point — the very
// construction the occluder projection uses, so the probe's ray and the solver's ray
// are the same 3-D segment.
[[nodiscard]] inline auto build_trace_fan(const cs2::aim::Vec3& eye, const cs2::aim::Basis& basis,
                                          float yaw_degrees, float pitch_degrees,
                                          float spread_degrees, int count, float depth)
    -> std::vector<TraceRay> {
  constexpr float k_deg_to_rad = 0.01745329251994329577F;
  constexpr float k_golden_angle = 2.39996322972865332F;  // 137.50776405 degrees
  std::vector<TraceRay> rays;
  const int total = count < 0 ? 0 : (count > TRACE_MAX_RAYS ? TRACE_MAX_RAYS : count);
  rays.reserve(static_cast<std::size_t>(total));
  const float centre_u = yaw_degrees * k_deg_to_rad;
  const float centre_v = -pitch_degrees * k_deg_to_rad;  // the Source pitch is positive down
  const float spread = spread_degrees * k_deg_to_rad;
  for (int i = 0; i < total; ++i) {
    float u = centre_u;
    float v = centre_v;
    if (i > 0) {
      // The golden-angle spiral: `r = spread * sqrt(i / count)`, `theta = i * 137.5°`.
      const float t = static_cast<float>(i) / static_cast<float>(total);
      const float radius = spread * std::sqrt(t);
      const float theta = static_cast<float>(i) * k_golden_angle;
      u = centre_u + radius * std::cos(theta);
      v = centre_v + radius * std::sin(theta);
    }
    const cs2::aim::Vec3 direction = basis.unproject(cs2::aim::Vec2{.x = u, .y = v});
    rays.push_back(TraceRay{
        .start = eye, .direction = direction, .end = eye + direction * depth, .depth = depth});
  }
  return rays;
}

// The pyramid's target box for the probe's rays: the far ends' AABB, **padded**.
//
// The pad matters: a box that is a single point (one ray, no fan) is a degenerate
// hull — the eye→box pyramid has no face planes, and `AabbBvh::query_pyramid`
// keeps **nothing** at all, so the probe's own answer would come back empty. The
// pad is small, so the pyramid stays a thin cone around the ray.
[[nodiscard]] inline auto trace_target_box(const std::vector<TraceRay>& rays, float pad) noexcept
    -> cs2::aim::Aabb {
  cs2::aim::Aabb box{};
  if (rays.empty()) {
    box.min = cs2::aim::Vec3{.x = -pad, .y = -pad, .z = -pad};
    box.max = cs2::aim::Vec3{.x = pad, .y = pad, .z = pad};
    return box;
  }
  cs2::aim::Vec3 lo = rays.front().end;
  cs2::aim::Vec3 hi = lo;
  for (const TraceRay& ray : rays) {
    lo.x = std::min(lo.x, ray.end.x);
    lo.y = std::min(lo.y, ray.end.y);
    lo.z = std::min(lo.z, ray.end.z);
    hi.x = std::max(hi.x, ray.end.x);
    hi.y = std::max(hi.y, ray.end.y);
    hi.z = std::max(hi.z, ray.end.z);
  }
  const cs2::aim::Vec3 margin{.x = pad, .y = pad, .z = pad};
  box.min = lo - margin;
  box.max = hi + margin;
  return box;
}

// The occluder cell for an enumerated solid — the **same** projection the solver's
// heatmap uses (`project_occluder` / `project_sphere_occluder` /
// `project_capsule_occluder` / `project_mesh_occluder`), so the probe's answer is the
// solver's answer and not a re-implementation of it.
[[nodiscard]] inline auto project_occlusion_hull(const cs2::aim::Vec3& eye,
                                                 const cs2::aim::Basis& basis,
                                                 const OcclusionHull& hull)
    -> std::optional<cs2::aim::OccluderCell> {
  if (hull.kind == cs2::aim::SolidKind::sphere) {
    return cs2::aim::project_sphere_occluder(eye, basis, hull.a, hull.radius,
                                             hull.surface_modifier);
  }
  if (hull.kind == cs2::aim::SolidKind::capsule) {
    return cs2::aim::project_capsule_occluder(eye, basis, hull.a, hull.b, hull.radius,
                                              hull.surface_modifier);
  }
  if (hull.kind == cs2::aim::SolidKind::mesh) {
    return cs2::aim::project_mesh_occluder(eye, basis, hull.vertices, hull.triangles,
                                           hull.surface_modifier, hull.mesh_bvh);
  }
  return cs2::aim::project_occluder(eye, basis, hull.vertices, hull.planes, hull.surface_modifier);
}

// The players the probe's own enumeration must skip (a player is a target, not an
// occluder). The enumeration drops a player two ways: the record's **handle index**
// (`handle & 0x7fff`) and the resolved **entity pointer** — so the probe needs
// both lists, and it sets them **itself**, so its answer never depends on the
// solver having run first in the tick.
struct TraceExclude {
  const std::uint16_t* handles = nullptr;  // the entity handle indices
  int handle_count = 0;
  const std::uintptr_t* entities = nullptr;  // the entity pointers
  int entity_count = 0;
};

// The probe: our occluder set's answer and the engine's own answer for the same rays,
// plus the difference. `invoke == false` fills the read-only diagnostics only (no
// engine call). `abi` selects the call form: the pinned one, or one of the
// deliberately wrong ones that prove the probe can fail. The engine call must run
// on a **game thread** (docs/handoff.md: an engine call that reaches tier0's
// allocator faults on the payload's own thread), so the caller runs this from the
// game thread's sink.
//
// `exclude` are the players to skip from the probe's own enumeration.
//
// `box_query` selects the enumeration's volume: `false` is the solver's per-target
// pyramid union (exact, the default), `true` the single eye→far-end AABB. Both are
// conservative supersets of the true occluders, so running the same ray through
// both localises a **selection** bug: a solid the box keeps and the pyramid drops
// is a solid the solver would never see.
[[nodiscard]] auto probe_trace(const std::vector<TraceRay>& rays, bool invoke,
                               TraceAbi abi = TraceAbi::correct,
                               const TraceExclude& exclude = TraceExclude{},
                               bool box_query = false,
                               const cs2::aim::Vec3* at = nullptr,
                               const cs2::aim::Vec3* box_min = nullptr,
                               const cs2::aim::Vec3* box_max = nullptr) -> TraceProbeRun;

// Has the static world body been parsed yet? The probe's enumeration reuses the
// parsed cache; parsing it inside the input hook is the probe's one expensive case
// (it happens once per session), so the answer publishes the state
// (`world_cached`) and the operator can tell a slow first run from the rest.
[[nodiscard]] auto occlusion_world_cache_ready() noexcept -> bool;

}  // namespace cs2::control
