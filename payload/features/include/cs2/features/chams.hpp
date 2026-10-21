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
// **Two threads are inside the detour at once.** The scene system renders
// the draw list in jobs on the tier0 thread pool, and the re-entrancy guard
// is **per owner**: it refuses a *recursion* and never another job's draw.
// So the draw path has **two** writers, and everything it shares has to be
// safe for two: the desc cache is a **per-slot seqlock** (see `DescCache`
// below), the guard's **policy** is per instance (the chams fail closed,
// the camera keeps failing open), and the hook's own state is atomic.
//
// **The target is verified, not assumed.** The draw's address comes from the
// dumper's `scene_draw_primitive` signature (ADR-0012: no hard-coded RVA) and
// the bytes at the resolved address are read through the fault-safe reader
// and compared with the prologue the RE measured before anything is patched
// (`cs2/features/hook_target.hpp`). A mismatch refuses the hook and says
// why — the previous target came from an MSVC RTTI *lambda* type, and
// patching a static function with a member function's argument list took the
// game down with an access violation.

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
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

// The desc cache: the table the draw path uses to pay the two engine reads
// (the entity a desc belongs to, and the view-model flag) **once per model**
// instead of once per primitive — the scene system draws every primitive of a
// model with the same desc.
//
// **Why it is a seqlock.** The scene system renders the draw list in jobs on
// the tier0 thread pool, and the per-owner guard lets **two** jobs into the
// draw path at once — that is the guard's whole job: it refuses only a
// *recursion*, never another thread's draw.
//
// The old cache published its entry with **three separate atomics** (the
// desc last, with a release store). That is a protocol that is safe for
// **one** writer and not for two: writer A stores `entity=A`, writer B
// stores `entity=B`, writer A stores `desc=A` (release). A reader that
// acquires `desc=A` synchronises with A's release — so A's `entity` store is
// visible — but the reader's `entity` load is relaxed and `entity`'s own
// modification order now has B **after** A, so the reader may legally observe
// `entity=B`. The result is **desc A with entity B**: the chams are decided
// on the **wrong player**.
//
// So every slot carries a **sequence word**: odd while a writer is inside the
// slot, even when the entry is stable. The writer claims the slot by taking
// the sequence from even to odd (one compare-exchange), writes the entry as
// one unit, and publishes the even sequence again. The reader takes the
// sequence, reads the entry, and takes the sequence again: if it changed, or
// if it was odd, the entry it read is not the entry it is looking at.
//
// The properties the protocol has to have:
//
//   * **a reader never pairs fields from two different writes** — that is the
//     point of the sequence word;
//   * **a writer never blocks a reader** — the sequence is a store, not a
//     lock. The reader retries a bounded number of times and then resolves
//     the desc itself, so the draw path never waits and never allocates;
//   * **the memory orders are justified** — see the comments on `lookup` and
//     `publish`.
//
// **The generation is part of the protocol.** Every entry records the
// generation (the frame stamp) it was filled for, and the reader only
// accepts an entry that belongs to the current generation. That is what
// makes the invalidation safe: the old code cleared the whole table with
// relaxed stores, which a publisher that was mid-publish could silently
// undo — and the clear itself was a data race between the two render
// threads. Here the invalidation **is** the generation: there is no clear to
// undo, and a publisher that was mid-publish when the frame advanced
// publishes an entry of the *old* generation, which the reader rejects.
//
// The slot index is unchanged — `(desc >> 4) & 63`, direct mapped, 16-byte
// granularity, no probe and no tag — because the collision rate is a
// property of the scene system's desc addresses and not of the protocol.
class DescCache {
 public:
  // How many slots the cache has (a power of two, so the slot is a mask).
  //
  // The name is the legacy one; it is not renamed here (a drive-by rename of
  // the old constants is noise).
  static constexpr int k_desc_cache_size = 64;

  // How many times a writer tries to claim a slot before it gives the entry
  // up. The caller keeps the values it computed either way: the entry is
  // simply not cached.
  static constexpr std::size_t CLAIM_ATTEMPTS = 4;

  // How many times a reader retries a slot that a writer is inside. The
  // writer holds the slot for a handful of stores, so the first retry
  // normally succeeds; when it does not, the reader resolves the desc
  // itself (the uncached path).
  static constexpr std::size_t READ_ATTEMPTS = 4;

  // What the cache knows about a desc.
  struct Entry {
    std::uintptr_t entity = 0;
    bool view_model = false;
  };

  // A new frame: the entries of the older generations become misses.
  //
  // The release: a reader that observes the new generation also observes
  // everything the payload published before the bump.
  void advance() noexcept {
    frame_stamp_.fetch_add(1, std::memory_order_release);
  }

  // The current generation.
  [[nodiscard]] auto stamp() const noexcept -> std::uint64_t {
    return frame_stamp_.load(std::memory_order_acquire);
  }

  // The slot a desc hashes to.
  [[nodiscard]] static auto slot_of(std::uint64_t desc_address) noexcept -> std::size_t {
    return static_cast<std::size_t>((desc_address >> 4) &
                                    static_cast<std::uint64_t>(k_desc_cache_size - 1));
  }

  // The cached entry for `desc_address`, or nothing.
  //
  // The reader never blocks: it reads, it checks the sequence, and it gives
  // up after a bounded number of tries.
  [[nodiscard]] auto lookup(std::uint64_t desc_address) const noexcept -> std::optional<Entry> {
    const Slot& slot = slots_[slot_of(desc_address)];
    for (std::size_t attempt = 0; attempt < READ_ATTEMPTS; ++attempt) {
      // The sequence is read **first**, with an acquire: the entry's reads
      // stay below it, so the reader never looks at an entry that belongs to
      // a write that had not been claimed yet.
      const std::uint64_t before = slot.sequence.load(std::memory_order_acquire);
      if ((before & 1U) != 0U) {
        // A writer is inside the slot: the entry is not readable yet.
        continue;
      }
      // The entry's own fields are relaxed: the sequence is what makes them
      // consistent, and they are published by the writer's release fence
      // below.
      const std::uint64_t entry_stamp = slot.stamp.load(std::memory_order_relaxed);
      const std::uint64_t entry_desc = slot.desc.load(std::memory_order_relaxed);
      const std::uint64_t entry_entity = slot.entity.load(std::memory_order_relaxed);
      const std::uint8_t entry_view_model = slot.view_model.load(std::memory_order_relaxed);
      // The acquire fence: the entry's reads stay **above** the second
      // sequence read, so a writer that moved the sequence under the reader
      // is seen and the entry is discarded.
      std::atomic_thread_fence(std::memory_order_acquire);
      const std::uint64_t after = slot.sequence.load(std::memory_order_acquire);
      if (before != after) {
        // The writer moved the sequence: the entry that was read is not the
        // entry that is there now.
        continue;
      }
      // The entry is stable. It is a hit only when it is the desc that was
      // asked for **and** it belongs to the current generation: an entry of
      // an older generation is a miss, because the desc address may have
      // been recycled since.
      if (entry_desc != desc_address ||
          entry_stamp != frame_stamp_.load(std::memory_order_acquire)) {
        return std::nullopt;
      }
      return Entry{static_cast<std::uintptr_t>(entry_entity), entry_view_model != 0};
    }
    // The slot never settled: there is no cached entry, and the caller
    // resolves the desc itself.
    return std::nullopt;
  }

  // Publish the entry for `desc_address`, resolved under the generation
  // `stamp`.
  //
  // Returns false when another writer held the slot: the entry is then not
  // cached, and the caller keeps the values it computed.
  auto publish(std::uint64_t desc_address, std::uint64_t stamp, std::uintptr_t entity,
               bool view_model) noexcept -> bool {
    Slot& slot = slots_[slot_of(desc_address)];
    // The claim: the sequence goes from even to odd, and only the writer
    // that took it may write the entry. The exchange is **acq_rel**: the
    // release keeps the claim visible before the entry's writes (so a
    // reader that sees the odd sequence cannot see the entry first), and the
    // acquire keeps the entry's writes below the claim.
    std::uint64_t sequence = slot.sequence.load(std::memory_order_relaxed);
    for (std::size_t attempt = 0; attempt < CLAIM_ATTEMPTS; ++attempt) {
      if ((sequence & 1U) != 0U) {
        // Another writer is inside the slot: the entry is not cached.
        return false;
      }
      if (slot.sequence.compare_exchange_weak(sequence, sequence + 1, std::memory_order_acq_rel,
                                              std::memory_order_relaxed)) {
        // The slot is ours: the entry is written as one unit.
        slot.stamp.store(stamp, std::memory_order_relaxed);
        slot.entity.store(entity, std::memory_order_relaxed);
        slot.view_model.store(view_model ? 1U : 0U, std::memory_order_relaxed);
        slot.desc.store(desc_address, std::memory_order_relaxed);
        // The release fence: the entry's fields are visible **before** the
        // sequence says the entry is stable again, so a reader that sees
        // the even sequence sees the whole entry.
        std::atomic_thread_fence(std::memory_order_release);
        slot.sequence.store(sequence + 2, std::memory_order_relaxed);
        return true;
      }
      // The exchange failed: another writer claimed the slot first (the
      // expected value is updated with what is there now).
    }
    return false;
  }

 private:
  // A slot: the sequence, and the entry it guards.
  struct Slot {
    // Odd while a writer is inside the slot, even when the entry is stable.
    std::atomic<std::uint64_t> sequence{0};
    // The generation the entry was filled for.
    std::atomic<std::uint64_t> stamp{0};
    std::atomic<std::uint64_t> desc{0};
    std::atomic<std::uint64_t> entity{0};
    std::atomic<std::uint8_t> view_model{0};
  };

  std::array<Slot, static_cast<std::size_t>(k_desc_cache_size)> slots_{};
  // The generation the cache is serving. The payload's per-frame step bumps
  // it; the render threads read it.
  std::atomic<std::uint64_t> frame_stamp_{0};
};

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
  // The draws the detour saw (every draw that reached the feature's
  // decision). A draw the guard refused never reaches the decision, so it
  // is not seen: it is counted as a **refusal** instead.
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
  // The draws the feature **dropped** instead of forwarding: the ones the
  // guard refused (the recursion, or the pool was exhausted) and the ones
  // that arrived while the hook was being torn down.
  //
  // They are counted separately because they are **not** in `seen`: a
  // refused draw never reached the decision, so `seen == drawn + skipped`
  // still holds and the refusal is visible on its own.
  [[nodiscard]] auto refused() const noexcept -> std::uint64_t {
    return refused_.load(std::memory_order_relaxed);
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
  // line show: the hook's state, the draws seen, the draws chamsed, the
  // draws skipped and the draws the feature refused.
  //
  // It is written into the caller's buffer and **never allocates**: the line
  // is built on the game's render thread, once per frame, and a heap
  // allocation there is a heap allocation in the game's frame budget.
  //
  // Returns false when the buffer cannot hold the line (it was truncated).
  [[nodiscard]] auto status_line(char* out, std::size_t size) const noexcept -> bool;

  // Count a draw the feature **dropped**: the guard refused it, or the hook
  // was torn down while the draw was in flight.
  //
  // The counter is separate from `seen_` on purpose: a dropped draw never
  // reached the decision, so `seen == drawn + skipped` keeps holding and the
  // refusal is visible on its own.
  void count_refusal() noexcept {
    refused_.fetch_add(1, std::memory_order_relaxed);
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
  //
  // The chams' guard **fails closed**: when every slot is held by another
  // owner the draw is refused (and counted) instead of being let in
  // untracked. An untracked call is not broken by the guard — the recursion
  // runs until the stack overflows and takes the renderer with it — while a
  // refusal drops one primitive. The camera's guard keeps the camera's
  // policy (fail open): the two guards are separate instances, so the
  // camera's behaviour does not change.
  guard::RecursionGuard draw_guard_{guard::RecursionGuard::Policy::fail_closed};
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
  // The counters: every draw that reached the decision, the draws re-drawn
  // with the chams' material and the draws forwarded untouched. `seen ==
  // drawn + skipped` always.
  //
  // They are **not** reset: the reset was three separate stores on the
  // payload's thread while the render threads were incrementing the same
  // counters, so a `seen` that landed between two of the stores left the
  // relation broken until the next reset. The operator reads the difference
  // between two `[state]` lines instead (`docs/known-issues.md` §11.1).
  std::atomic<std::uint64_t> seen_{0};
  std::atomic<std::uint64_t> drawn_{0};
  std::atomic<std::uint64_t> skipped_{0};
  // The draws the feature dropped instead of forwarding (the guard refused
  // them, or the hook was torn down while the draw was in flight).
  std::atomic<std::uint64_t> refused_{0};
  // The desc cache: the desc -> (entity, view model) table the draw path
  // uses. It is safe for the two writers the per-owner guard allows.
  DescCache desc_cache_{};
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
