#include "cs2/aim/reader.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "cs2/game/memory.hpp"

namespace cs2::aim {
namespace {

// The head box half-extent, in units, for the collision-AABB *fallback* only
// (used when the real studio hitbox set cannot be read).
constexpr float k_head_half = 4.0F;

// The hitgroup id of the head (`HITGROUP_HEAD`). Only the head has a
// non-identity damage multiplier.
constexpr int k_hitgroup_head = 1;

// --- scene graph / model (`docs/re/aim.md`) --------------------------------
constexpr std::uintptr_t k_scene_node = 0x330;        // C_BaseEntity::m_pGameSceneNode
constexpr std::uintptr_t k_model_state = 0x140;       // CSkeletonInstance::m_modelState
constexpr std::uintptr_t k_bone_array = 0x80;         // CModelState::m_pBoneArray
constexpr std::uintptr_t k_model_handle = 0xA0;       // CModelState::m_hModel (StrongHandle)
constexpr std::uintptr_t k_hitbox_set_index = 0x3FC;  // CSkeletonInstance::m_nHitboxSet
// `CSkeletonInstance + 0x348` = `CModelState::m_MeshGroupMask` (`modelState + 0x208`):
// the active mesh-group mask, intersected with each mesh's mask to pick a mesh.
constexpr std::uintptr_t k_mesh_group_mask = 0x348;

// --- CModel → render meshes → hitbox sets → boxes (verified live) ----------
constexpr std::uintptr_t k_mesh_count = 0x70;  // model -> mesh count
constexpr std::uintptr_t k_mesh_array = 0x78;  // model -> mesh array (ptr per mesh)
constexpr std::uintptr_t k_mesh_mask = 0x90;   // model -> per-mesh enable mask (uint64[])
constexpr std::uintptr_t k_set_flags = 0x164;  // mesh -> hitbox-set flags (bit31 = data present)
constexpr std::uintptr_t k_set_data = 0x168;   // mesh -> CHitBoxSet* array
constexpr std::uintptr_t k_set_count = 0x174;  // mesh -> hitbox-set count
constexpr std::uintptr_t k_set_entry_stride = 0x48;
constexpr std::uintptr_t k_set_entry_boxes = 0x28;  // hitbox count
constexpr std::uintptr_t k_set_entry_array = 0x30;  // CHitBox* array
constexpr std::uintptr_t k_box_stride = 0x70;       // sizeof(CHitBox)
constexpr std::uintptr_t k_box_bone_name = 0x10;    // m_sBoneName (CUtlString)
constexpr std::uintptr_t k_box_mins = 0x18;         // m_vMinBounds (capsule endpoint A)
constexpr std::uintptr_t k_box_maxs = 0x24;         // m_vMaxBounds (capsule endpoint B)
constexpr std::uintptr_t k_box_radius = 0x30;       // m_flShapeRadius
constexpr std::uintptr_t k_box_group = 0x38;        // m_nGroupId (the hitgroup id)
constexpr std::uintptr_t k_box_shape = 0x3C;        // m_nShapeType
constexpr std::uintptr_t k_box_index = 0x48;        // m_nHitBoxIndex

// `m_nShapeType`: a player hitbox is a capsule (`docs/re/aim.md`). Any other
// shape is not modelled here.
constexpr std::uint8_t k_shape_capsule = 2;

// CModel::GetBoneIndexForHitboxForMesh — the hitbox -> bone remap.
constexpr std::uintptr_t k_remap_count = 0x220;
constexpr std::uintptr_t k_remap_table = 0x228;  // int16[]
constexpr std::uintptr_t k_remap_b = 0x240;      // uint16[mesh]
constexpr std::uintptr_t k_remap_a = 0x2F0;      // uint16[mesh]

constexpr std::uintptr_t k_bone_stride = 0x20;

// `CCSWeaponBaseVData` (reached via `C_BaseEntity::m_nSubclassID` + the entity's
// VData chain, `docs/re/aim.md`); the VData carries the real damage model.
constexpr std::uintptr_t k_vdata_spread = 0x750;  // m_flSpread (FiringModeFloat)
// The inaccuracy terms (`m_flInaccuracy*`, `docs/re/aim.md` §"Inaccuracy"), used for the
// hitchance cone. The engine composes the applicable terms from the shooter's state
// (`FUN_18080b880`); `aim::inaccuracy` is the first-order stand/move estimate.
constexpr std::uintptr_t k_vdata_inaccuracy_crouch = 0x758;
constexpr std::uintptr_t k_vdata_inaccuracy_stand = 0x760;
constexpr std::uintptr_t k_vdata_inaccuracy_move = 0x788;
constexpr std::uintptr_t k_vdata_inaccuracy_jump_initial = 0x7B8;
constexpr std::uintptr_t k_vdata_inaccuracy_jump_apex = 0x7BC;
// `m_WeaponType` (`SWeaponType`, +0x520): the ragebot's firearm gate.
constexpr std::uintptr_t k_vdata_weapon_type = 0x520;
// `m_bIsRevolver` (bool, +0x71E): the VData's own revolver discriminator — the
// payload's read of the weapon **id** the engine compares
// (`docs/re/aim.md` §"The attack type").
constexpr std::uintptr_t k_vdata_is_revolver = 0x71E;
// `m_nNumBullets` (int32, +0x730) and `m_nSpreadSeed` (int32, +0x7D8): the
// shotgun pattern table's inputs (`docs/re/aim.md` §"The pattern table").
constexpr std::uintptr_t k_vdata_num_bullets = 0x730;
constexpr std::uintptr_t k_vdata_spread_seed = 0x7D8;
// `C_CSWeaponBase::m_weaponMode` (int32, weapon + 0x1A00): the engine's attack
// type (0 primary, 1 secondary). It is a **sticky** flag — one path sets it, the
// fire path consumes and clears it — so a payload that reads it before the engine
// fires reads the engine's **previous** decision, and three of the four
// `CSBaseGunFire` call sites do not pass it at all
// (`docs/re/aim.md` §"The attack type").
constexpr std::uintptr_t k_weapon_mode = 0x1A00;
// `C_CSWeaponBase::m_flRecoilIndex` (float32, weapon + 0x1A28): the per-shot
// recoil counter the negev's rule is keyed on. It counts 0, 1, 2, … after every
// shot (`docs/re/aim.md` §"The spread cone").
constexpr std::uintptr_t k_weapon_recoil_index = 0x1A28;
// The **effective** inaccuracy the engine computed for the local pawn
// (`CsPlayerPawn::m_flInterpolatedInaccuracy`, client `+0x1F50`, `docs/re/aim.md`
// §"Inaccuracy"): the crosshair value — the engine's own composition of the stance/move/
// jump terms, so the spread cone needs no formula.
constexpr std::uintptr_t k_pawn_interpolated_inaccuracy = 0x1F50;
constexpr std::uintptr_t k_vdata_damage = 0x820;
constexpr std::uintptr_t k_vdata_headshot = 0x824;
constexpr std::uintptr_t k_vdata_penetration = 0x82C;
constexpr std::uintptr_t k_vdata_range_modifier = 0x834;

// One `CTransform` (`docs/re/aim.md`, the bone array): a position, a scale float,
// then the unit quaternion `(x, y, z, w)`. The rotation is at `+0x10`, **not**
// `+0x0C` — measured live 2026-10-16: the float at `+0x0C` is a constant `1.0`
// and reading the naive `{Vector, Quat}` layout yields non-unit quaternions
// (|q| ~ 1.0-1.4), which stretched every capsule instead of rotating it.
struct RawBone {
  Vec3 position{};     // +0x00
  float scale = 1.0F;  // +0x0C (constant 1.0 on the player bone array)
  float quat[4]{};     // +0x10 (x, y, z, w)
};

// Resolved model addresses for a pawn.
struct ModelRef {
  std::uintptr_t scene = 0;
  std::uintptr_t model = 0;
  std::uintptr_t bones = 0;
  std::uint8_t hitbox_set = 0;
  std::uint64_t mesh_group_mask = 0;
};

auto resolve_model(std::uintptr_t pawn) noexcept -> ModelRef {
  ModelRef ref;
  if (pawn == 0) {
    return ref;
  }
  if (!game::read(pawn + k_scene_node, ref.scene) || ref.scene == 0) {
    ref.scene = 0;
    return ref;
  }
  const std::uintptr_t model_state = ref.scene + k_model_state;
  std::uintptr_t handle = 0;
  if (!game::read(model_state + k_model_handle, handle) || handle == 0) {
    return ref;
  }
  // `m_hModel` is a `CStrongHandle`: dereference once to the binding, once to the
  // model object (verified live).
  std::uintptr_t model = 0;
  if (!game::read(handle, model) || model == 0) {
    return ref;
  }
  ref.model = model;
  (void)game::read(model_state + k_bone_array, ref.bones);
  (void)game::read(ref.scene + k_hitbox_set_index, ref.hitbox_set);
  (void)game::read(ref.scene + k_mesh_group_mask, ref.mesh_group_mask);
  return ref;
}

// The bone index a hitbox rides: `CModel::GetBoneIndexForHitboxForMesh`.
auto bone_for_hitbox(const ModelRef& ref, int mesh_index, int hitbox_index) noexcept -> int {
  std::int32_t remap_count = 0;
  if (!game::read(ref.model + k_remap_count, remap_count) || remap_count == 0) {
    return -1;
  }
  std::uintptr_t a = 0;
  std::uintptr_t b = 0;
  std::uintptr_t table = 0;
  if (!game::read(ref.model + k_remap_a, a) || a == 0 || !game::read(ref.model + k_remap_b, b) ||
      b == 0 || !game::read(ref.model + k_remap_table, table) || table == 0) {
    return -1;
  }
  std::uint16_t start = 0;
  std::uint16_t offset = 0;
  const auto mesh = static_cast<std::uintptr_t>(mesh_index);
  if (!game::read(a + mesh * 2, start) || !game::read(b + mesh * 2, offset)) {
    return -1;
  }
  const auto index =
      static_cast<std::int32_t>(start) + static_cast<std::int32_t>(offset) + hitbox_index;
  std::int16_t bone = -1;
  if (!game::read(table + static_cast<std::uintptr_t>(index) * 2, bone)) {
    return -1;
  }
  return bone;
}

}  // namespace

auto read_weapon_profile(std::uintptr_t weapon_vdata, WeaponProfile& profile) noexcept -> bool {
  if (weapon_vdata == 0) {
    return false;
  }
  std::int32_t damage = 0;
  if (!game::read(weapon_vdata + k_vdata_damage, damage) || damage <= 0) {
    return false;
  }
  profile.damage = static_cast<float>(damage);
  if (float headshot = 0.0F;
      game::read(weapon_vdata + k_vdata_headshot, headshot) && headshot > 0.0F) {
    profile.headshot_multiplier = headshot;
  }
  if (float penetration = 0.0F;
      game::read(weapon_vdata + k_vdata_penetration, penetration) && penetration > 0.0F) {
    profile.penetration = penetration;
  }
  if (float range_modifier = 0.0F;
      game::read(weapon_vdata + k_vdata_range_modifier, range_modifier) && range_modifier > 0.0F) {
    profile.range_modifier = range_modifier;
  }
  if (float spread = 0.0F; game::read(weapon_vdata + k_vdata_spread, spread) && spread > 0.0F) {
    profile.spread = spread;
  }
  // The inaccuracy terms (read as-is; the shader picks which apply).
  (void)game::read(weapon_vdata + k_vdata_inaccuracy_crouch, profile.inaccuracy_crouch);
  (void)game::read(weapon_vdata + k_vdata_inaccuracy_stand, profile.inaccuracy_stand);
  (void)game::read(weapon_vdata + k_vdata_inaccuracy_move, profile.inaccuracy_move);
  (void)game::read(weapon_vdata + k_vdata_inaccuracy_jump_initial, profile.inaccuracy_jump_initial);
  (void)game::read(weapon_vdata + k_vdata_inaccuracy_jump_apex, profile.inaccuracy_jump_apex);
  (void)game::read(weapon_vdata + k_vdata_weapon_type, profile.weapon_type);
  // The VData's own revolver flag: the payload's read of the weapon **id** the
  // engine compares. The deagle's is false, the R8's is true.
  (void)game::read(weapon_vdata + k_vdata_is_revolver, profile.is_revolver);
  (void)game::read(weapon_vdata + k_vdata_num_bullets, profile.num_bullets);
  (void)game::read(weapon_vdata + k_vdata_spread_seed, profile.spread_seed);
  return true;
}

auto build_input(const game::Snapshot& snapshot, const WeaponProfile& weapon, int target_index,
                 bool weapon_from_game) -> std::optional<SolveInput> {
  if (!snapshot.valid || !snapshot.local.has_value()) {
    return std::nullopt;
  }
  const game::PlayerSnapshot& local = *snapshot.local;

  const game::PlayerSnapshot* target = nullptr;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (player.is_local || !player.alive || player.team == local.team) {
      continue;
    }
    if (target_index >= 0 && player.index != target_index) {
      continue;
    }
    if (target == nullptr || player.distance < target->distance) {
      target = &player;
    }
  }
  if (target == nullptr) {
    return std::nullopt;
  }

  SolveInput input;
  input.eye = local.eye_origin;
  // The chart is the absolute world yaw/pitch — never the camera (docs/ragebot.md).
  input.basis = Basis::world();
  input.weapon = weapon;
  // Prefer the shooter's real weapon VData (damage / headshot / penetration /
  // range modifier) over the caller's profile when available.
  if (weapon_from_game) {
    (void)read_weapon_profile(local.weapon_vdata, input.weapon);
    // The engine's effective inaccuracy for the current shot (the crosshair value):
    // the exact composition of the stance/move/jump terms, read from the local pawn.
    if (float effective = 0.0F;
        game::read(local.pawn + k_pawn_interpolated_inaccuracy, effective) && effective >= 0.0F) {
      input.weapon.inaccuracy = effective;
    }
    // The engine's **attack type** and its **per-shot recoil counter** live on the
    // weapon *entity*, not on its VData.
    //
    // **The attack type is evidence, not knowledge.** The field is sticky: the
    // engine's secondary path sets it to 1 and the fire path consumes and clears
    // it, so a payload that reads it *before* the engine fires sees the engine's
    // **previous** decision — and three of the four `CSBaseGunFire` call sites do
    // not pass the field at all (they pass a literal `1`, a literal `0`, or
    // `ammo_in_clip & 1`). The payload therefore cannot know the engine's attack
    // type for the shot it is about to take.
    //
    // It reports what the field says, and the **core** applies the revolver's
    // `1 - r^2` only when the field says the *secondary* attack. Because the
    // fire path clears the field, the payload reads 0 (primary) for the shots it
    // takes, so the transform is off in practice — the conservative reading of
    // "not the secondary attack" — and the cone is the standard one
    // (`docs/re/aim.md` §"The attack type").
    if (std::int32_t mode = 0; game::read(local.weapon_entity + k_weapon_mode, mode)) {
      input.weapon.attack_type =
          (mode == ATTACK_TYPE_SECONDARY) ? ATTACK_TYPE_SECONDARY : ATTACK_TYPE_PRIMARY;
    }
    // The **recoil counter**: the engine increments it by one after every shot and
    // resets it when the weapon stops firing. An unreadable counter leaves the
    // negev's transform off (`RECOIL_INDEX_UNKNOWN`).
    if (float recoil = 0.0F;
        game::read(local.weapon_entity + k_weapon_recoil_index, recoil) && recoil >= 0.0F) {
      input.weapon.recoil_index = static_cast<int>(recoil);
    }
  }
  input.pawn = target->pawn;

  // The real studio hitboxes: bone-local capsule endpoints transformed by the
  // bone (`read_model_hitboxes`). Fall back to the collision-AABB proxy only
  // when the model cannot be resolved.
  input.hitboxes = read_solver_hitboxes(target->pawn, input.weapon);
  if (input.hitboxes.empty()) {
    // Fallback: the collision AABB split into a head cube on the eye and a body
    // box (the pre-studio-hitbox proxy).
    const Vec3 world_mins = target->origin + target->mins;
    const Vec3 world_maxs = target->origin + target->maxs;
    const auto boxes = split_player_at_head(world_mins, world_maxs, target->eye_origin, k_head_half,
                                            weapon.headshot_multiplier, 1.0F);
    input.hitboxes.assign(boxes.begin(), boxes.end());
  }
  // The hitboxes belong to **the target this input is about**, so they carry
  // its entity index.
  //
  // **Why the reader stamps it.** The index is the key the Monte-Carlo core
  // looks a target's health up under (`TargetHealth::index` against
  // `Hitbox::target`), and the occlusion filter narrows the world solids by it
  // (`solve`). The reader is the one place that knows which target it chose,
  // so it is the one place that can say so: the `aim` command used to hand the
  // health list the entity index while the hitboxes kept their default -1, so
  // the lookup never matched and the can-kill key was dead.
  for (Hitbox& hitbox : input.hitboxes) {
    hitbox.target = target->index;
  }
  return input;
}

auto read_model_hitboxes(std::uintptr_t pawn, bool read_names) -> std::vector<ModelHitbox> {
  std::vector<ModelHitbox> out;
  const ModelRef ref = resolve_model(pawn);
  if (ref.model == 0 || ref.bones == 0) {
    return out;
  }

  std::int32_t mesh_count = 0;
  std::uintptr_t meshes = 0;
  std::uintptr_t masks = 0;
  if (!game::read(ref.model + k_mesh_count, mesh_count) || mesh_count <= 0 || mesh_count > 64) {
    return out;
  }
  if (!game::read(ref.model + k_mesh_array, meshes) || meshes == 0) {
    return out;
  }
  (void)game::read(ref.model + k_mesh_mask, masks);

  // Use the first render mesh whose mesh-group mask intersects the skeleton's
  // active mask (all player meshes carry the same hitbox set).
  for (int mesh_index = 0; mesh_index < mesh_count; ++mesh_index) {
    if (masks != 0) {
      std::uint64_t mask = 0;
      if (!game::read(masks + static_cast<std::uintptr_t>(mesh_index) * 8, mask) ||
          (mask & ref.mesh_group_mask) == 0) {
        continue;
      }
    }
    std::uintptr_t mesh = 0;
    if (!game::read(meshes + static_cast<std::uintptr_t>(mesh_index) * 8, mesh) || mesh == 0) {
      continue;
    }
    std::uint32_t flags = 0;
    if (!game::read(mesh + k_set_flags, flags)) {
      continue;
    }
    // A zero flag word means this mesh carries no hitbox-set table (the engine's
    // own hitbox lookup treats it as absent), so do not read `+0x168` as an
    // inline array — the sets always live behind the pointer.
    if ((flags & 0x7FFFFFFFU) == 0U) {
      continue;
    }
    std::uintptr_t sets = 0;
    if (!game::read(mesh + k_set_data, sets) || sets == 0) {
      continue;
    }
    std::int32_t set_count = 0;
    if (!game::read(mesh + k_set_count, set_count) || set_count <= 0) {
      continue;
    }
    const auto set_index =
        static_cast<std::uintptr_t>(ref.hitbox_set < set_count ? ref.hitbox_set : set_count - 1);
    const std::uintptr_t set = sets + set_index * k_set_entry_stride;

    std::int32_t box_count = 0;
    std::uintptr_t boxes = 0;
    if (!game::read(set + k_set_entry_boxes, box_count) || box_count <= 0 || box_count > 128) {
      continue;
    }
    if (!game::read(set + k_set_entry_array, boxes) || boxes == 0) {
      continue;
    }

    out.reserve(static_cast<std::size_t>(box_count));
    for (int i = 0; i < box_count; ++i) {
      const std::uintptr_t box = boxes + static_cast<std::uintptr_t>(i) * k_box_stride;
      ModelHitbox hitbox;
      std::uint16_t hitbox_index = 0;
      if (!game::read(box + k_box_mins, hitbox.mins) ||
          !game::read(box + k_box_maxs, hitbox.maxs) ||
          !game::read(box + k_box_radius, hitbox.radius)) {
        continue;
      }
      (void)game::read(box + k_box_index, hitbox_index);
      std::int32_t group = 0;
      (void)game::read(box + k_box_group, group);
      hitbox.group = group;
      std::uint8_t shape = 0;
      (void)game::read(box + k_box_shape, shape);
      hitbox.shape = shape;

      hitbox.bone = bone_for_hitbox(ref, mesh_index, static_cast<int>(hitbox_index));

      // The world capsule endpoints (the bone transform from the bone array). A
      // box whose bone cannot be resolved has no world geometry; skip it rather
      // than leave the endpoints at (0,0,0) — a phantom hitbox at the map origin.
      RawBone raw{};
      if (hitbox.bone < 0 ||
          !game::read(ref.bones + static_cast<std::uintptr_t>(hitbox.bone) * k_bone_stride, raw)) {
        continue;
      }
      BoneTransform bone{};
      bone.position = raw.position;
      std::memcpy(bone.rotation, raw.quat, sizeof(bone.rotation));
      hitbox.transform = bone;
      hitbox.mesh = mesh_index;
      hitbox.world_a = bone.position + rotate(bone, hitbox.mins);
      hitbox.world_b = bone.position + rotate(bone, hitbox.maxs);

      if (read_names) {
        if (const std::string name = game::read_cstring(
                [&]() -> std::uintptr_t {
                  std::uintptr_t name_ptr = 0;
                  (void)game::read(box + k_box_bone_name, name_ptr);
                  return name_ptr;
                }(),
                24);
            !name.empty()) {
          std::snprintf(hitbox.name, sizeof(hitbox.name), "%s", name.c_str());
        }
      }
      out.push_back(hitbox);
    }
    break;  // one mesh is enough — player meshes share the hitbox set
  }
  return out;
}

auto read_solver_hitboxes(std::uintptr_t pawn, const WeaponProfile& weapon) -> std::vector<Hitbox> {
  std::vector<Hitbox> out;
  for (const ModelHitbox& box : read_model_hitboxes(pawn, /*read_names=*/false)) {
    // Only capsules are modelled (`m_nShapeType`); a non-capsule hitbox with no
    // radius (a box) is skipped rather than mis-aimed as a bare segment.
    if (box.shape != k_shape_capsule && box.radius <= 0.0F) {
      continue;
    }
    Hitbox hitbox;
    hitbox.is_capsule = true;
    hitbox.capsule = Capsule{.a = box.world_a, .b = box.world_b, .radius = box.radius};
    hitbox.group = box.group;
    hitbox.bone = box.bone;
    hitbox.multiplier = box.group == k_hitgroup_head ? weapon.headshot_multiplier : 1.0F;
    out.push_back(hitbox);
  }
  return out;
}

}  // namespace cs2::aim
