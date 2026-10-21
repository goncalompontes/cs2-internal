#include "cs2/features/chams.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

#include "cs2/features/esp_cache.hpp"
#include "cs2/features/guard_scope.hpp"
#include "cs2/game/log.hpp"
#include "cs2/game/material.hpp"
#include "cs2/game/memory.hpp"
#include "cs2/game/module.hpp"

#include "offsets.hpp"

#if defined(_WIN32)
#include <windows.h>
#endif

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
//
// The signature is the one the RE re-derived from the function's own body and
// its call site (`docs/re/chams.md` §8): the *real* draw is a nine-argument
// member function, and the material is in `R9` — the fourth argument. The
// detour below was always right; only the **address** it patched was wrong.
using DrawPrimitiveFn = void(__fastcall*)(void* desc, void* render_context, void* mesh_primitive,
                                          const void* material, const void* material_mode,
                                          void* draw_values, bool deferred, void* stats,
                                          void* layer);

// The module the draw lives in, and the dumper's signature for it.
//
// The address is **not** a constant: it is resolved through the signature the
// calibration dump carries (ADR-0012: no hard-coded RVA), and the bytes at
// the resolved address are compared with the expected prologue before
// anything is patched.
inline constexpr std::string_view SCENE_MODULE = "scenesystem.dll";
inline constexpr std::string_view SCENE_DRAW_OFFSET = "scene_draw_primitive";

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

// The owner token: the OS thread id.
//
// The re-entrancy guard needs a value that is unique per thread, and it must
// be readable **without** the payload's TLS block: the payload is manually
// mapped, and the scene system's render threads have no TLS block installed
// for it, so a `thread_local` read on that path faults inside the payload.
// `GetCurrentThreadId()` reads the thread's own block, so it is safe on any
// thread, the game's included.
[[nodiscard]] auto current_owner() noexcept -> guard::OwnerId {
#if defined(_WIN32)
  return static_cast<guard::OwnerId>(GetCurrentThreadId());
#else
  return 1U;
#endif
}

// The draw hook's **target** — and why it is verified at run time.
//
// The address the hook used to patch was derived from an MSVC RTTI *lambda*
// type:
// `DrawPrimitive@CAnimatableSceneObjectDesc@@QEAAXPEAVIRenderContext@@PEAUCMeshDrawPrimitive_t@@PEBVIMaterial2@@PEAVIMaterialMode@@AEAUDrawValues_t@@_NAEAUScSceneSystemPerFrameStats_t@@PEAVIScSceneLayer@@`.
//
// Decoded, that is a **member** function: `Q` public, `E` 64-bit, `A` __cdecl,
// `A` near, `A` unqualified, `X` void, then `this` plus
// `IRenderContext*`, `CMeshDrawPrimitive_t*`, `const IMaterial2*`,
// `IMaterialMode*`, `DrawValues_t&`, `bool`, `SceneSystemPerFrameStats_t&`,
// `ISceneLayer*`.
//
// But the type it came from is the type of a **lambda**, and a lambda's
// `operator()` is **static**: it takes the eight arguments and has **no
// `this`**. The detour below assumes the member function's shape, so patching
// the lambda's `operator()` shifts the whole argument list by one: the callee
// receives the material pointer where it expects the mesh pointer and
// dereferences it. The live test produced exactly that - an access violation
// (0xc0000005) on a read of a garbage address, with the fault inside the
// renderer.
//
// The target was re-derived from the class's **own** RTTI chain and vftable
// (`docs/re/chams.md` §8): the real draw is at `scenesystem.dll + 0x580A0`
// and it **is** a nine-argument member function, so the detour and the
// typedef above never changed — only the address did.
//
// A constant cannot express that, and a constant is what made the crash
// possible. What replaced it is a **runtime verification**: the address comes
// from the dumper's signature and the bytes at that address are read through
// the fault-safe reader and compared with the prologue the RE measured
// (`cs2/features/hook_target.hpp`). A mismatch refuses the hook and says why.

void __fastcall draw_primitive_detour(void* desc, void* render_context, void* mesh_primitive,
                                      const void* material, const void* material_mode,
                                      void* draw_values, bool deferred, void* stats, void* layer) {
  Chams* active = g_active;
  if (active == nullptr || active->original() == nullptr) {
    // The hook is being torn down: there is no trampoline to call, so the
    // draw is dropped for this frame rather than crashing the renderer.
    return;
  }
  // The re-entrancy guard.
  //
  // The trampoline is the game's own prologue plus a jump back; if the
  // prologue were patched wrongly the trampoline would land **in this
  // detour** again, and the chain (detour -> original -> detour -> ...) would
  // recurse until the stack overflowed (0xc00000fd) and took the whole
  // renderer with it. The guard breaks the cycle: the re-entrant call
  // returns without touching the material, so the draw is dropped for that
  // primitive instead of crashing the game.
  //
  // The guard is **per owner** (the thread id), not process-wide: the scene
  // system renders its draw list in jobs on the tier0 thread pool
  // (`scenesystem.dll` imports `g_pThreadPool` and carries
  // `CSceneSystem::Thread_RenderSceneDrawList` and the `sc_disableThreading`
  // convar, so the threading is on by default). A process-wide counter would
  // read the *other* job's call as a recursion and **drop that draw** — a
  // hole in the model for that frame — and it would do so silently. The
  // per-owner guard refuses only the call that is the recursion: the one that
  // comes back on the *same* thread.
  //
  // It is not a `thread_local` either: the payload is manually mapped and its
  // TLS block is not installed on the game's render threads, so a
  // thread-local read here faults inside the payload (measured on the
  // camera's detour: `read addr=0x4` at the guard's load — the mapped image's
  // TLS slot was 154, above the 64 slots the module's TLS bootstrap
  // supports).
  //
  // The scope holds the guard **across** the trampoline call — that is the
  // recursion it exists to break, so a guard released before the call would
  // protect nothing — and releases it on **every** path out of the scope.
  guard::Scope scope(active->draw_guard(), current_owner());
  if (!scope.entered()) {
    // The trampoline came back here: this is the recursion, and it is broken
    // here rather than a thousand frames later.
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

  // The colour: the material system's parameter setter writes it into the
  // chams material. The decision is a pure function (`game::colour_action`),
  // so the same rule covers the colour changing, the chams being switched off
  // and the material not being ready yet.
  const bool changed = visuals.chams_color != colour_;
  colour_ = visuals.chams_color;
  const game::Material material = material_.load(std::memory_order_relaxed);
  const bool tintable = material != 0 && !colour_failed_;
  switch (game::colour_action(visuals.chams, tintable, colour_applied_, changed)) {
    case game::ColourAction::apply:
      apply_colour();
      break;
    case game::ColourAction::restore:
      restore_colour();
      break;
    case game::ColourAction::none:
      break;
  }

  // The hook follows the switch: it is installed when the chams are turned on
  // and removed when they are turned off, so a disabled feature never patches
  // the renderer's code.
  if (visuals.chams) {
    if (!hook_.installed() &&
        hook_status_.load(std::memory_order_relaxed) == hook_target::Status::idle) {
      // The chams have just been switched on: the state starts at "the
      // install is being attempted", and the log's latch is reset so a
      // refusal is reported again the next time the feature is used.
      hook_status_.store(hook_target::Status::pending, std::memory_order_relaxed);
      reported_status_.store(hook_target::Status::pending, std::memory_order_relaxed);
    }
    (void)install();
  } else if (hook_.installed()) {
    hook_.uninstall();
    if (g_active == this) {
      g_active = nullptr;
    }
  }
  if (!visuals.chams) {
    // The chams are off: nothing is patched, the state is "off" and the next
    // refusal is logged again (the latch is reset with the state).
    //
    // The **refusal latch** is cleared too. It stops a refused install from
    // being retried (and re-reported) on every frame, and it has to be
    // cleared when the operator switches the feature off: a latch that
    // survived the switch-off would make the next switch-on never retry the
    // install, so the refusal would never be reported again and the HUD
    // would say "installing" forever — a lie, because no install is being
    // attempted, and the reason the hook is not installed would be gone.
    install_failed_.store(hook_target::refusal_after_switch(
                              visuals.chams, install_failed_.load(std::memory_order_relaxed)),
                          std::memory_order_relaxed);
    hook_status_.store(hook_target::Status::idle, std::memory_order_relaxed);
    reported_status_.store(hook_target::Status::idle, std::memory_order_relaxed);
    // The counters belong to the session that is over: a re-enable shows the
    // new hook's own numbers, not the previous session's.
    seen_.store(0, std::memory_order_relaxed);
    drawn_.store(0, std::memory_order_relaxed);
    skipped_.store(0, std::memory_order_relaxed);
  }
}

void Chams::update(const game::Snapshot& snapshot) noexcept {
  // The hook: installed lazily, because the scene system only exists once the
  // game is up. The install is retried on every frame until it succeeds.
  if (!hook_.installed()) {
    (void)install();
  }

  // A new frame: the render thread drops its desc cache, so a desc address
  // the engine recycles cannot carry a stale entity into this frame.
  frame_stamp_.fetch_add(1, std::memory_order_relaxed);

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
    // The material is swapped: the old one gets its own colour back before
    // the new one is tinted.
    restore_colour();
    resolved_mode_ = mode;
    material_resolved_ = true;
    game::MaterialHandle handle = 0;
    const game::Material resolved = resolve_material(mode, handle);
    handle_ = handle;
    material_.store(resolved, std::memory_order_relaxed);
    if (resolved == 0) {
      game::append_log("[cs2-features] chams: the material could not be resolved");
    } else if (enabled_.load(std::memory_order_relaxed)) {
      // The colour can only be written once the material is known.
      colour_failed_ = false;
      apply_colour();
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
  // Every draw that reaches the feature is **seen**: the counter answers "how
  // many draws did the hook see", and `seen == drawn + skipped` always.
  seen_.fetch_add(1, std::memory_order_relaxed);
  auto* original = reinterpret_cast<DrawPrimitiveFn>(hook_.original());
  if (original == nullptr) {
    // The hook is being torn down: there is no trampoline to call, so the
    // draw is dropped for this frame rather than crashing the renderer.
    skipped_.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  // The cheapest way out: with the feature off the draw is forwarded
  // untouched, without touching the engine at all.
  if (!enabled_.load(std::memory_order_relaxed)) {
    skipped_.fetch_add(1, std::memory_order_relaxed);
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
  // The entity the drawn model belongs to, and whether it is the view model.
  //
  // Both are engine reads, and the scene system draws every **primitive** of a
  // model with the same desc — so the reads are cached per desc and paid once
  // per **model**. They used to be four `ReadProcessMemory` syscalls per
  // primitive (measured at 284 ns each: 0.57 ms per frame at 500 primitives,
  // 2.3 ms at 2000), paid for every primitive in the scene and not only for
  // the players'.
  //
  // The cache is dropped when the payload's per-frame step publishes a new
  // frame stamp, so a desc address the engine recycles cannot carry a stale
  // entity into the next frame.
  const std::uint64_t stamp = frame_stamp_.load(std::memory_order_relaxed);
  if (stamp != desc_cache_stamp_) {
    desc_cache_stamp_ = stamp;
    for (DescEntry& entry : desc_cache_) {
      entry.desc.store(0, std::memory_order_relaxed);
    }
  }
  const auto desc_address = reinterpret_cast<std::uintptr_t>(desc);
  const auto slot = static_cast<std::size_t>((desc_address >> 4) &
                                             static_cast<std::uintptr_t>(k_desc_cache_size - 1));
  DescEntry& entry = desc_cache_[slot];
  std::uintptr_t entity = 0;
  bool view_model = false;
  if (entry.desc.load(std::memory_order_acquire) == desc_address) {
    entity = static_cast<std::uintptr_t>(entry.entity.load(std::memory_order_relaxed));
    view_model = entry.view_model.load(std::memory_order_relaxed) != 0;
  } else {
    entity = entity_of(desc);
    view_model = entity != 0 && is_viewmodel(entity);
    // The desc is published last: a reader that sees it sees the entity and
    // the flag that go with it.
    entry.entity.store(entity, std::memory_order_relaxed);
    entry.view_model.store(view_model ? 1 : 0, std::memory_order_relaxed);
    entry.desc.store(desc_address, std::memory_order_release);
  }
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
  facts.is_viewmodel = view_model;
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
  // **A feature that is off must not patch the game's code.** The draw hook is an
  // inline hook into the renderer's hot path; installing it while the chams are
  // off buys nothing and risks the renderer (a mis-patched prologue is a crash
  // in the game, not in us). The hook is installed when the operator turns the
  // chams on, and removed when they turn them off.
  //
  // A refused install is not retried either: the refusal is reported once
  // (`game::append_log` opens, writes and closes a file per call, and the
  // install path runs on the render thread). The latch that holds it is
  // cleared when the feature is switched off, so the next switch-on retries
  // and reports the refusal again.
  if (!hook_target::may_attempt_install(enabled_.load(std::memory_order_relaxed),
                                        install_failed_.load(std::memory_order_relaxed))) {
    return false;
  }

  // --- The target -----------------------------------------------------------
  //
  // The address comes from the dumper's `scene_draw_primitive` signature
  // (ADR-0012: no hard-coded RVA). The offset is **absent** when the
  // calibration dump predates the signature — the operator has to refresh it
  // with `scripts/calibrate-live.py` — and the chams then stay off and say so.
  const cs2::sdk::OffsetInfo* offset = cs2::sdk::find_offset(SCENE_DRAW_OFFSET);
  const game::ModuleInfo scene = game::find_module(SCENE_MODULE);

  hook_target::Facts facts;
  facts.offset_present = offset != nullptr;
  if (offset != nullptr) {
    facts.rva = offset->rva;
    facts.offset_module = offset->module;
  }
  facts.module_present = scene.valid();
  facts.module_base = scene.base;
  facts.module_size = scene.size;
  if (offset != nullptr && scene.valid()) {
    // The prologue is read through the **fault-safe** reader: the payload is
    // manually mapped without registered exception tables, so a bad address
    // must never fault.
    const auto target = scene.base + static_cast<std::uintptr_t>(offset->rva);
    facts.prologue_read = game::read_bytes(target, facts.prologue.data(), facts.prologue.size());
    facts.prologue_length = facts.prologue_read ? facts.prologue.size() : 0;
  }

  const hook_target::Status verdict =
      hook_target::decide(facts, hook_target::SCENE_DRAW_PROLOGUE, SCENE_MODULE);
  switch (verdict) {
    case hook_target::Status::no_offset:
      report(verdict,
             "[cs2-features] chams: the calibration dump has no 'scene_draw_primitive' "
             "offset for scenesystem.dll - refresh it with scripts/calibrate-live.py; "
             "the chams are off");
      install_failed_.store(true, std::memory_order_relaxed);
      return false;
    case hook_target::Status::no_module:
      // The game is not up yet: retry on the next frame. This is a normal
      // state, so it is recorded but not logged.
      report(verdict, "");
      return false;
    case hook_target::Status::outside_module:
      report(verdict,
             "[cs2-features] chams: the resolved draw does not lie inside "
             "scenesystem.dll; the chams are off");
      install_failed_.store(true, std::memory_order_relaxed);
      return false;
    case hook_target::Status::unreadable:
      report(verdict,
             "[cs2-features] chams: the draw's prologue could not be read; the "
             "chams are off");
      return false;
    case hook_target::Status::mismatch:
      report(verdict,
             "[cs2-features] chams: the prologue at the resolved draw does not "
             "match the one the RE measured - scenesystem.dll is not the build "
             "the chams were derived on; the chams are off");
      install_failed_.store(true, std::memory_order_relaxed);
      return false;
    case hook_target::Status::accepted:
      break;
    default:
      return false;
  }

  // --- The patch ------------------------------------------------------------
  //
  // Every check above passed: the address is the one the measurement was
  // taken on, and the bytes there are the ones the measurement saw.
  const auto target = scene.base + static_cast<std::uintptr_t>(offset->rva);
  g_active = this;
  const auto error = hook_.install(reinterpret_cast<void*>(target),
                                   reinterpret_cast<void*>(&draw_primitive_detour));
  if (error.has_value()) {
    g_active = nullptr;
    install_failed_.store(true, std::memory_order_relaxed);
    report(hook_target::Status::refused,
           std::string("[cs2-features] chams: the draw hook failed: ") + hooks::to_string(*error));
    return false;
  }
  report(hook_target::Status::installed, "[cs2-features] chams: the scene system's draw is hooked");
  return true;
}

auto Chams::resolve_material(ChamsMode mode, game::MaterialHandle& handle) noexcept
    -> game::Material {
  handle = 0;
  if (!materials_.valid()) {
    materials_ = game::MaterialSystem::attach();
  }
  if (!materials_.valid()) {
    return 0;
  }
  return materials_.find_material(material_path(mode), handle);
}

void Chams::apply_colour() noexcept {
  const game::Material material = material_.load(std::memory_order_relaxed);
  if (material == 0 || handle_ == 0) {
    return;
  }
  const float rgba[4] = {
      static_cast<float>(colour_.r) / 255.0F,
      static_cast<float>(colour_.g) / 255.0F,
      static_cast<float>(colour_.b) / 255.0F,
      static_cast<float>(colour_.a) / 255.0F,
  };
  float original[4]{};
  const game::ColourResult result = materials_.set_colour(handle_, material, rgba, original);
  if (result == game::ColourResult::applied) {
    if (!colour_applied_) {
      // The first write: the material's own colour is what has to be put
      // back when the chams go off.
      for (std::size_t index = 0; index < 4; ++index) {
        original_colour_[index] = original[index];
      }
    }
    colour_applied_ = true;
    colour_failed_ = false;
    return;
  }
  if (result == game::ColourResult::no_parameter) {
    // The material has no colour parameter the payload knows: the chams keep
    // drawing with the material's own colour, and the payload stops asking.
    colour_failed_ = true;
    game::append_log(
        "[cs2-features] chams: the material has no colour parameter; "
        "the chams keep the material's own colour");
  }
}

void Chams::restore_colour() noexcept {
  if (!colour_applied_) {
    return;
  }
  const game::Material material = material_.load(std::memory_order_relaxed);
  if (material != 0 && handle_ != 0) {
    (void)materials_.restore_colour(handle_, material, original_colour_);
  }
  colour_applied_ = false;
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

void Chams::report(hook_target::Status status, std::string_view message) noexcept {
  hook_status_.store(status, std::memory_order_relaxed);
  // **The log is latched.** `game::append_log` opens, writes and closes a file
  // per call, and the install path runs on the render thread and can be
  // retried every frame: a line per attempt wrote ~500 MB a day in the
  // animfix (the same bug, the same cost). So a line is written only when the
  // state **changes**.
  hook_target::Status reported = reported_status_.load(std::memory_order_relaxed);
  while (hook_target::must_report(reported, status)) {
    if (reported_status_.compare_exchange_weak(reported, status, std::memory_order_relaxed)) {
      if (!message.empty()) {
        game::append_log(message);
      }
      return;
    }
  }
}

auto Chams::status_line(char* out, std::size_t size) const noexcept -> bool {
  if (out == nullptr || size == 0) {
    return false;
  }
  // The line is built on the game's render thread, once per frame: it is
  // written straight into the caller's buffer, with no `std::string` and no
  // heap allocation. The description is a `string_view`, so it is printed
  // with a precision (`%.*s`) instead of being copied into a temporary.
  const std::string_view state = hook_target::describe(hook_status());
  const int written = std::snprintf(
      out, size, "hook=%.*s seen=%llu chams=%llu skip=%llu", static_cast<int>(state.size()),
      state.data(), static_cast<unsigned long long>(seen()),
      static_cast<unsigned long long>(drawn()), static_cast<unsigned long long>(skipped()));
  // `snprintf` returns the length the line would have had: a length that does
  // not fit means the line was truncated, and the caller is better off
  // knowing than showing half a status.
  return written >= 0 && static_cast<std::size_t>(written) < size;
}

}  // namespace cs2::features
