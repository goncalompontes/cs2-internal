#include "cs2/features/chams.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "cs2/features/esp_cache.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/material.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"

namespace cs2::features {
namespace {

// The active instance: the detour has no context parameter.
//
// This is the accepted exception to "no global mutable state" (ADR-0010): a
// C-ABI entry point with no place to pass a context, the same pattern the
// third-person camera's hook uses (`cs2/features/thirdperson.cpp`). The
// pointer is written by the payload's thread when the hook is installed and
// cleared when it is uninstalled, so a detour that runs sees either the
// instance or nothing.
Chams* g_active = nullptr;

// `CAnimatableSceneObjectDesc::DrawPrimitive`:
//
//   void(this, IRenderContext*, CMeshDrawPrimitive_t*, const IMaterial2*,
//        const IMaterialMode*, DrawValues_t&, bool, SceneSystemPerFrameStats_t*,
//        ISceneLayer*);
//
// The material is the **fourth** argument (index 3 counting `this`).
using DrawPrimitiveFn = void(__fastcall*)(void* desc, void* render_context, void* mesh_primitive,
                                          const void* material, const void* material_mode,
                                          void* draw_values, bool deferred, void* stats,
                                          void* layer);

// The scene object desc -> the entity that owns the model.
//
// The client sets the desc up when the model is set up
// (`CModelState::SetupModel`): the animatable desc's `+0x110` field receives
// `model_state + 0x130`, and the model state's `+0x30` holds the owning
// entity. Both offsets were read off the live client build
// (client.dll sha 659940c1...).
inline constexpr std::uintptr_t k_desc_model_state_field = 0x110;
inline constexpr std::uintptr_t k_model_state_owner_field = 0x130;
inline constexpr std::uintptr_t k_model_state_entity_field = 0x30;

// `C_BaseEntity::m_pRenderComponent` and
// `CRenderComponent::m_bIsRenderingWithViewModels` — the flag the engine
// raises while the view model (the weapon in front of the camera) is drawn.
inline constexpr std::uintptr_t k_entity_render_component = 0x338;
inline constexpr std::uintptr_t k_render_component_view_models = 0x50;

// The material each mode draws with.
//
//   * `flat` — the game's flat (unlit) material: the real chams look;
//   * `glow` — the no-depth-test variant of the wireframe material, which is
//     the game's own "draw it through the walls" material. It is a *see
//     through* approximation of a glow, not an additive one;
//   * `wireframe` — the game's wireframe material.
[[nodiscard]] auto material_path(ChamsMode mode) noexcept -> std::string_view {
  switch (mode) {
    case ChamsMode::flat:
      return game::k_flat_material_path;
    case ChamsMode::glow:
      return game::k_nodepth_material_path;
    case ChamsMode::wireframe:
      return game::k_wire_material_path;
  }
  return game::k_flat_material_path;
}

// The entity the drawn model belongs to, or 0 when the desc does not carry one.
[[nodiscard]] auto entity_of(const void* desc) noexcept -> std::uintptr_t {
  const auto base = reinterpret_cast<std::uintptr_t>(desc);
  if (base == 0) {
    return 0;
  }
  std::uintptr_t field = 0;
  if (!game::read(base + k_desc_model_state_field, field)) {
    return 0;
  }
  // The field holds the *address* of a field inside the model state, so it can
  // never be below the model state's own size.
  if (field < k_model_state_owner_field) {
    return 0;
  }
  const std::uintptr_t model_state = field - k_model_state_owner_field;
  std::uintptr_t entity = 0;
  if (!game::read(model_state + k_model_state_entity_field, entity)) {
    return 0;
  }
  return entity;
}

// Is the entity drawn as the view model? The engine raises the render
// component's "rendering with view models" flag while the weapon in front of
// the camera is drawn.
[[nodiscard]] auto is_viewmodel(std::uintptr_t entity) noexcept -> bool {
  std::uintptr_t render_component = 0;
  if (!game::read(entity + k_entity_render_component, render_component) || render_component == 0) {
    return false;
  }
  std::uint8_t flag = 0;
  if (!game::read(render_component + k_render_component_view_models, flag)) {
    return false;
  }
  return (flag & 1U) != 0;
}

void __fastcall draw_primitive_detour(void* desc, void* render_context, void* mesh_primitive,
                                      const void* material, const void* material_mode,
                                      void* draw_values, bool deferred, void* stats, void* layer) {
  Chams* active = g_active;
  auto* original =
      active != nullptr ? reinterpret_cast<DrawPrimitiveFn>(active->original()) : nullptr;
  if (original == nullptr) {
    // The hook is being torn down: there is no trampoline to call, so the
    // draw is dropped for this frame rather than crashing the renderer.
    return;
  }
  active->on_draw(desc, render_context, mesh_primitive, material, material_mode, draw_values,
                  deferred, stats, layer);
}

}  // namespace

Chams::~Chams() {
  if (hook_.installed()) {
    hook_.uninstall();
  }
  if (g_active == this) {
    g_active = nullptr;
  }
}

void Chams::configure(const Config::Visuals& visuals) noexcept {
  enabled_.store(visuals.chams, std::memory_order_relaxed);
  // The chams are for the *enemy* players; the operator can add the local
  // player, but there is no "chams the teammates" switch.
  only_enemies_.store(true, std::memory_order_relaxed);
  include_local_.store(visuals.chams_local, std::memory_order_relaxed);
  visible_only_.store(visuals.chams_visible, std::memory_order_relaxed);
  mode_.store(visuals.chams_mode, std::memory_order_relaxed);
  // The colour is *not* applied yet: the material system's "set a parameter
  // by its hashed name" entry point is unknown (only the getter was located),
  // and the colour parameter's name is not in any module — it lives in the
  // .vmat data. The flat material therefore keeps its own colour. The hash
  // and the parameter lookup are in `cs2::game` (`material_parameter_hash`),
  // ready for the setter.
  (void)visuals.chams_color;
}

void Chams::update(const game::Snapshot& snapshot) noexcept {
  // The hook: installed lazily, because the scene system only exists once the
  // game is up. The install is retried on every frame until it succeeds.
  if (!hook_.installed()) {
    (void)install();
  }

  // The local player: the entity the "is local" test compares against, and
  // the team the "only enemies" test uses.
  local_pawn_.store(snapshot.local_pawn, std::memory_order_relaxed);
  if (snapshot.local.has_value()) {
    local_team_.store(snapshot.local->team, std::memory_order_relaxed);
  }

  // The players the chams may draw. The facts come from the snapshot, so the
  // render path does not have to read them from the engine.
  int count = 0;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (count >= k_max_targets) {
      break;
    }
    if (player.pawn == 0 || player.pawn > k_target_address_mask) {
      continue;
    }
    const std::uint64_t facts = (static_cast<std::uint64_t>(player.team) & 0x3U) |
                                ((static_cast<std::uint64_t>(player.life_state) & 0x3U) << 2) |
                                (static_cast<std::uint64_t>(player.is_local ? 1 : 0) << 4);
    targets_[static_cast<std::size_t>(count)].store(
        (static_cast<std::uint64_t>(player.pawn) & k_target_address_mask) |
            (facts << k_target_facts_shift),
        std::memory_order_relaxed);
    ++count;
  }
  target_count_.store(count, std::memory_order_relaxed);

  // The material: resolved once per mode change. The pointer is immutable
  // once published, so the render thread can use it without a lock.
  const ChamsMode mode = mode_.load(std::memory_order_relaxed);
  if (!material_resolved_ || mode != resolved_mode_) {
    resolved_mode_ = mode;
    material_resolved_ = true;
    const game::Material resolved = resolve_material(mode);
    material_.store(resolved, std::memory_order_relaxed);
    if (resolved == 0) {
      game::append_log("[cs2-features] chams: the material could not be resolved");
    }
  }

  // The "only when the player is visible" filter. The authoritative answer
  // comes from the ESP's visibility provider; without a provider every player
  // counts as visible, exactly like the ESP's own visibility toggles.
  const EspProviders providers = esp_providers();
  if (!visible_only_.load(std::memory_order_relaxed) || providers.visibility == nullptr) {
    visible_filter_.store(false, std::memory_order_relaxed);
    visible_count_.store(0, std::memory_order_relaxed);
    return;
  }
  visible_filter_.store(true, std::memory_order_relaxed);
  int visible = 0;
  for (const game::PlayerSnapshot& player : snapshot.players) {
    if (visible >= k_max_visible) {
      break;
    }
    if (providers.visibility(providers.visibility_context, player.index) != Visibility::hidden) {
      visible_[static_cast<std::size_t>(visible)].store(player.pawn, std::memory_order_relaxed);
      ++visible;
    }
  }
  visible_count_.store(visible, std::memory_order_relaxed);
}

void Chams::on_draw(void* desc, void* render_context, void* mesh_primitive, const void* material,
                    const void* material_mode, void* draw_values, bool deferred, void* stats,
                    void* layer) noexcept {
  auto* original = reinterpret_cast<DrawPrimitiveFn>(hook_.original());
  if (original == nullptr) {
    return;
  }
  // The cheapest way out: with the feature off the draw is forwarded
  // untouched, without touching the engine at all.
  if (!enabled_.load(std::memory_order_relaxed)) {
    original(desc, render_context, mesh_primitive, material, material_mode, draw_values, deferred,
             stats, layer);
    return;
  }
  // The material the payload would substitute. Without one the draw goes
  // through untouched (fail closed).
  const game::Material override_material = material_.load(std::memory_order_relaxed);
  if (override_material == 0) {
    skipped_.fetch_add(1, std::memory_order_relaxed);
    original(desc, render_context, mesh_primitive, material, material_mode, draw_values, deferred,
             stats, layer);
    return;
  }
  // The entity the drawn model belongs to. Without one there is nothing to
  // decide on (fail closed).
  const std::uintptr_t entity = entity_of(desc);
  if (entity == 0) {
    skipped_.fetch_add(1, std::memory_order_relaxed);
    original(desc, render_context, mesh_primitive, material, material_mode, draw_values, deferred,
             stats, layer);
    return;
  }

  // The player's facts, from the snapshot's table. An entity that is not one
  // of the players (a prop, a dropped weapon, the view model) is not in the
  // table and is never chamsed.
  const int count = target_count_.load(std::memory_order_relaxed);
  game::ChamsEntity facts;
  bool known = false;
  for (int index = 0; index < count && index < k_max_targets; ++index) {
    const std::uint64_t packed =
        targets_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed);
    if ((packed & k_target_address_mask) != entity) {
      continue;
    }
    const std::uint64_t packed_facts = packed >> k_target_facts_shift;
    facts.team = static_cast<int>(packed_facts & 0x3U);
    facts.life_state = static_cast<int>((packed_facts >> 2) & 0x3U);
    facts.is_local = ((packed_facts >> 4) & 0x1U) != 0;
    known = true;
    break;
  }
  if (!known) {
    skipped_.fetch_add(1, std::memory_order_relaxed);
    original(desc, render_context, mesh_primitive, material, material_mode, draw_values, deferred,
             stats, layer);
    return;
  }
  // The view model is never chamsed, whatever the table says: the engine
  // raises the render component's flag while the weapon in front of the camera
  // is drawn.
  facts.is_viewmodel = is_viewmodel(entity);
  if (facts.is_viewmodel) {
    skipped_.fetch_add(1, std::memory_order_relaxed);
    original(desc, render_context, mesh_primitive, material, material_mode, draw_values, deferred,
             stats, layer);
    return;
  }

  game::ChamsPolicy policy;
  policy.enabled = true;
  policy.only_enemies = only_enemies_.load(std::memory_order_relaxed);
  policy.include_local = include_local_.load(std::memory_order_relaxed);
  policy.local_team = local_team_.load(std::memory_order_relaxed);

  if (!game::wants_chams(facts, policy)) {
    skipped_.fetch_add(1, std::memory_order_relaxed);
    original(desc, render_context, mesh_primitive, material, material_mode, draw_values, deferred,
             stats, layer);
    return;
  }
  if (visible_filter_.load(std::memory_order_relaxed) && !is_visible(entity)) {
    skipped_.fetch_add(1, std::memory_order_relaxed);
    original(desc, render_context, mesh_primitive, material, material_mode, draw_values, deferred,
             stats, layer);
    return;
  }

  // The override. The material is an **argument** of the draw, so the
  // substitution is local to this call: the material system's state is never
  // touched, and there is nothing to restore afterwards. (A draw that returns
  // without restoring a *global* material would chams the whole world; this
  // design cannot do that, because the material is never made global.)
  original(desc, render_context, mesh_primitive, reinterpret_cast<const void*>(override_material),
           material_mode, draw_values, deferred, stats, layer);
  drawn_.fetch_add(1, std::memory_order_relaxed);
}

auto Chams::install() noexcept -> bool {
  if (hook_.installed()) {
    return true;
  }
  if (install_failed_.load(std::memory_order_relaxed)) {
    return false;
  }
  const game::ModuleInfo scene = game::find_module("scenesystem.dll");
  if (!scene.valid()) {
    return false;  // the game is not up yet: retry on the next frame
  }
  const std::uintptr_t target = scene.base + k_draw_primitive_rva;
  if (!scene.contains(target)) {
    // A build the offsets were not derived on. Do not hook: a stale address
    // would patch arbitrary code.
    install_failed_.store(true, std::memory_order_relaxed);
    game::append_log(
        "[cs2-features] chams: scenesystem.dll does not hold the draw at the "
        "expected offset; the chams are off");
    return false;
  }
  g_active = this;
  const auto error = hook_.install(reinterpret_cast<void*>(target),
                                   reinterpret_cast<void*>(&draw_primitive_detour));
  if (error.has_value()) {
    g_active = nullptr;
    install_failed_.store(true, std::memory_order_relaxed);
    game::append_log("[cs2-features] chams: the draw hook failed: " +
                     std::string(hooks::to_string(*error)));
    return false;
  }
  game::append_log("[cs2-features] chams: the scene system's draw is hooked");
  return true;
}

auto Chams::resolve_material(ChamsMode mode) noexcept -> game::Material {
  if (!materials_.valid()) {
    materials_ = game::MaterialSystem::attach();
  }
  if (!materials_.valid()) {
    return 0;
  }
  return materials_.find_material(material_path(mode));
}

auto Chams::is_visible(std::uintptr_t entity) const noexcept -> bool {
  const int count = visible_count_.load(std::memory_order_relaxed);
  for (int index = 0; index < count && index < k_max_visible; ++index) {
    if (visible_[static_cast<std::size_t>(index)].load(std::memory_order_relaxed) == entity) {
      return true;
    }
  }
  return false;
}

}  // namespace cs2::features
