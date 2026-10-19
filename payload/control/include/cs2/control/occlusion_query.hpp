#pragma once

// World-solids enumeration for the occlusion heatmap (docs/re/occlusion.md).
//
// Calls `CGamePhysicsQueryInterface::EntitiesInBox` once for the box spanning the
// eye→target segment and parses the returned overlap records into world-space
// convex hulls, which `aim::project_occluder` then projects into the chart. The
// engine call is wrapped in `diag::call_guarded`, so a wrong ABI faults back here
// instead of killing the process (the payload has no exception tables).

#include <cstdint>
#include <vector>

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
  cs2::aim::Vec3 a{};                           // sphere: centre; capsule: endpoint a; mesh AABB min
  cs2::aim::Vec3 b{};                           // capsule: endpoint b; mesh AABB max
  float radius = 0.0F;                          // sphere / capsule radius
  // The solid's world-space AABB, from the `RnHull_t` header (`+0x14`/`+0x20`) when
  // available — cached so the per-query broad phase is O(1) per solid instead of
  // O(vertices) (`docs/re/vphysics2.md` P1.7).
  cs2::aim::Vec3 bounds_min{};
  cs2::aim::Vec3 bounds_max{};
  bool has_bounds = false;
  // The shape's real surface (`CGameSurfaceProperties`, docs/re/penetration.md).
  float surface_modifier = 1.0F;  // bulletPenetrationDistanceModifier
  float damage_modifier = 1.0F;   // bulletPenetrationDamageModifier
  int material = -1;              // gamematerial (CHAR_TEX_*)
  bool grate = false;             // CONTENTS_GRATE span
};

struct OcclusionQueryStats {
  bool ok = false;       // the engine query returned (no fault)
  bool faulted = false;  // a guarded fault was recovered
  int records = 0;       // overlap records the query returned
  int bodies = 0;        // records whose +0x58 resolved to a CPhysicsBody
  int shapes = 0;        // collision shapes visited
  int hulls = 0;         // convex hulls parsed
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
  // The first records' raw `+0x18` handles (hex), for the player-exclusion check.
  std::uint32_t record_handles[8] = {};
  int record_handle_count = 0;
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
[[nodiscard]] auto query_world_occluders(const cs2::aim::Vec3& from, const cs2::aim::Vec3& box_min,
                                         const cs2::aim::Vec3& box_max, bool invoke,
                                         OcclusionQueryStats* stats,
                                         const std::uint16_t* exclude = nullptr,
                                         int exclude_count = 0) -> std::vector<OcclusionHull>;

}  // namespace cs2::control
