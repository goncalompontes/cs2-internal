#pragma once

// The 3D chams: the player models are drawn with one of the game's **own**
// flat materials instead of the model's own.
//
// **Route B — the real material override.** The payload hooks the scene
// system's draw entry and substitutes the `const IMaterial2*` the draw
// receives. The override is an *argument* substitution, not a change of the
// material system's global state, so a draw that returns cannot leave the
// world chamsed: the only material the payload ever hands out is the one it
// passes to that single draw.
//
// **What the hook sees.** The scene system's draw entry carries the material
// but **no entity pointer** (the draw signature is
// `CAnimatableSceneObjectDesc::DrawPrimitive(IRenderContext*,
// CMeshDrawPrimitive_t*, const IMaterial2*, const IMaterialMode*,
// DrawValues_t&, bool, SceneSystemPerFrameStats_t*, ISceneLayer*)`), so the
// entity is recovered from the scene object desc the draw belongs to. See
// `cs2/features/chams.cpp` for the chain and its provenance.
//
// **The render path is cheap.** The facts about a drawn player (its team, its
// life state, whether it is the local player) come from the snapshot, which
// the payload already reads at ~30 Hz; the draw hook only has to recover the
// entity the desc belongs to. With the feature off, the hook does nothing but
// one atomic load.
//
// **Fail closed.** If the scene system is not up, the hook target is not
// where it should be, the material is not resolved, or the entity cannot be
// recovered, the draw is forwarded **untouched**.

#include <atomic>
#include <cstdint>

#include "cs2/features/esp.hpp"
#include "cs2/game/material.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/hooks/inline.hpp"

namespace cs2::features {

// The scene system's `CAnimatableSceneObjectDesc::DrawPrimitive`.
//
// Provenance (scenesystem.dll sha 040eb851..., image base 0x180000000):
//
//   * the mangled name recovered from the module's RTTI lambda types is
//     `CAnimatableSceneObjectDesc::DrawPrimitive(IRenderContext*,
//     CMeshDrawPrimitive_t*, const IMaterial2*, const IMaterialMode*,
//     DrawValues_t&, bool, SceneSystemPerFrameStats_t*, ISceneLayer*)`;
//   * the *implementation* was pinned through the lambda's
//     `std::_Func_impl_no_alloc` vftable: the only code that constructs that
//     closure is the enclosing member function, which is therefore
//     `DrawPrimitive` — the address below;
//   * the class's vtable is at scenesystem.dll + 0x5E5C90 (the RTTI chain
//     TypeDescriptor -> CompleteObjectLocator -> the vftable pointer at
//     +0x5E5C88), and the derived class's `DrawInstances` override — the
//     virtual that calls `DrawPrimitive` — is vtable slot 1.
inline constexpr std::uintptr_t k_draw_primitive_rva = 0x592C0;

class Chams {
 public:
  Chams() = default;
  ~Chams();
  Chams(const Chams&) = delete;
  Chams& operator=(const Chams&) = delete;

  // The operator's settings (the menu's shared settings block).
  void configure(const Config::Visuals& visuals) noexcept;

  // Install the draw hook. Idempotent; safe to call on every frame. Returns
  // false while the scene system is not up yet (the caller retries) or when
  // the hook could not be installed (the chams then do nothing).
  [[nodiscard]] bool install() noexcept;

  // The per-frame step (the payload's render thread, ~30 Hz): install the
  // hook — retrying until the game is up — keep the material in step with the
  // mode, and refresh the local player and the visibility filter.
  void update(const game::Snapshot& snapshot) noexcept;

  // The hook's callback: the scene system's render thread, inside the draw.
  //
  // `material` is the material the draw would use; the payload substitutes
  // its own when the entity is one of ours.
  void on_draw(void* desc, void* render_context, void* mesh_primitive, const void* material,
               const void* material_mode, void* draw_values, bool deferred, void* stats,
               void* layer) noexcept;

  // Diagnostics.
  [[nodiscard]] auto hook_installed() const noexcept -> bool {
    return hook_.installed();
  }
  [[nodiscard]] auto material() const noexcept -> game::Material {
    return material_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto drawn() const noexcept -> std::uint64_t {
    return drawn_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto skipped() const noexcept -> std::uint64_t {
    return skipped_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto original() const noexcept -> void* {
    return hook_.original();
  }

 private:
  // The material the current mode uses (0 when none could be resolved).
  [[nodiscard]] auto resolve_material(ChamsMode mode, game::MaterialHandle& handle) noexcept
      -> game::Material;

  // Write the operator's colour into the chams material and remember the
  // material's own colour, so it can be put back.
  //
  // Nothing happens when the material is not resolved or has no colour
  // parameter: the chams then draw with the material's own colour.
  void apply_colour() noexcept;

  // Put the material's own colour back.
  void restore_colour() noexcept;

  // Is the entity one of the players the visibility filter lets through?
  [[nodiscard]] auto is_visible(std::uintptr_t entity) const noexcept -> bool;

  // The facts about a player, packed into one word so the table can be
  // published with a single atomic store (a reader never sees a torn pair).
  //
  //   bits  0..46  the pawn address (a user-mode pointer is below 2^47)
  //   bits 47..48  the team (2 = T, 3 = CT)
  //   bits 49..50  the life state
  //   bit  51      the local player
  static constexpr std::uint64_t k_target_address_mask = 0x00007FFFFFFFFFFFULL;
  static constexpr int k_target_facts_shift = 47;

  hooks::InlineHook hook_;
  game::MaterialSystem materials_{};
  // The material system's handle for the chams material: the draw takes the
  // material, the material system's parameter setter takes the handle.
  game::MaterialHandle handle_{0};
  // The override material, published to the render thread.
  std::atomic<game::Material> material_{0};
  std::atomic<bool> enabled_{false};
  std::atomic<bool> only_enemies_{true};
  std::atomic<bool> include_local_{false};
  std::atomic<bool> visible_only_{false};
  std::atomic<int> local_team_{0};
  std::atomic<std::uintptr_t> local_pawn_{0};
  std::atomic<ChamsMode> mode_{ChamsMode::flat};
  // The mode the current material was resolved for, and the mode the
  // operator last asked for (the payload's thread only).
  ChamsMode resolved_mode_ = ChamsMode::flat;
  bool material_resolved_ = false;
  // The operator's colour and the material's own colour (the payload's
  // thread only; the render thread never touches the material).
  Color colour_{};
  float original_colour_[4]{};
  bool colour_applied_ = false;
  // The material has no colour parameter: the chams keep drawing with the
  // material's own colour and the payload stops asking for it.
  bool colour_failed_ = false;
  std::atomic<bool> install_failed_{false};
  std::atomic<std::uint64_t> drawn_{0};
  std::atomic<std::uint64_t> skipped_{0};
  // The players the chams may draw, packed (see `k_target_address_mask`).
  // The payload's thread writes them, the scene system's render thread reads
  // them; one atomic store per player means a reader never sees a torn
  // entry.
  static constexpr int k_max_targets = 64;
  std::atomic<std::uint64_t> targets_[k_max_targets]{};
  std::atomic<int> target_count_{0};
  // The players the visibility filter lets through (their pawn addresses).
  // A fixed-size array of atomics: the writer is the payload's thread, the
  // reader the scene system's render thread, so no lock is taken on the
  // render path.
  static constexpr int k_max_visible = 64;
  std::atomic<std::uintptr_t> visible_[k_max_visible]{};
  std::atomic<int> visible_count_{0};
  std::atomic<bool> visible_filter_{false};
};

}  // namespace cs2::features
