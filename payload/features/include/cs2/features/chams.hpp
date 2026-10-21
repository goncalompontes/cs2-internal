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
// **The render path is not free.** The facts about a drawn player (its team,
// its life state, whether it is the local player) come from the snapshot, which
// the payload already reads at ~30 Hz; the draw hook still has to recover the
// entity the desc belongs to and the view-model flag, and both are **engine
// reads** — the payload reads through the OS primitive, so a read is a
// syscall (~284 ns measured). The scene system draws every **primitive** of a
// model with the same desc, so the reads are cached per desc and paid once
// per **model**: without the cache they were four syscalls per primitive
// (0.57 ms per frame at 500 primitives, 2.3 ms at 2000) and they were paid
// for every primitive in the scene, not only for the players'. With the
// feature off, the hook does nothing but one atomic load.
//
// **Fail closed.** If the scene system is not up, the hook target is not
// where it should be, the material is not resolved, or the entity cannot be
// recovered, the draw is forwarded **untouched**.
//
// **The target is verified, not assumed.** The draw's address comes from the
// dumper's `scene_draw_primitive` signature (ADR-0012: no hard-coded RVA) and
// the bytes at the resolved address are read through the fault-safe reader
// and compared with the prologue the RE measured before anything is patched
// (`cs2/features/hook_target.hpp`). A mismatch refuses the hook and says
// why — the previous target came from an MSVC RTTI *lambda* type, and
// patching a static function with a member function's argument list took the
// game down with an access violation.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "cs2/features/esp.hpp"
#include "cs2/features/hook_target.hpp"
#include "cs2/features/recursion_guard.hpp"
#include "cs2/game/material.hpp"
#include "cs2/game/snapshot.hpp"
#include "cs2/hooks/inline.hpp"

namespace cs2::features {

// The scene system's `CAnimatableSceneObjectDesc::DrawPrimitive`.
//
// **Provenance** (scenesystem.dll sha256 040eb851…, image base 0x180000000,
// PE timestamp 0x6ACFCD4F):
//
//   * the class's own RTTI chain puts its vftable at `scenesystem.dll +
//     0x5E5C90`, and the vftable's slot 1 holds the *batch* wrapper — the
//     virtual that loops over the primitives and calls the real draw once per
//     primitive. The wrapper's own arguments put the material in the **fifth**
//     position, not the fourth;
//   * the real draw — the one the wrapper calls — is at `scenesystem.dll +
//     0x580A0`, and it is a **nine**-argument member function: `this`, the
//     render context, the mesh primitive, the `const IMaterial2*` (the
//     **fourth** argument), the material mode, the draw values, the deferred
//     flag, the per-frame stats and the scene layer. That is exactly the
//     signature the payload's detour uses, so **the detour and the typedef
//     never changed — only the address did**;
//   * the prologue at that address is the 20 bytes in
//     `hook_target::SCENE_DRAW_PROLOGUE`, and it is **unique** in the module.
//
// The address itself is **not** a constant here: it is resolved through the
// dumper's signature, so it is re-resolved on every build
// (`docs/re/chams.md` §8).

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
  // Whether the operator has the chams on.
  [[nodiscard]] auto enabled() const noexcept -> bool {
    return enabled_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto material() const noexcept -> game::Material {
    return material_.load(std::memory_order_relaxed);
  }
  // The draws the detour saw (every draw that reached the feature).
  [[nodiscard]] auto seen() const noexcept -> std::uint64_t {
    return seen_.load(std::memory_order_relaxed);
  }
  // The draws that were re-drawn with the chams' material.
  [[nodiscard]] auto drawn() const noexcept -> std::uint64_t {
    return drawn_.load(std::memory_order_relaxed);
  }
  // The draws that were forwarded untouched.
  [[nodiscard]] auto skipped() const noexcept -> std::uint64_t {
    return skipped_.load(std::memory_order_relaxed);
  }
  [[nodiscard]] auto original() const noexcept -> void* {
    return hook_.original();
  }

  // The draw hook's state: `installed` while the hook is in place, and the
  // reason it is not when it is not (no offset, no module, a prologue
  // mismatch, a refused patch).
  [[nodiscard]] auto hook_status() const noexcept -> hook_target::Status {
    return hook_status_.load(std::memory_order_relaxed);
  }

  // The re-entrancy guard the draw detour holds.
  //
  // It is the **shared** guard (`cs2/features/recursion_guard.hpp`): a
  // per-owner (per-thread) depth, so the scene system's other threads are
  // never mistaken for a recursion, and no `thread_local` is read on a path
  // the manually mapped payload has no TLS block for.
  //
  // The detour holds it in a `guard::Scope`, so it is released on every path.
  [[nodiscard]] auto draw_guard() noexcept -> guard::RecursionGuard& {
    return draw_guard_;
  }

  // The one-line status the payload's status block and the overlay's chams
  // line show: the hook's state, the draws seen, the draws chamsed and the
  // draws skipped.
  //
  // It is written into the caller's buffer and **never allocates**: the line
  // is built on the game's render thread, once per frame, and a heap
  // allocation there is a heap allocation in the game's frame budget.
  //
  // Returns false when the buffer cannot hold the line (it was truncated).
  [[nodiscard]] auto status_line(char* out, std::size_t size) const noexcept -> bool;

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

  // Record the hook's state and **latch** the log: a line is written only
  // when the state changes, so a refused hook cannot write a line per frame
  // (`game::append_log` opens, writes and closes a file per call).
  //
  // An empty `message` records the state without writing a line (the states
  // that are a normal part of the start-up, e.g. "the module is not loaded
  // yet").
  void report(hook_target::Status status, std::string_view message) noexcept;

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
  // The draw detour's re-entrancy guard (the shared one). It belongs to the
  // feature, not to the file scope: a detour has no context parameter, and
  // the guard is state (ADR-0010).
  guard::RecursionGuard draw_guard_{};
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
  // The draw hook's state, and the state the log last reported (the latch).
  std::atomic<hook_target::Status> hook_status_{hook_target::Status::idle};
  std::atomic<hook_target::Status> reported_status_{hook_target::Status::idle};
  // The counters: every draw the detour saw, the draws re-drawn with the
  // chams' material and the draws forwarded untouched. `seen == drawn +
  // skipped` always.
  std::atomic<std::uint64_t> seen_{0};
  std::atomic<std::uint64_t> drawn_{0};
  std::atomic<std::uint64_t> skipped_{0};
  // --- The desc cache --------------------------------------------------------
  //
  // The scene system draws every **primitive** of a model with the same desc,
  // so the two engine reads the draw path needs (the entity the desc belongs
  // to and the view-model flag) are paid once per **model** instead of once
  // per primitive.
  //
  // The entries are invalidated when the payload's per-frame step publishes a
  // new frame stamp, so a desc address the engine recycles cannot carry a
  // stale entity into the next frame. The invalidation is done by the render
  // thread itself (it owns the cache), so there is no cross-thread write.
  struct DescEntry {
    std::atomic<std::uint64_t> desc{0};
    std::atomic<std::uint64_t> entity{0};
    std::atomic<std::uint8_t> view_model{0};
  };
  static constexpr int k_desc_cache_size = 64;  // a power of two
  DescEntry desc_cache_[k_desc_cache_size]{};
  // The frame stamp the cache was filled for (the render thread's own).
  std::uint64_t desc_cache_stamp_ = 0;
  // The payload's per-frame step bumps this; the render thread notices and
  // drops the cache.
  std::atomic<std::uint64_t> frame_stamp_{0};
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
