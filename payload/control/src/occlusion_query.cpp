#include "cs2/control/occlusion_query.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

#include "cs2/aim/bvh.hpp"
#include "cs2/diag/crash_handler.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"
#include "offsets.hpp"

namespace cs2::control {
namespace {

using cs2::aim::Vec3;

void mark_incomplete(OcclusionQueryStats* stats, OcclusionQueryIssue issue) {
  if (stats != nullptr) {
    stats->incomplete_reasons |= static_cast<std::uint32_t>(issue);
    stats->complete = false;
  }
}

// client.dll RVAs. They are now resolved by the dumper's curated signatures
// (ADR-0012 — `external/dumper/src/analysis.rs`: `physics_query_interface`,
// `physics_query_filter`, `entities_in_box`) into `cs2/sdk/offsets.hpp`. The literals
// below are the build-`659940c1` fallback used when the SDK was generated without a
// calibration dump (`calibration/README.md`).
constexpr std::uintptr_t k_query_global_fallback = 0x2227058;    // the query interface
constexpr std::uintptr_t k_static_filter_fallback = 0x2227070;   // the engine's filter
constexpr std::uintptr_t k_entities_in_box_fallback = 0x9DBD20;  // EntitiesInBox

// A payload global flag: when set, the world-body enumeration also runs the single-AABB
// query and reports how many hulls the pyramid dropped. POD only. (Scratch containers must
// stay local: a namespace-scope `std::vector` needs CRT dynamic initialization, which the
// manual-map injector cannot resolve — see `docs/handoff.md` gotchas.)
bool g_verify_enum = false;

// A client.dll offset from the generated SDK, or `fallback` when the entry is absent.
// Evaluated per query (a ~20-entry scan, negligible beside the engine query) so the
// payload gains no dynamic or `static` initialiser (which would add a CRT import the
// manual-map injector cannot resolve — see docs/handoff.md gotchas).
[[nodiscard]] auto sdk_client_offset(std::string_view name, std::uintptr_t fallback)
    -> std::uintptr_t {
  for (const cs2::sdk::OffsetInfo& info : cs2::sdk::OFFSETS) {
    if (info.module == "client.dll" && info.name == name) {
      return info.rva;
    }
  }
  return fallback;
}

// Trace/overlap mask. This is the **engine's own bullet mask** (docs/re/bullet-trace.md):
// `FX_FireBullets` builds its filter with `0x1c300b` (`CTraceFilter` init
// `FUN_180352920`), and its trace is `TraceLineIntervals` with that mask. Using the
// same mask keeps the enumerated solid set consistent with the bullets the engine
// actually fires.
//
// NOTE (RE, 2026-10-19, `docs/re/bullet-trace.md` §"The bullet filter"): the value the
// engine's *world query* actually reads is `*(qword*)(filter + 8)`, not the `contents`
// argument — `EntitiesInBox` (`0x1809dbd20`) loads `r13b = filter[0x37]`, and when
// `flags & 7` passes it hands `filter + 8` to the world query at vtable `+0x5d0`
// (`TraceLineIntervals` does the same at `+0x520`, `TraceShape` at `+0x558/0x560`).
// The engine's static default filter (`client+0x2227070`) carries `+8 = 0xffffffff…`
// (all contents), so a payload passing it enumerates a **superset** — including
// `CONTENTS_PLAYERCLIP` brushes the bullet passes through. The custom filter below
// copies the engine object (keeping its vtable, which the query reads far beyond
// `+8`) and writes the bullet's real mask into `+8`.
constexpr int k_bullet_mask = 0x1C300B;
constexpr int k_contents_solid = 0x1C300B;  // the `contents` argument (also the bullet mask)

// The engine filter object (`CTraceFilter`, 0x48 B): vtable @+0, mask @+8, flags
// @+0x37, group @+0x38 — the fields `FUN_180352920` writes (docs/re/bullet-trace.md).
constexpr std::uintptr_t k_filter_size = 0x48;
constexpr std::uintptr_t k_filter_mask = 0x08;
constexpr std::uintptr_t k_filter_flags = 0x37;
constexpr std::uintptr_t k_filter_group = 0x38;
constexpr std::uintptr_t k_filter_byte39 = 0x39;

// Shape layer (`docs/re/vphysics2.md`): shape+0x18 type, +0xC0 hull / +0xC8 mesh.
constexpr std::uintptr_t k_shape_type = 0x18;
constexpr std::uintptr_t k_shape_hull = 0xC0;
// Sphere (type 0) / capsule (type 1) geometry, resolved from the shape constructors
// and their vtable methods on `vphysics2.dll` `1d768045…` (2026-10-17):
//   sphere : centre (Vec3) at +0xB8, radius at +0xC4
//   capsule: endpoint A at +0xB8, endpoint B at +0xC4 (each Vec3), radius at +0xD0
// The constructor already multiplies these by the shape's own scale, so no scale
// read is needed. The engine's own point tests read exactly these offsets, e.g.
// `FUN_18024ca60` (sphere) and `FUN_18024faa0` (capsule).
constexpr std::uintptr_t k_shape_sphere_center = 0xB8;
constexpr std::uintptr_t k_shape_sphere_radius = 0xC4;
constexpr std::uintptr_t k_shape_capsule_a = 0xB8;
constexpr std::uintptr_t k_shape_capsule_b = 0xC4;
constexpr std::uintptr_t k_shape_capsule_radius = 0xD0;
// Mesh (type 3): `RnMesh_t*` at `shape+0xC8`, per-shape scale (Vec3) at `+0xB8`. In
// `RnMesh_t` (`docs/re/vphysics2.md`, from `RnMeshClone`/`RnMeshUpdateChangedVertices`):
// vertex count `+0x30` and vertices `+0x38` (stride `0xC`); triangle count `+0x18`;
// the triangle **vertex indices** (3 int32 per triangle) at `+0x50` — verified live
// (`docs/re/bullet-trace.md`): the map's large/merged wall surfaces are mesh shapes,
// so they must be read, not skipped.
constexpr std::uintptr_t k_shape_mesh = 0xC8;
constexpr std::uintptr_t k_shape_mesh_scale = 0xB8;
constexpr std::uintptr_t k_mesh_vertex_count = 0x30;
constexpr std::uintptr_t k_mesh_vertices = 0x38;
constexpr std::uintptr_t k_mesh_tri_count = 0x18;
constexpr std::uintptr_t k_mesh_indices = 0x50;
// The exact ray test intersects a mesh's triangles (a mesh is not convex), so the
// triangle count is bounded for the one-time level parse. A larger mesh is split into
// spatial **clusters** (below) rather than skipped: the world's largest collision mesh
// on de_dust2 has ~101k vertices, and the previous 65536 cap silently dropped it —
// which is exactly why a wall/ground surface it carries was absent from the occluders
// (live-confirmed 2026-10-19, `docs/re/bullet-trace.md`).
constexpr std::int32_t k_max_mesh_vertices = 1 << 20;
constexpr std::int32_t k_max_mesh_triangles = 1 << 20;
// A collision mesh is emitted as several spatially bounded clusters of at most this
// many triangles, so a map-wide mesh becomes many small occluders (bounded chart
// silhouettes, cheap `ray_span`) instead of one chart-wide hull that fragments and
// stalls the solve. `k_max_mesh_clusters` bounds the work per mesh.
constexpr std::int32_t k_mesh_cluster_triangles = 2048;
constexpr int k_max_mesh_clusters = 128;
constexpr std::uintptr_t k_hull_vertex_count = 0x70;
constexpr std::uintptr_t k_hull_vertices = 0x78;
constexpr std::uintptr_t k_vertex_stride = 0xC;
// `RnHull_t` faces: plane count @+0x88, `RnPlane_t*` @+0x90, stride 0x10
// (normal.xyz + dist) — `docs/re/vphysics2.md`. The interior is `dot(n, p) <= d`.
constexpr std::uintptr_t k_hull_plane_count = 0x88;
constexpr std::uintptr_t k_hull_planes = 0x90;
constexpr std::uintptr_t k_plane_stride = 0x10;
// `RnHull_t` AABB header (`docs/re/vphysics2.md`): min @+0x14, max @+0x20. Read once
// (in world space, i.e. when the body transform is identity) so the per-query broad
// phase does not recompute the bounds from the vertex array.
constexpr std::uintptr_t k_hull_aabb_min = 0x14;
constexpr std::uintptr_t k_hull_aabb_max = 0x20;
// Compound pool (`RnCompoundClone`/`RnCompoundDestroy`, `docs/re/vphysics2.md`):
// the `RnHull_t` array is at `comp+0xF0` (count) / `+0xF8` (block), stride `0xF8`; the
// `RnMesh_t` array at `comp+0x28` / `+0x30`, stride `0xC0`.
constexpr std::uintptr_t k_compound_hull_count = 0xF0;
constexpr std::uintptr_t k_compound_hull_block = 0xF8;
constexpr std::uintptr_t k_compound_hull_stride = 0xF8;
constexpr std::uintptr_t k_compound_mesh_count = 0x28;
constexpr std::uintptr_t k_compound_mesh_block = 0x30;
constexpr std::uintptr_t k_compound_mesh_stride = 0xC0;
constexpr std::int32_t k_max_compound_parts = 512;

// Per-shape surface (`CGameSurfaceProperties`, docs/re/penetration.md). Each
// `IPhysicsShape` holds a pointer to its material-properties object at `+0x48`; that
// object's `+0x10` is the index into the game's `SurfacePropertiesList` array
// (`*(client + DAT_18254e100) + 0x28`, stride 0x20 — the engine's resolver
// `FUN_1809e3560` returns `*(int*)(obj+0x10) * 0x20 + [list+0x28]`). The record:
//   +0x00 maxspeedfactor, +0x04 jumpfactor,
//   +0x08 bulletPenetrationDistanceModifier (float),
//   +0x0C bulletPenetrationDamageModifier   (float),
//   +0x14 gamematerial                      (int16, CHAR_TEX_*).
// Live-verified (2026-10-18): the world body's brushes resolve to 29 distinct
// surfaces (concrete 0.5, metal 0.4, wood 0.9, glass 0.99, …).
constexpr std::uintptr_t k_shape_surface = 0x48;
constexpr std::uintptr_t k_surface_object_index = 0x10;
constexpr std::uintptr_t k_surface_list_fallback = 0x254E100;  // DAT_18254e100
constexpr std::uintptr_t k_surface_list_array = 0x28;
constexpr std::uintptr_t k_surface_stride = 0x20;

// Overlap record (0x70 B, docs/re/occlusion.md): transform blob @+0x00, handle
// @+0x18 (= `body->vtbl[0x478]()`), material token @+0x50
// (= `*(hit->vtbl[0x30]() + 0x28)`), the `CPhysicsBody*` @+0x58
// (= `hit->vtbl[0x10]()`), @+0x60 = 0. Verified by decompiling the record build
// (`ConvertOverlapMultiple` `FUN_1809d2dc0`, client.dll 659940c1).
constexpr std::uintptr_t k_record_stride = 0x70;
constexpr std::uintptr_t k_record_handle = 0x18;
constexpr std::uintptr_t k_record_material = 0x50;
// Compound (type 4) probe offsets (`docs/re/vphysics2.md` §"Shape geometry offsets"):
// `shape+0xC0` = `RnCompound_t*`; `shape+0xD0` = the 0x30-byte instance-record count.
// `RnCompound_t` holds typed geometry **by value**: meshes (stride `0xC0`) at `+0x28`,
// hulls (`RnHull_t`, stride `0xF8`) at `+0xF0`, and further groups at `+0x100`/`+0x110`
// (stride `0x10`) / `+0x120` (stride `0x1C`).
constexpr std::uintptr_t k_shape_compound = 0xC0;
constexpr std::uintptr_t k_compound_instances = 0xD0;
constexpr std::uintptr_t k_compound_meshes = 0x28;
constexpr std::uintptr_t k_compound_hulls = 0xF0;
constexpr std::uintptr_t k_compound_g100 = 0x100;
constexpr std::uintptr_t k_compound_g110 = 0x110;
constexpr std::uintptr_t k_compound_g120 = 0x120;

// The chain the record actually carries (docs/re/occlusion.md §"The record → shape
// chain", derived live 2026-10-17): `record + 0x58` is a `CPhysicsBody*` (its vtable
// lives in vphysics2.dll). From a body:
//   body + 8   -> the body's data block
//   data + 0x60 -> shape count (int)
//   data + 0x68 -> the shape pointer array (inline when the count <= 1, else a
//                  pointer to the allocation — this is a `CUtlVector` with inline
//                  capacity 1)
//   body vtable slot 0x1b0 -> the body's world transform (a 3x4 matrix built from
//                  its position/quaternion), written to a caller buffer
// Each shape is a vphysics2 `IPhysicsShape`: `+0x18` type (2 = hull), `+0xC0` the
// `RnHull_t*` for a hull.
constexpr std::uintptr_t k_record_body = 0x58;
constexpr std::uintptr_t k_body_data = 0x08;
constexpr std::uintptr_t k_body_shape_count = 0x60;
// The shape array: `data + 0x68` holds the pointer when the vector spilled (count
// > 1), else the inline single element. Validated live: the world body's
// `*(data + 0x68)` yields the 10149 brush `IPhysicsShape`s (type 2).
constexpr std::uintptr_t k_body_shapes_ptr = 0x68;
// `CPhysicsBody::vtable[0x1b0]` = the body's world transform.
constexpr std::uintptr_t k_body_transform_slot = 0x1B0;
// The world body holds one shape per map brush (~10k on de_dust2).
constexpr int k_max_body_shapes = 32768;
constexpr int k_max_bodies = 256;        // unique overlap bodies walked per query
constexpr int k_max_query_targets = 64;
constexpr int k_max_query_exclude = 64;
constexpr int k_large_body_shapes = 64;  // above this = a static world body (cached)
// Hulls projected into the chart per query. The view volume can bring in ~1–2k world
// brushes; a low cap silently drops an arbitrary subset (e.g. the wall you are facing)
// so the geometry is drawn but never attenuates. Keep this well above the realistic
// world-in-view count.
constexpr int k_max_occluders = 8192;
// The results CUtlVector is backed by our static buffer and flagged external, so the
// engine must never need to grow it: size it generously (the view volume can overlap
// several thousand world brushes). 8192 * 0x70 = 896 KiB of POD.
constexpr int k_max_records = 8192;

// ---- the overlap filter (docs/re/occlusion.md) -----------------------------
// `EntitiesInBox` -> `FilterOverlapResults` keeps the worldspawn (`0x8000`)
// unconditionally and otherwise resolves the record's entity and calls the filter's
// `ShouldHitEntity` (vtable slot `+8`). The engine's *static default* filter
// (`client+0x2227070`) is `mov al,1; ret` — it accepts **every** entity — so a
// player's collision body (a target, not a solid) comes back as an occluder.
//
// A payload-built filter is not safe: the engine reads **more** of the filter vtable
// than slot `+8`, so a minimal one makes the query return nothing (and the game
// crashes). We keep the engine's filter and drop the players here, resolving each
// record's `+0x18` handle exactly as `FilterOverlapResults` does (the paged identity
// table) and comparing the entity to the players' pawns.
std::uintptr_t g_exclude_pawns[64] = {};
int g_exclude_pawn_count = 0;

// The engine's static default filter object, byte-copied and re-masked to the bullet
// mask (see `k_bullet_mask`): the copy keeps the engine's vtable (which the query reads
// far past `+8`), so it is safe where a payload-built vtable is not. POD only.
alignas(16) unsigned char g_bullet_filter[k_filter_size] = {};
bool g_bullet_filter_ready = false;
// Hulls whose `RnHull_t` plane array could not be read; their half-spaces are rebuilt
// from the vertices so they still attenuate (a silhouette without planes is drawn but
// never occludes).
int g_hulls_no_planes = 0;
// Collision-mesh clusters emitted across the process (a big mesh is split into several
// so no map-wide mesh is dropped and no chart-wide silhouette is built).
int g_mesh_clusters = 0;

// The game's resolved `SurfacePropertiesList` array (docs/re/penetration.md), set
// once per query from `*(client + DAT_18254e100) + 0x28`. POD only.
std::uintptr_t g_surface_array = 0;

// The first shape surface ever resolved, so the probe survives the world-body cache
// (the world is parsed once; later queries parse nothing).
int g_surface_probe_set = 0;
int g_surface_probe_index = -1;
float g_surface_probe_distance = 0.0F;
int g_surface_probe_material = -1;

// A shape's real surface: the game-surface penetration modifiers + material.
struct ShapeSurface {
  float distance = 1.0F;  // bulletPenetrationDistanceModifier
  float damage = 1.0F;    // bulletPenetrationDamageModifier
  int material = -1;      // gamematerial (CHAR_TEX_*)
  bool grate = false;     // CONTENTS_GRATE span
};

// `CGameSurfaceProperties` (`docs/re/penetration.md`, 0x20 B): the fields the KV
// parser `FUN_1809cd960` fills and `FUN_1808a1820` reads.
struct SurfaceRecord {
  float max_speed_factor = 0.0F;
  float jump_factor = 0.0F;
  float distance_modifier = 0.0F;
  float damage_modifier = 0.0F;
  std::uint32_t material_tag = 0;
  std::int16_t material = -1;
  std::int16_t sound_material = 0;
  std::uint8_t flags[8] = {};
};
static_assert(sizeof(SurfaceRecord) == 0x20, "CGameSurfaceProperties is 0x20 bytes");

// Read a shape's real surface (`shape+0x48` -> material object -> `+0x10` index ->
// `SurfacePropertiesList[index]`). Neutral defaults on any validation failure, so a
// bad read can never fabricate a penetrating wall (the query is marked incomplete and
// rage fails closed). A real zero distance modifier is valid and means impenetrable.
auto read_shape_surface(std::uintptr_t shape, OcclusionQueryStats* stats) -> ShapeSurface {
  ShapeSurface surface;
  if (g_surface_array < 0x10000) {
    mark_incomplete(stats, OcclusionQueryIssue::surface_metadata);
    return surface;
  }
  std::uintptr_t material_object = 0;
  if (!cs2::game::read(shape + k_shape_surface, material_object) || material_object < 0x10000) {
    mark_incomplete(stats, OcclusionQueryIssue::surface_metadata);
    return surface;
  }
  std::int32_t index = 0;
  if (!cs2::game::read(material_object + k_surface_object_index, index) || index < 0 ||
      index > 65535) {
    mark_incomplete(stats, OcclusionQueryIssue::surface_metadata);
    return surface;
  }
  SurfaceRecord record{};
  if (!cs2::game::read(g_surface_array + static_cast<std::uintptr_t>(index) * k_surface_stride,
                       record)) {
    mark_incomplete(stats, OcclusionQueryIssue::surface_metadata);
    return surface;
  }
  if (!(record.distance_modifier >= 0.0F) || !(record.distance_modifier <= 1000.0F) ||
      record.material < 0) {
    mark_incomplete(stats, OcclusionQueryIssue::surface_metadata);
    return surface;
  }
  surface.distance = record.distance_modifier;
  surface.damage = record.damage_modifier;
  surface.material = record.material;
  if (g_surface_probe_set == 0) {
    g_surface_probe_set = 1;
    g_surface_probe_index = index;
    g_surface_probe_distance = record.distance_modifier;
    g_surface_probe_material = record.material;
  }
  if (stats != nullptr) {
    if (stats->surface_hits == 0) {
      stats->surface_index = index;
      stats->surface_distance = record.distance_modifier;
      stats->surface_material = record.material;
    }
    ++stats->surface_hits;
  }
  return surface;
}

// Stamp a solid with its shape's surface.
void apply_surface(OcclusionHull& solid, const ShapeSurface& surface) {
  solid.surface_modifier = surface.distance;
  solid.damage_modifier = surface.damage;
  solid.material = surface.material;
  solid.grate = surface.grate;
}

// Resolve an entity handle (`index & 0x7fff`) via the paged identity table
// (`EntitySystem` layout) to its entity instance pointer. 0 when unresolved.
auto resolve_entity(std::uintptr_t blocks, std::uint32_t handle) -> std::uintptr_t {
  if (blocks < 0x10000 || handle == 0 || handle == 0xffffffffU) {
    return 0;
  }
  const std::uint32_t index = handle & 0x7fffU;
  std::uintptr_t page_base = 0;
  if (!cs2::game::read(blocks + static_cast<std::uintptr_t>(index >> 9) * 8, page_base) ||
      page_base < 0x10000) {
    return 0;
  }
  std::uintptr_t entity = 0;
  if (!cs2::game::read(page_base + static_cast<std::uintptr_t>(index & 0x1ff) * 0x70, entity)) {
    return 0;
  }
  return entity;
}

// CUtlVector header: count @+0x00, data pointer @+0x10 (see ResultVector below).

using EntitiesInBoxFn = int (*)(void*, const Vec3*, const Vec3*, void*, int, void*, int);
EntitiesInBoxFn g_entities_in_box = nullptr;

// `CUtlVector<OverlapRecord>` header (docs/re/occlusion.md): count @+0x00,
// data pointer @+0x10, capacity @+0x18, flags @+0x1C. We pre-allocate the buffer
// and set `flags` bit31 exactly as the game's own caller does, so the engine never
// reaches its allocator (the earlier fault was inside tier0's allocation path).
struct ResultVector {
  int count = 0;
  int pad1 = 0;
  int pad2 = 0;
  int pad3 = 0;
  void* data = nullptr;
  int capacity = 0;
  unsigned flags = 0;
};
static_assert(sizeof(ResultVector) == 0x20, "CUtlVector header is 0x20 bytes");

alignas(16) unsigned char g_record_buffer[k_max_records * k_record_stride] = {};

struct BoxCall {
  void* iface = nullptr;
  void* filter = nullptr;
  Vec3 lo{};
  Vec3 hi{};
  int contents = 0;
  int result = 0;
  ResultVector* vector = nullptr;
};

void invoke_box(void* context) {
  auto* call = static_cast<BoxCall*>(context);
  if (g_entities_in_box != nullptr) {
    call->result = g_entities_in_box(call->iface, &call->lo, &call->hi, call->filter,
                                     call->contents, call->vector, 0);
  }
}

// `body->vtbl[0x1b0]` writes 8 floats: `position(x,y,z), 1.0, quaternion(x,y,z,w)`
// (the write pattern is `CONCAT44(z, 1.0f)` for floats[2..3], so the rotation is
// floats[4..7]). `v_world = position + R(quat) * v_local`.
struct BodyTransform {
  Vec3 position{};
  float quat[4] = {0.0F, 0.0F, 0.0F, 1.0F};  // x, y, z, w

  [[nodiscard]] auto apply(const Vec3& v) const -> Vec3 {
    const float x = quat[0];
    const float y = quat[1];
    const float z = quat[2];
    const float w = quat[3];
    const Vec3 rotated{
        (1 - 2 * (y * y + z * z)) * v.x + 2 * (x * y - z * w) * v.y + 2 * (x * z + y * w) * v.z,
        2 * (x * y + z * w) * v.x + (1 - 2 * (x * x + z * z)) * v.y + 2 * (y * z - x * w) * v.z,
        2 * (x * z - y * w) * v.x + 2 * (y * z + x * w) * v.y + (1 - 2 * (x * x + y * y)) * v.z};
    return Vec3{rotated.x + position.x, rotated.y + position.y, rotated.z + position.z};
  }

  // A body transform that is the identity (`v_world == v_local`), so geometry stored
  // in the shape's own frame is already world-space (the static world body).
  [[nodiscard]] auto is_identity() const -> bool {
    return position.x == 0.0F && position.y == 0.0F && position.z == 0.0F && quat[0] == 0.0F &&
           quat[1] == 0.0F && quat[2] == 0.0F && quat[3] == 1.0F;
  }
};

// `body->vtbl[0x1b0](body, out)`: fill 16 floats with the body's world transform.
// `CPhysicsBody::vtable[0x1b0](body, out)` fills 8 floats with the body's world
// transform (`position(x,y,z), 1.0, quaternion(x,y,z,w)`). Validated live.
struct TransformCall {
  void* object = nullptr;
  float* out = nullptr;
  void* result = nullptr;
};

void invoke_transform(void* context) {
  auto* call = static_cast<TransformCall*>(context);
  if (call->object == nullptr) {
    return;
  }
  auto** vtable = *reinterpret_cast<void***>(call->object);
  if (vtable == nullptr) {
    return;
  }
  auto fn = reinterpret_cast<void* (*)(void*, void*)>(vtable[k_body_transform_slot / 8]);
  if (fn != nullptr) {
    call->result = fn(call->object, call->out);
  }
}

// Read the body's world transform (guarded). `out` keeps the identity on failure.
auto read_transform(std::uintptr_t object, BodyTransform& out) -> bool {
  float raw[8] = {};
  TransformCall call;
  call.object = reinterpret_cast<void*>(object);
  call.out = raw;
  if (!cs2::diag::call_guarded(&invoke_transform, &call)) {
    return false;
  }
  out.position = Vec3{raw[0], raw[1], raw[2]};
  out.quat[0] = raw[4];
  out.quat[1] = raw[5];
  out.quat[2] = raw[6];
  out.quat[3] = raw[7];
  return true;
}

auto parse_hull(std::uintptr_t hull, const BodyTransform& transform,
                std::vector<OcclusionHull>& out, const ShapeSurface& surface,
                OcclusionQueryStats* stats) -> bool {
  std::int32_t count = 0;
  std::uintptr_t vertices = 0;
  if (!cs2::game::read(hull + k_hull_vertex_count, count) || count < 3 || count > 65536) {
    mark_incomplete(stats, count > 65536 ? OcclusionQueryIssue::parser_limit
                                         : OcclusionQueryIssue::parser_failure);
    return false;
  }
  if (!cs2::game::read(hull + k_hull_vertices, vertices) || vertices == 0) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return false;
  }
  std::vector<Vec3> points;
  points.reserve(static_cast<std::size_t>(count));
  for (std::int32_t i = 0; i < count; ++i) {
    Vec3 point{};
    if (!cs2::game::read(vertices + static_cast<std::uintptr_t>(i) * k_vertex_stride, point)) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      break;
    }
    points.push_back(transform.apply(point));
  }
  if (points.size() < 3) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return false;
  }
  // The hull's planes: the exact test for "is this solid between the eye and a
  // cell?" (`OccluderCell::ray_span`), so the floor underfoot and walls behind the
  // target are excluded by geometry rather than by a bounding-box heuristic.
  std::vector<cs2::aim::OccluderPlane> planes;
  std::int32_t plane_count = 0;
  std::uintptr_t plane_array = 0;
  if (cs2::game::read(hull + k_hull_plane_count, plane_count) && plane_count > 0 &&
      plane_count <= 1024 && cs2::game::read(hull + k_hull_planes, plane_array) &&
      plane_array >= 0x10000) {
    planes.reserve(static_cast<std::size_t>(plane_count));
    for (std::int32_t i = 0; i < plane_count; ++i) {
      struct RawPlane {
        Vec3 normal;
        float dist;
      } raw{};
      if (!cs2::game::read(plane_array + static_cast<std::uintptr_t>(i) * k_plane_stride, raw)) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
        break;
      }
      planes.push_back(cs2::aim::OccluderPlane{raw.normal, raw.dist});
    }
  }
  if (planes.empty() && points.size() >= 4) {
    // The `RnHull_t` plane array could not be read (offset/build drift). Rebuild the
    // hull's half-spaces from its own vertices so it still attenuates: a silhouette
    // with no planes is projected and split cells, but `ray_span` returns nullopt, so
    // the wall is drawn and *never* occludes (`docs/re/occlusion.md`).
    planes = cs2::aim::convex_hull_3d(points);
    ++g_hulls_no_planes;
  }
  if (planes.empty()) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
  }
  OcclusionHull solid;
  solid.kind = cs2::aim::SolidKind::polytope;
  solid.vertices = std::move(points);
  solid.planes = std::move(planes);
  apply_surface(solid, surface);
  // Cache the hull's own AABB (world-space when the body transform is identity) so
  // the per-query broad phase never re-scans the vertex array.
  if (transform.is_identity()) {
    Vec3 mn{};
    Vec3 mx{};
    if (cs2::game::read(hull + k_hull_aabb_min, mn) &&
        cs2::game::read(hull + k_hull_aabb_max, mx)) {
      solid.bounds_min = mn;
      solid.bounds_max = mx;
      solid.has_bounds = true;
    }
  }
  out.push_back(std::move(solid));
  return true;
}

// Split a triangle soup into spatially bounded clusters, appending one
// `SolidKind::mesh` occluder per cluster (docs/re/bullet-trace.md). A collision mesh
// can be map-wide (the largest on de_dust2 is ~101k vertices); one chart-wide convex
// silhouette for it fragments every hitbox and makes `ray_span` scan the whole soup,
// so it is split by the widest axis at the median until each cluster is small. Each
// cluster carries its own world AABB (cached for the per-query broad phase when the
// body transform is identity — the static world).
auto emit_mesh_clusters(std::vector<std::array<Vec3, 3>>& tris, bool transform_identity,
                        std::vector<OcclusionHull>& out, const ShapeSurface& surface) -> bool {
  if (tris.empty()) {
    return true;
  }
  struct Range {
    int begin = 0;
    int end = 0;
  };
  std::vector<Range> stack;
  stack.push_back(Range{0, static_cast<int>(tris.size())});
  int emitted = 0;
  bool complete = true;
  while (!stack.empty() && emitted < k_max_mesh_clusters) {
    const Range range = stack.back();
    stack.pop_back();
    const int count = range.end - range.begin;
    if (count <= 0) {
      continue;
    }
    if (count <= k_mesh_cluster_triangles ||
        static_cast<int>(stack.size()) + emitted + 2 > k_max_mesh_clusters) {
      OcclusionHull solid;
      solid.kind = cs2::aim::SolidKind::mesh;
      solid.triangles.reserve(static_cast<std::size_t>(count) * 3);
      Vec3 mn = tris[static_cast<std::size_t>(range.begin)][0];
      Vec3 mx = mn;
      for (int i = range.begin; i < range.end; ++i) {
        for (const Vec3& p : tris[static_cast<std::size_t>(i)]) {
          solid.triangles.push_back(p);
          mn.x = std::min(mn.x, p.x);
          mn.y = std::min(mn.y, p.y);
          mn.z = std::min(mn.z, p.z);
          mx.x = std::max(mx.x, p.x);
          mx.y = std::max(mx.y, p.y);
          mx.z = std::max(mx.z, p.z);
        }
      }
      // The projection only needs the silhouette's point set, and the chart silhouette is
      // the **2-D convex hull** of the projected points. Projecting all `count*3` triangle
      // vertices to rebuild that hull every tick was the ragebot's dominant cost once the
      // meshes were cached (`docs/ragebot-perf.md` §3.9): the AABB corners span the same
      // convex hull (a superset — the mesh is inside its own bounds), so the broad phase is
      // unchanged while the projection drops from thousands of points to eight. The exact
      // thickness still comes from `ray_span` over `triangles`/`mesh_bvh`.
      solid.vertices = {Vec3{mn.x, mn.y, mn.z}, Vec3{mx.x, mn.y, mn.z},
                        Vec3{mx.x, mx.y, mn.z}, Vec3{mn.x, mx.y, mn.z},
                        Vec3{mn.x, mn.y, mx.z}, Vec3{mx.x, mn.y, mx.z},
                        Vec3{mx.x, mx.y, mx.z}, Vec3{mn.x, mx.y, mx.z}};
      // Build the triangle BVH once, here (the clusters are cached per level), so
      // `ray_span` is O(log n) rather than O(triangles) (`docs/ragebot-perf.md` §3.7).
      solid.mesh_bvh =
          std::make_shared<const cs2::aim::MeshBvh>(cs2::aim::build_mesh_bvh(solid.triangles));
      if (transform_identity) {
        solid.bounds_min = mn;
        solid.bounds_max = mx;
        solid.has_bounds = true;
      }
      apply_surface(solid, surface);
      out.push_back(std::move(solid));
      ++emitted;
      ++g_mesh_clusters;
      continue;
    }
    // The widest axis of the range's vertex bounds.
    Vec3 mn = tris[static_cast<std::size_t>(range.begin)][0];
    Vec3 mx = mn;
    for (int i = range.begin; i < range.end; ++i) {
      for (const Vec3& p : tris[static_cast<std::size_t>(i)]) {
        mn.x = std::min(mn.x, p.x);
        mn.y = std::min(mn.y, p.y);
        mn.z = std::min(mn.z, p.z);
        mx.x = std::max(mx.x, p.x);
        mx.y = std::max(mx.y, p.y);
        mx.z = std::max(mx.z, p.z);
      }
    }
    const float ex = mx.x - mn.x;
    const float ey = mx.y - mn.y;
    const float ez = mx.z - mn.z;
    int axis = 0;
    if (ey > ex && ey >= ez) {
      axis = 1;
    } else if (ez > ex && ez > ey) {
      axis = 2;
    }
    const auto centroid = [axis](const std::array<Vec3, 3>& t) -> float {
      const float c = axis == 0 ? t[0].x + t[1].x + t[2].x
                                : (axis == 1 ? t[0].y + t[1].y + t[2].y : t[0].z + t[1].z + t[2].z);
      return c * (1.0F / 3.0F);
    };
    const auto pivot_pos = static_cast<std::ptrdiff_t>((range.begin + range.end) / 2);
    std::nth_element(tris.begin() + range.begin, tris.begin() + pivot_pos, tris.begin() + range.end,
                     [&centroid](const std::array<Vec3, 3>& a, const std::array<Vec3, 3>& b) {
                       return centroid(a) < centroid(b);
                     });
    const int pivot_index = static_cast<int>(pivot_pos);
    if (pivot_index <= range.begin || pivot_index >= range.end) {
      complete = false;
      continue;  // degenerate split; drop the range rather than loop forever
    }
    stack.push_back(Range{range.begin, pivot_index});
    stack.push_back(Range{pivot_index, range.end});
  }
  return complete && stack.empty();
}

// Read an `RnMesh_t` (a triangle soup; docs/re/vphysics2.md), split it into clusters
// and transform each to world space. `scale` is the shape's uniform scale (a Vec3),
// `transform` the owning body's world transform.
auto parse_mesh(std::uintptr_t mesh, const Vec3& scale, const BodyTransform& transform,
                std::vector<OcclusionHull>& out, const ShapeSurface& surface,
                OcclusionQueryStats* stats) -> bool {
  std::int32_t count = 0;
  std::uintptr_t vertices = 0;
  if (!cs2::game::read(mesh + k_mesh_vertex_count, count) || count < 3 ||
      count > k_max_mesh_vertices || !cs2::game::read(mesh + k_mesh_vertices, vertices) ||
      vertices < 0x10000) {
    mark_incomplete(stats, count > k_max_mesh_vertices ? OcclusionQueryIssue::parser_limit
                                                       : OcclusionQueryIssue::parser_failure);
    return false;
  }
  std::int32_t tri_count = 0;
  std::uintptr_t indices = 0;
  if (!cs2::game::read(mesh + k_mesh_tri_count, tri_count) || tri_count <= 0 ||
      tri_count > k_max_mesh_triangles || !cs2::game::read(mesh + k_mesh_indices, indices) ||
      indices < 0x10000) {
    mark_incomplete(stats, tri_count > k_max_mesh_triangles ? OcclusionQueryIssue::parser_limit
                                                            : OcclusionQueryIssue::parser_failure);
    return false;
  }
  // Batch-read the vertex array (one syscall) then the triangle indices.
  std::vector<Vec3> points(static_cast<std::size_t>(count));
  if (!cs2::game::read_bytes(vertices, points.data(),
                             static_cast<std::size_t>(count) * k_vertex_stride)) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return false;
  }
  for (Vec3& point : points) {
    const Vec3 scaled{point.x * scale.x, point.y * scale.y, point.z * scale.z};
    point = transform.apply(scaled);
  }
  std::vector<std::int32_t> tri_indices(static_cast<std::size_t>(tri_count) * 3);
  if (!cs2::game::read_bytes(indices, tri_indices.data(),
                             static_cast<std::size_t>(tri_count) * 3 * sizeof(std::int32_t))) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return false;
  }
  std::vector<std::array<Vec3, 3>> tris;
  tris.reserve(static_cast<std::size_t>(tri_count));
  for (std::size_t t = 0; t + 2 < tri_indices.size(); t += 3) {
    const std::int32_t ia = tri_indices[t];
    const std::int32_t ib = tri_indices[t + 1];
    const std::int32_t ic = tri_indices[t + 2];
    if (ia < 0 || ia >= count || ib < 0 || ib >= count || ic < 0 || ic >= count) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      continue;
    }
    tris.push_back(std::array<Vec3, 3>{points[static_cast<std::size_t>(ia)],
                                       points[static_cast<std::size_t>(ib)],
                                       points[static_cast<std::size_t>(ic)]});
  }
  if (tris.empty()) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return false;
  }
  const int before = static_cast<int>(out.size());
  if (!emit_mesh_clusters(tris, transform.is_identity(), out, surface)) {
    mark_incomplete(stats, OcclusionQueryIssue::mesh_cluster_limit);
  }
  return static_cast<int>(out.size()) > before;
}

auto parse_shape(std::uintptr_t shape, const BodyTransform& transform,
                 std::vector<OcclusionHull>& out, int& shapes, int* type_counts,
                 OcclusionQueryStats* stats) -> bool {
  if (shape == 0) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return false;
  }
  std::int32_t type = -1;
  if (!cs2::game::read(shape + k_shape_type, type)) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return false;
  }
  ++shapes;
  if (type_counts != nullptr && type >= 0 && type < 8) {
    ++type_counts[type];
  }
  // The shape's real surface (docs/re/penetration.md). Read once per shape; every
  // solid it yields is stamped with it.
  const ShapeSurface surface = read_shape_surface(shape, stats);
  switch (type) {
    case 2: {  // GENERIC_SHAPE_HULL
      std::uintptr_t hull = 0;
      if (!cs2::game::read(shape + k_shape_hull, hull) || hull == 0) {
        return false;
      }
      const bool parsed = parse_hull(hull, transform, out, surface, stats);
      if (!parsed) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      }
      return parsed;
    }
    case 0: {  // GENERIC_SHAPE_SPHERE
      Vec3 center{};
      float radius = 0.0F;
      if (!cs2::game::read(shape + k_shape_sphere_center, center) ||
          !cs2::game::read(shape + k_shape_sphere_radius, radius) || !(radius > 0.0F)) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
        return false;
      }
      OcclusionHull solid;
      solid.kind = cs2::aim::SolidKind::sphere;
      solid.a = transform.apply(center);
      solid.radius = radius;
      apply_surface(solid, surface);
      out.push_back(std::move(solid));
      return true;
    }
    case 1: {  // GENERIC_SHAPE_CAPSULE
      Vec3 a{};
      Vec3 b{};
      float radius = 0.0F;
      if (!cs2::game::read(shape + k_shape_capsule_a, a) ||
          !cs2::game::read(shape + k_shape_capsule_b, b) ||
          !cs2::game::read(shape + k_shape_capsule_radius, radius) || !(radius > 0.0F)) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
        return false;
      }
      OcclusionHull solid;
      solid.kind = cs2::aim::SolidKind::capsule;
      solid.a = transform.apply(a);
      solid.b = transform.apply(b);
      solid.radius = radius;
      apply_surface(solid, surface);
      out.push_back(std::move(solid));
      return true;
    }
    case 3: {  // GENERIC_SHAPE_MESH — a triangle soup; the map's large walls.
      std::uintptr_t mesh = 0;
      if (!cs2::game::read(shape + k_shape_mesh, mesh) || mesh == 0) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
        return false;
      }
      Vec3 scale{1.0F, 1.0F, 1.0F};
      if (!cs2::game::read(shape + k_shape_mesh_scale, scale)) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      }
      return parse_mesh(mesh, scale, transform, out, surface, stats);
    }
    case 4: {  // GENERIC_SHAPE_COMPOUND — the pool of sub-shapes (docs/re/vphysics2.md)
      std::uintptr_t compound = 0;
      if (!cs2::game::read(shape + k_shape_compound, compound) || compound < 0x10000) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
        return false;
      }
      // The instance records and the g100/g110/g120 child pools are unresolved. Even
      // though hull and mesh pools are parsed below, this shape cannot be called complete.
      mark_incomplete(stats, OcclusionQueryIssue::unsupported_compound);
      // RE probe (docs/re/vphysics2.md): the first type-4 shape's group counts.
      if (stats != nullptr && stats->compound == 0) {
        stats->compound = compound;
        (void)cs2::game::read(shape + k_compound_instances, stats->compound_instances);
        (void)cs2::game::read(compound + k_compound_meshes, stats->compound_meshes);
        (void)cs2::game::read(compound + k_compound_hulls, stats->compound_hulls);
        (void)cs2::game::read(compound + k_compound_g100, stats->compound_g100);
        (void)cs2::game::read(compound + k_compound_g110, stats->compound_g110);
        (void)cs2::game::read(compound + k_compound_g120, stats->compound_g120);
      }
      // Recurse into the pool: the compound stores its `RnHull_t`/`RnMesh_t` children
      // **by value** (`RnCompoundClone`/`RnCompoundDestroy`, `docs/re/vphysics2.md`),
      // so a child's address is `block + i*stride`. The children are shape-local, so
      // they are transformed by the owning body's transform (identity for the world).
      // NOTE: CS2 gameplay props are multi-hull *bodies* (many type-2 shapes), not
      // compound shapes — no live type-4 was found to validate this — so every read is
      // bounds-checked and a bad pool contributes nothing.
      bool any = false;
      std::int32_t hull_count = 0;
      std::uintptr_t hull_block = 0;
      if (cs2::game::read(compound + k_compound_hull_count, hull_count) && hull_count > 0 &&
          hull_count <= k_max_compound_parts &&
          cs2::game::read(compound + k_compound_hull_block, hull_block) && hull_block >= 0x10000) {
        for (std::int32_t i = 0; i < hull_count; ++i) {
          if (parse_hull(hull_block + static_cast<std::uintptr_t>(i) * k_compound_hull_stride,
                         transform, out, surface, stats)) {
            any = true;
          } else {
            mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
          }
        }
      } else if (hull_count > k_max_compound_parts) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_limit);
      }
      std::int32_t mesh_count = 0;
      std::uintptr_t mesh_block = 0;
      if (cs2::game::read(compound + k_compound_mesh_count, mesh_count) && mesh_count > 0 &&
          mesh_count <= k_max_compound_parts &&
          cs2::game::read(compound + k_compound_mesh_block, mesh_block) && mesh_block >= 0x10000) {
        const Vec3 scale{1.0F, 1.0F, 1.0F};
        for (std::int32_t i = 0; i < mesh_count; ++i) {
          if (parse_mesh(mesh_block + static_cast<std::uintptr_t>(i) * k_compound_mesh_stride,
                         scale, transform, out, surface, stats)) {
            any = true;
          }
        }
      } else if (mesh_count > k_max_compound_parts) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_limit);
      }
      if (!any) {
        mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      }
      return any;
    }
    default:
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      return false;
  }
}

// Copy the engine's static filter object at `engine` (an in-process VA) and write the
// bullet mask/group/flags into it, returning the payload-side object. Falls back to the
// engine object when the copy fails. The engine's own vtable is preserved by the copy.
auto build_bullet_filter(std::uintptr_t engine) -> void* {
  g_bullet_filter_ready = false;
  if (engine < 0x10000 || !cs2::game::read_bytes(engine, g_bullet_filter, k_filter_size)) {
    return reinterpret_cast<void*>(engine);
  }
  *reinterpret_cast<std::int64_t*>(g_bullet_filter + k_filter_mask) =
      static_cast<std::int64_t>(k_bullet_mask);
  g_bullet_filter[k_filter_group] = 3;     // the shot's collision group (FUN_180352920)
  g_bullet_filter[k_filter_flags] = 0x0F;  // the engine default's query-selection flags
  g_bullet_filter[k_filter_byte39] =
      static_cast<unsigned char>((g_bullet_filter[k_filter_byte39] & 0xC9U) | 0x49U);
  g_bullet_filter_ready = true;
  return g_bullet_filter;
}

// One `EntitiesInBox` call. Returns the record count (0 on fault/empty).
auto run_box_query(void* iface, void* filter, const Vec3& lo, const Vec3& hi, ResultVector& vector,
                   bool* ok) -> std::int32_t {
  BoxCall call;
  call.iface = iface;
  call.filter = filter;
  call.contents = k_contents_solid;
  call.vector = &vector;
  call.lo = lo;
  call.hi = hi;
  *ok = cs2::diag::call_guarded(&invoke_box, &call);
  return *ok ? vector.count : 0;
}

// Hulls of the large (static world) body, parsed once and reused — the map geometry
// never changes within a level, and walking ~10k brush shapes every tick is far too
// slow for the per-frame occlusion refresh. POD only: a namespace-scope `std::vector`
// would need CRT dynamic initialisation, which a manually mapped payload may not run.
// Keyed on the body AND the level's geometry identity (its data block + first shape
// pointer), so a map change that reuses the body allocation cannot serve stale
// brush hulls.
struct WorldHullCache {
  std::uintptr_t body = 0;
  std::uintptr_t data = 0;
  std::uintptr_t first_shape = 0;
  OcclusionHull* hulls = nullptr;
  int count = 0;
  std::uint32_t incomplete_reasons = 0;
};
WorldHullCache g_world_cache;

// The world AABB of a solid (for the query filter). Handles the analytic primitives,
// whose `vertices` are empty.
void hull_bounds(const OcclusionHull& hull, Vec3& mn, Vec3& mx) {
  if (hull.kind == cs2::aim::SolidKind::sphere) {
    const Vec3 r{hull.radius, hull.radius, hull.radius};
    mn = hull.a - r;
    mx = hull.a + r;
    return;
  }
  if (hull.kind == cs2::aim::SolidKind::capsule) {
    const float r = hull.radius;
    mn = Vec3{std::min(hull.a.x, hull.b.x) - r, std::min(hull.a.y, hull.b.y) - r,
              std::min(hull.a.z, hull.b.z) - r};
    mx = Vec3{std::max(hull.a.x, hull.b.x) + r, std::max(hull.a.y, hull.b.y) + r,
              std::max(hull.a.z, hull.b.z) + r};
    return;
  }
  if (hull.vertices.empty() && !hull.has_bounds) {
    mn = Vec3{};
    mx = Vec3{};
    return;
  }
  if (hull.has_bounds) {
    mn = hull.bounds_min;
    mx = hull.bounds_max;
    return;
  }
  mn = hull.vertices.front();
  mx = mn;
  for (const Vec3& v : hull.vertices) {
    mn.x = std::min(mn.x, v.x);
    mn.y = std::min(mn.y, v.y);
    mn.z = std::min(mn.z, v.z);
    mx.x = std::max(mx.x, v.x);
    mx.y = std::max(mx.y, v.y);
    mx.z = std::max(mx.z, v.z);
  }
}

// The world-hull AABB BVH (`docs/ragebot-perf.md` §3.7, the "missing BVH"): built once
// when the world cache is (re)built (once per level for the static world), so the
// per-query box overlap is O(log n + k) instead of walking every hull. `aim::AabbBvh`
// is pure + host-tested (`aim-bvh`).
cs2::aim::AabbBvh g_world_bvh;

// Walk one `CPhysicsBody`'s shapes and append each hull's world-space vertices.
// Small bodies (entities/props) are walked every call; large bodies (the static
// world, ~10k brush shapes) are parsed once into `g_large_cache` and then copied
// (only the hulls intersecting the query box, capped).
void walk_body(std::uintptr_t body, const Vec3& eye, const Vec3& lo, const Vec3& hi,
               std::vector<OcclusionHull>& out, int& shapes, int* type_counts,
               OcclusionQueryStats* stats, bool collect, const cs2::aim::Aabb* targets,
               int target_count) {
  std::uintptr_t data = 0;
  if (!cs2::game::read(body + k_body_data, data) || data < 0x10000) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return;
  }
  std::int32_t count = 0;
  if (!cs2::game::read(data + k_body_shape_count, count)) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return;
  }
  if (count <= 0) {
    return;
  }
  if (count > k_max_body_shapes) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_limit);
    return;
  }
  if (stats != nullptr && collect) {
    ++stats->bodies;
  }

  std::uintptr_t array = data + k_body_shapes_ptr;
  if (count > 1) {
    if (!cs2::game::read(data + k_body_shapes_ptr, array)) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      return;
    }
  }
  if (array < 0x10000) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    return;
  }

  if (count > k_large_body_shapes) {
    // Static world geometry: parse once, reuse. Its hull vertices are already in
    // world space, so no transform is applied. Rebuild the cache when the level's
    // geometry identity changes (a reused body pointer is not enough on its own).
    std::uintptr_t first_shape = 0;
    (void)cs2::game::read(array, first_shape);
    if (g_world_cache.body != body || g_world_cache.data != data ||
        g_world_cache.first_shape != first_shape) {
      delete[] g_world_cache.hulls;
      g_world_cache.hulls = nullptr;
      g_world_cache.count = 0;
      g_world_cache.incomplete_reasons = 0;
      g_world_cache.body = body;
      g_world_cache.data = data;
      g_world_cache.first_shape = first_shape;
    }
    if (g_world_cache.count == 0) {
      std::vector<OcclusionHull> parsed;
      OcclusionQueryStats cache_stats;
      for (int j = 0; j < count; ++j) {
        std::uintptr_t shape = 0;
        if (!cs2::game::read(array + static_cast<std::uintptr_t>(j) * 8, shape) ||
            shape < 0x10000) {
          mark_incomplete(&cache_stats, OcclusionQueryIssue::parser_failure);
          continue;
        }
        if (!parse_shape(shape, BodyTransform{}, parsed, shapes, type_counts, &cache_stats)) {
          mark_incomplete(&cache_stats, OcclusionQueryIssue::parser_failure);
        }
      }
      g_world_cache.incomplete_reasons = cache_stats.incomplete_reasons;
      if (stats != nullptr) {
        stats->surface_hits += cache_stats.surface_hits;
        if (stats->surface_index < 0 && cache_stats.surface_index >= 0) {
          stats->surface_index = cache_stats.surface_index;
          stats->surface_distance = cache_stats.surface_distance;
          stats->surface_material = cache_stats.surface_material;
        }
        if (stats->compound == 0 && cache_stats.compound != 0) {
          stats->compound = cache_stats.compound;
          stats->compound_instances = cache_stats.compound_instances;
          stats->compound_meshes = cache_stats.compound_meshes;
          stats->compound_hulls = cache_stats.compound_hulls;
          stats->compound_g100 = cache_stats.compound_g100;
          stats->compound_g110 = cache_stats.compound_g110;
          stats->compound_g120 = cache_stats.compound_g120;
        }
      }
      g_world_cache.count = static_cast<int>(parsed.size());
      g_world_cache.hulls = new OcclusionHull[parsed.size()];
      for (std::size_t i = 0; i < parsed.size(); ++i) {
        g_world_cache.hulls[i] = std::move(parsed[i]);
      }
      std::vector<cs2::aim::Aabb> boxes(static_cast<std::size_t>(g_world_cache.count));
      for (int i = 0; i < g_world_cache.count; ++i) {
        hull_bounds(g_world_cache.hulls[i], boxes[static_cast<std::size_t>(i)].min,
                    boxes[static_cast<std::size_t>(i)].max);
      }
      g_world_bvh.build(std::move(boxes));
    }
    if (g_world_cache.incomplete_reasons != 0 && stats != nullptr) {
      stats->incomplete_reasons |= g_world_cache.incomplete_reasons;
      stats->complete = false;
    }
    if (stats != nullptr && collect) {
      stats->world_cache_body = g_world_cache.body;
      stats->world_cache_hulls = g_world_cache.count;
    }
    int pushed = 0;
    // BVH query: only the hulls that can lie between the eye and a target, in
    // O(log n + k) — the enumeration no longer walks all ~10k world hulls
    // (`docs/ragebot-perf.md` §3.7). With per-target boxes it is the union of the
    // eye→target pyramids, which is exact (a conservative superset of the solids that can
    // occlude) and far tighter than the single eye→all-targets box: one distant enemy
    // otherwise makes the box span the whole map (`docs/ragebot-perf.md` §2.1).
    std::vector<int> hits;
    hits.reserve(1024);
    std::vector<int> owner;  // per-cached-hull target: -1 none, >=0 one target, -2 shared
    if (targets != nullptr && target_count > 0) {
      // Owner per cached hull: `t` when only one target's pyramid contains it, `-2` when
      // several do (then it is shared and applies to all of them).
      owner.assign(static_cast<std::size_t>(g_world_cache.count), -1);
      std::vector<int> target_hits;
      for (int t = 0; t < target_count; ++t) {
        target_hits.clear();
        // The pyramid's apex is the **eye**, not the query box corner: a solid occludes a
        // hitbox only if it meets a segment from the eye into that hitbox.
        g_world_bvh.query_pyramid(eye, targets[t], target_hits);
        for (const int index : target_hits) {
          hits.push_back(index);
          const int previous = owner[static_cast<std::size_t>(index)];
          owner[static_cast<std::size_t>(index)] =
              previous == -1 ? t : (previous == t ? t : -2);
        }
      }
      // The per-target queries overlap, so deduplicate before projecting.
      std::sort(hits.begin(), hits.end());
      hits.erase(std::unique(hits.begin(), hits.end()), hits.end());
      if (g_verify_enum && stats != nullptr) {
        // Diagnostic: run the single-AABB query on the same tick. The AABB volume
        // contains the pyramid union, so every pyramid hull must also be in the AABB set.
        std::vector<int> box_hits;
        g_world_bvh.query(cs2::aim::Aabb{lo, hi}, box_hits);
        std::sort(box_hits.begin(), box_hits.end());
        stats->enum_box_kept += static_cast<int>(box_hits.size());
        stats->enum_pyramid_kept += static_cast<int>(hits.size());
        for (const int index : hits) {
          if (!std::binary_search(box_hits.begin(), box_hits.end(), index)) {
            ++stats->enum_outside_box;
          }
        }
        // The decisive check: a hull the box keeps, the pyramid dropped, and that *is*
        // inside an eye→target pyramid would be a real occlusion lost. It must be 0.
        for (const int index : box_hits) {
          if (std::binary_search(hits.begin(), hits.end(), index)) {
            continue;
          }
          Vec3 hlo{};
          Vec3 hhi{};
          hull_bounds(g_world_cache.hulls[index], hlo, hhi);
          for (int t = 0; t < target_count; ++t) {
            if (cs2::aim::AabbBvh::aabb_intersects_pyramid(eye, targets[t],
                                                           cs2::aim::Aabb{hlo, hhi})) {
              ++stats->enum_box_only_between;
              break;
            }
          }
        }
      }
    } else {
      g_world_bvh.query(cs2::aim::Aabb{lo, hi}, hits);
    }
    for (const int index : hits) {
      const int owner_target = owner.empty() ? -1 : owner[static_cast<std::size_t>(index)];
      out.push_back(g_world_cache.hulls[index]);
      if (owner_target == -2) {
        out.back().target = -1;  // shared by several targets: applies to all
      } else if (owner_target >= 0) {
        out.back().target = owner_target;
      }
      if (stats != nullptr && collect) {
        ++stats->world_box_kept;
      }
      if (++pushed >= k_max_occluders) {
        if (hits.size() > static_cast<std::size_t>(pushed)) {
          mark_incomplete(stats, OcclusionQueryIssue::occluder_capacity);
        }
        break;
      }
    }
    if (stats != nullptr && collect) {
      stats->world_box_rejected += g_world_cache.count - static_cast<int>(hits.size());
    }
    return;
  }

  // Entity/prop bodies: hull vertices are body-local, so apply the body's own world
  // transform (`CPhysicsBody::vtable[0x1b0]`, validated live).
  BodyTransform transform;
  const bool have_transform = read_transform(body, transform);
  if (!have_transform) {
    mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
  }
  for (int j = 0; j < count; ++j) {
    std::uintptr_t shape = 0;
    if (!cs2::game::read(array + static_cast<std::uintptr_t>(j) * 8, shape) || shape < 0x10000) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      continue;
    }
    if (!parse_shape(shape, have_transform ? transform : BodyTransform{}, out, shapes, type_counts,
                     stats)) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
    }
  }
}

// Walk a record array: for each record, follow `+0x58` → `CPhysicsBody`, then walk
// each unique body once (the worldspawn records all share one body). A record whose
// `+0x18` handle index (`handle & 0x7fff`) is in `exclude` is skipped — those are the
// players, which are targets, not occluders (the worldspawn `0x8000` is always kept).
void walk_records(std::uintptr_t data, std::int32_t count, const Vec3& eye, const Vec3& lo,
                  const Vec3& hi, std::vector<OcclusionHull>& out, OcclusionQueryStats* stats,
                  int& shapes, int* type_counts, bool collect, std::uintptr_t entity_blocks,
                  const std::uint16_t* exclude, int exclude_count, const cs2::aim::Aabb* targets,
                  int target_count) {
  std::uintptr_t bodies[k_max_bodies] = {};
  int body_count = 0;

  constexpr int k_record_scan_limit = 4096;
  const int limit = std::min<std::int32_t>(count, k_record_scan_limit);
  if (count > k_record_scan_limit) {
    mark_incomplete(stats, OcclusionQueryIssue::record_scan_limit);
  }
  for (int i = 0; i < limit; ++i) {
    const std::uintptr_t record = data + static_cast<std::uintptr_t>(i) * k_record_stride;

    // One-record RE probe (docs/re/occlusion.md): the material token's type is still
    // unresolved, so surface it for a live run to settle.
    std::uint32_t handle = 0;
    if (!cs2::game::read(record + k_record_handle, handle)) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      continue;
    }
    if (stats != nullptr) {
      ++stats->material_records;
      if (i < 1) {
        std::uintptr_t token = 0;
        if (cs2::game::read(record + k_record_material, token)) {
          stats->material_token = token;
        }
      }
      if (stats->record_handle_count < 8) {
        stats->record_handles[stats->record_handle_count++] = handle;
      }
    }

    // Drop a player's collision body (a target, not a solid). The worldspawn
    // (`0x8000`) is always kept; otherwise resolve the handle exactly as
    // `FilterOverlapResults` does (the paged identity table) and compare the entity to
    // the players' pawns, with the handle-index set as a fallback.
    if (handle != 0x8000U) {
      bool is_player = false;
      if (g_exclude_pawn_count > 0) {
        const std::uintptr_t entity = resolve_entity(entity_blocks, handle);
        for (int e = 0; e < g_exclude_pawn_count && entity != 0; ++e) {
          if (g_exclude_pawns[e] == entity) {
            is_player = true;
            break;
          }
        }
      }
      if (!is_player && exclude != nullptr) {
        const auto index = static_cast<std::uint16_t>(handle & 0x7fffU);
        for (int e = 0; e < exclude_count; ++e) {
          if (exclude[e] == index) {
            is_player = true;
            break;
          }
        }
      }
      if (is_player) {
        if (stats != nullptr) {
          ++stats->excluded_records;
        }
        continue;
      }
    }

    std::uintptr_t body = 0;
    if (!cs2::game::read(record + k_record_body, body) || body < 0x10000) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      continue;
    }
    bool seen = false;
    for (int b = 0; b < body_count; ++b) {
      if (bodies[b] == body) {
        seen = true;
        break;
      }
    }
    if (!seen) {
      if (body_count < k_max_bodies) {
        bodies[body_count++] = body;
      } else {
        mark_incomplete(stats, OcclusionQueryIssue::body_capacity);
      }
    }
  }

  std::vector<OcclusionHull> sink;  // absorbs hulls for non-collecting probes
  std::vector<OcclusionHull>& target = collect ? out : sink;
  int* const counts = collect ? type_counts : nullptr;
  for (int b = 0; b < body_count; ++b) {
    walk_body(bodies[b], eye, lo, hi, target, shapes, counts, stats, collect, targets,
              target_count);
  }
}

}  // namespace

void set_occlusion_exclude_pawns(const std::uintptr_t* pawns, int count) noexcept {
  g_exclude_pawn_count = 0;
  if (pawns == nullptr) {
    return;
  }
  for (int i = 0; i < count && g_exclude_pawn_count < 64; ++i) {
    if (pawns[i] != 0) {
      g_exclude_pawns[g_exclude_pawn_count++] = pawns[i];
    }
  }
}

auto query_world_occluders(const Vec3& from, const Vec3& box_min, const Vec3& box_max, bool invoke,
                           const std::uint16_t* exclude, int exclude_count,
                           const cs2::aim::Aabb* targets, int target_count, bool verify_enum)
    -> OcclusionQueryResult {
  OcclusionQueryResult result;
  OcclusionQueryStats* const stats = &result.stats;
  g_verify_enum = verify_enum;
  if (target_count < 0 || target_count > k_max_query_targets ||
      (target_count > 0 && targets == nullptr)) {
    mark_incomplete(stats, OcclusionQueryIssue::target_capacity);
    target_count = std::clamp(target_count, 0, k_max_query_targets);
    if (targets == nullptr) {
      target_count = 0;
    }
  }
  if (exclude_count < 0 || exclude_count > k_max_query_exclude ||
      (exclude_count > 0 && exclude == nullptr)) {
    mark_incomplete(stats, OcclusionQueryIssue::target_capacity);
    exclude_count = std::clamp(exclude_count, 0, k_max_query_exclude);
    if (exclude == nullptr) {
      exclude_count = 0;
    }
  }
  stats->enum_targets = target_count;
  for (int i = 0; i < target_count; ++i) {
    const Vec3 centre{(targets[i].min.x + targets[i].max.x) * 0.5F,
                      (targets[i].min.y + targets[i].max.y) * 0.5F,
                      (targets[i].min.z + targets[i].max.z) * 0.5F};
    const Vec3 delta{centre.x - from.x, centre.y - from.y, centre.z - from.z};
    const float distance = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
    stats->enum_target_max_distance = std::max(stats->enum_target_max_distance, distance);
  }
  if (!invoke) {
    mark_incomplete(stats, OcclusionQueryIssue::not_invoked);
  }

  const cs2::game::ModuleInfo client = cs2::game::find_module("client.dll");
  if (!client.valid()) {
    if (invoke) {
      mark_incomplete(stats, OcclusionQueryIssue::query_unavailable);
    }
    return result;
  }
  const std::uintptr_t base = client.base;
  const std::uintptr_t query_global =
      sdk_client_offset("physics_query_interface", k_query_global_fallback);
  const std::uintptr_t static_filter =
      sdk_client_offset("physics_query_filter", k_static_filter_fallback);
  const std::uintptr_t entities_in_box =
      sdk_client_offset("entities_in_box", k_entities_in_box_fallback);
  // The paged entity identity table (`EntitySystem`), used to resolve a record's
  // handle to its entity so the players' collision bodies can be dropped.
  std::uintptr_t entity_blocks = 0;
  (void)cs2::game::read(base + sdk_client_offset("entity_list", 0x236FA70), entity_blocks);

  // The game's surface-properties list (docs/re/penetration.md): resolved once so a
  // shape's material object can be turned into its real penetration modifiers.
  g_surface_array = 0;
  std::uintptr_t surface_list = 0;
  if (cs2::game::read(base + sdk_client_offset("surface_properties_list", k_surface_list_fallback),
                      surface_list) &&
      surface_list >= 0x10000) {
    (void)cs2::game::read(surface_list + k_surface_list_array, g_surface_array);
  }
  if (g_surface_array < 0x10000) {
    g_surface_array = 0;
  }
  stats->surface_array = g_surface_array;

  std::uintptr_t iface = 0;
  if (!cs2::game::read(base + query_global, iface) || iface == 0) {
    if (invoke) {
      mark_incomplete(stats, OcclusionQueryIssue::query_unavailable);
    }
    return result;
  }
  stats->iface = iface;
  stats->filter = base + static_filter;
  std::uintptr_t world = 0;
  std::uintptr_t filter_vtable = 0;
  (void)cs2::game::read(iface, world);
  (void)cs2::game::read(base + static_filter, filter_vtable);
  stats->world = world;
  stats->filter_vtable = filter_vtable;
  if (!invoke) {
    return result;  // diagnostics only
  }
  stats->complete = true;

  g_entities_in_box = reinterpret_cast<EntitiesInBoxFn>(base + entities_in_box);

  // The custom bullet filter (see `k_bullet_mask`): a copy of the engine's filter with
  // the shot's collision mask, so the enumerated set matches what a bullet crosses.
  void* filter = build_bullet_filter(base + static_filter);
  stats->filter = reinterpret_cast<std::uintptr_t>(filter);
  stats->filter_custom = g_bullet_filter_ready;
  if (g_bullet_filter_ready) {
    stats->filter_mask = *reinterpret_cast<std::int64_t*>(g_bullet_filter + k_filter_mask);
    stats->filter_group = g_bullet_filter[k_filter_group];
    stats->filter_flags = g_bullet_filter[k_filter_flags];
  } else {
    mark_incomplete(stats, OcclusionQueryIssue::query_unavailable);
  }

  int shapes = 0;
  int* type_counts = stats->type_counts;

  auto do_query = [&](const Vec3& lo, const Vec3& hi, bool collect) {
    ResultVector vector;
    vector.data = g_record_buffer;
    vector.capacity = k_max_records;
    vector.flags = 0x80000000U;  // external buffer: the engine will not reallocate
    bool ok = false;
    const std::int32_t count =
        run_box_query(reinterpret_cast<void*>(iface), filter, lo, hi, vector, &ok);
    stats->ok = ok;
    stats->faulted = !ok;
    stats->records = count;
    if (!ok) {
      mark_incomplete(stats, OcclusionQueryIssue::query_fault);
      return;
    }
    if (count < 0) {
      mark_incomplete(stats, OcclusionQueryIssue::parser_failure);
      return;
    }
    if (count >= k_max_records) {
      mark_incomplete(stats, OcclusionQueryIssue::record_capacity);
    }
    if (count > k_max_records || vector.data != g_record_buffer) {
      mark_incomplete(stats, OcclusionQueryIssue::record_capacity);
      return;
    }
    if (!ok || count <= 0 || vector.data == nullptr) {
      return;
    }
    const auto data = reinterpret_cast<std::uintptr_t>(vector.data);
    walk_records(data, count, from, lo, hi, result.hulls, stats, shapes, type_counts, collect,
                 entity_blocks, exclude, exclude_count, targets, target_count);
  };

  // The box spans the eye and every target hitbox (the caller's AABB), plus the
  // requested 8-unit padding. It is built from world positions only — never the camera
  // angles — so the enumerated solid set is stable while the player looks around.
  // Which solids actually occlude is decided per cell by depth in `build_heatmap`.
  constexpr float margin = 8.0F;
  const Vec3 lo{box_min.x - margin, box_min.y - margin, box_min.z - margin};
  const Vec3 hi{box_max.x + margin, box_max.y + margin, box_max.z + margin};
  do_query(lo, hi, true);

  if (stats != nullptr) {
    stats->shapes = shapes;
    stats->hulls = static_cast<int>(result.hulls.size());
    stats->hulls_no_planes = g_hulls_no_planes;
    stats->mesh_clusters = g_mesh_clusters;
    // The surface probe persists across queries: the world body is parsed only on the
    // first query, so a cached query has no `surface_hits` of its own.
    if (stats->surface_hits == 0 && g_surface_probe_set != 0) {
      stats->surface_index = g_surface_probe_index;
      stats->surface_distance = g_surface_probe_distance;
      stats->surface_material = g_surface_probe_material;
    }
  }
  stats->complete = stats->incomplete_reasons == 0;
  return result;
}

}  // namespace cs2::control
