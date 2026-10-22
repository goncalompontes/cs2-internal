# FIX-1 — the findings of the adversarial review of T1 (settings + config foundation)

**Your worktree: `C:\Users\gpmpo\cs2-wt\fix1`**, branch `fix/t1-review`, based on
`ae49b72` (the integration tip). Do all your work there; never touch
`C:\Users\gpmpo\cs2-internal` or another agent's worktree.

**Read first:** `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` and
`C:\Users\gpmpo\cs2-internal\docs\style-guide.md`.

The review's verdict was **DO NOT SHIP**. Below is the condensed finding list with
the fix guidance. **The review's own reasoning is authoritative** — if you think a
finding is wrong, say so with evidence rather than silently skipping it.

---

## Blockers

### B1 — "Save" persists the **defaults** and wipes the user's live settings

`payload/src/payload_main.cpp:60` holds `features::Config config;` inside
`Runtime`. It is **never synced with the live settings block** — the only writes
to it are `apply_pending_op` and `set_config_path`. So the sink at
`payload_main.cpp:204-208`:

```cpp
if (cs2::features::apply_pending_op(runtime->config, settings)) {
  const cs2::overlay::wire::Settings loaded = cs2::features::to_wire_settings(runtime->config);
  runtime->menu.settings() = loaded;
  runtime->publisher.publish_settings_block(loaded);
}
```

passes a config that still holds the **defaults**. `apply_pending_op` therefore
**saves the defaults** to the file, and the write-back resets the live block, so
the user's ESP toggles visibly snap back to the defaults in the same frame.

**Fix:** the config must be **current** before the op runs — the sink must
rebuild it from the live block first:

```cpp
runtime->config = cs2::features::from_wire_settings(settings);
```

and the write-back must happen **only when the op actually changed the
configuration** (a *save* changes nothing). `features::Config` has a defaulted
`operator==` — use it: capture the config before the op and compare after.

### B2 — a config **Load** never reaches the payload's ESP in the Vulkan-layer path

`payload/features/src/model_publisher.cpp:212-224`, `publish_settings_block`:

```cpp
shared->settings_sequence.store(sequence + 1, ...);  // odd: writing
std::memcpy(&shared->settings, &settings, sizeof(settings));
shared->settings_sequence.store(sequence + 2, ...);  // even: stable
settings_sequence_ = sequence + 2;                   // <-- swallows the change
```

`settings_sequence_` is the **only** change-detection variable in
`apply_settings` (`model_publisher.cpp:250-254`):

```cpp
if (sequence != settings_sequence_) {
  settings_sequence_ = sequence;
  overlay_->set_esp_config(from_wire_settings(copy));  // the ONLY path to the ESP config
}
```

Because the write sets `settings_sequence_` to exactly the value the segment now
holds, the next `apply_settings()` sees no change and never calls
`set_esp_config`. The review reproduced it:

```
frame 1: esp config adopted the layer's block: 1 (esp.box=0)
frame 2: esp config updated from the published block: 0
the loaded configuration reached the payload's ESP config: NO
```

**Fix:** `publish_settings_block` must **not** update `settings_sequence_` — the
next `apply_settings()` has to see the sequence difference and adopt the block.
(Think about which side owns the change detection.)

**The second half of B2:** the **layer** latches its settings copy once
(`overlay/src/renderer.cpp:672-680`, `static bool settings_loaded`), so the layer
never re-reads the block; the next time the user touches the layer menu it
writes its **stale** copy back and the load is reverted. The layer must
**re-read the settings block when it changes** (the transport already has a
seqlock read: `overlay/src/transport.cpp:186-198`).

**This means `overlay/src/transport.cpp` and `overlay/src/renderer.cpp` are in
scope for this task** — they were not in T1's ownership, but they are the layer
side of the same defect.

---

## Majors

### M1 — the payload becomes a **second writer** to a cross-process seqlock

`publish_settings_block` writes `shared->settings` at runtime while the layer's
menu writes the same block (`overlay/src/transport.cpp:214-223`). A seqlock
assumes **one** writer; the two processes share no lock (`g_lock` in
`transport.cpp` is process-local). Interleaving can make a reader accept a
**torn** block (a colour byte from one write, a flag from the other).

**Fix:** make the settings block **self-verifying** so a torn read is always
detected, regardless of how many writers there are:

- add a checksum field for the settings block to `wire::Shared`
  (`overlay/include/cs2/overlay/wire.hpp`) and write it **inside** the seqlock
  window, after the block;
- both readers (`payload/features/src/model_publisher.cpp`'s `apply_settings` and
  `overlay/src/transport.cpp`'s `read_settings`) verify the checksum of the copy
  they took and **retry** on a mismatch (a torn read then always fails the
  check);
- the checksum must be computed **identically** by both builds, so put the
  function in `wire.hpp` itself (it is a header both sides already include) —
  a simple FNV-1a over the block's bytes is enough;
- **bump `wire::k_version`** (12 → 13) and check that the layer's transport
  actually refuses a mismatched version before it reads the block — a stale
  layer binary must not read a block whose checksum it does not know.

### M2 — `menu_config_path` is never published into the live block

`set_config_path(runtime->config)` mutates the payload's *config copy*, which is
only converted back into the wire block inside the `if` at line 204 — false on
every frame that no op runs. So `menu_config_path` stays **empty** in the UI for
the whole session, and the comment above the call states the opposite of what
the code does.

**Fix:** publish the path into the **live** block (the in-process menu's
`settings_`, and the shared segment in the layer path) — and do it in a way that
does not clobber the user's in-flight edits. It is a read-only display field, so
it only needs to be written when it actually changes.

### M3 — the host test cannot detect a **mis-paired** conversion

`payload/features/tests/test_features_config.cpp` builds a sentinel block and
asserts the round trip is byte-identical. A round trip only proves
`to ∘ from = id`; a **consistent swap** of two fields is invisible whenever the
two sentinel values are equal — and the sentinel uses `1` for ~30 boolean
fields, so every bool-to-bool swap is undetectable. The review proved it:

```
# swapped esp_skeleton <-> esp_tracer in BOTH directions:
features-config: ok        <-- exit 0, the test passes
```

**Fix:** make the test **pairing-sensitive**:
- give every field a **distinct** sentinel value (not a shared `1`);
- add a **per-field differential** test: mutate exactly one wire field, run
  `from_wire_settings`, and assert that **exactly one** config field changed and
  that it is the expected one. A consistent swap then fails.

The review's own audit found the *current* pairing is correct (0 mismatches) —
the finding is that the **test** does not protect it.

### M4 — the write-back destroys unrelated live state

`to_wire_settings(runtime->config)` carries the **default** rage/menu state, so
every config op also zeroes the live block's `menu_open`,
`menu_config_op` / `menu_config_token` and `rage_config_op` /
`rage_config_token`. A pending ragebot per-weapon op that the menu set in the
same frame is silently discarded.

**Fix:** the write-back must preserve the **live** op/token fields (and
`menu_open`) — they are protocol state, not configuration. Build the new block
from the loaded config and then copy those fields across from the block that was
live.

---

## Minors (fix these too — they are cheap)

- **m1** `payload/features/include/cs2/features/settings.hpp:13-22` — the doc
  comment is now wrong ("`EspConfig` -> the settings block. Fields the ESP does
  not carry (movement, rage key, `show_hud`, `menu_open`) are left at their
  struct defaults" / "clamping the enum-like fields"). All of those fields are
  mapped and all numerics are clamped. Fix the comment. (The public API is still
  spelled `EspConfig` — T1 kept `using EspConfig = Config;` because
  `payload/features/include/cs2/features/overlay.hpp` names it. **Now that you
  own the feature's headers, drop the alias** and update `overlay.hpp` and its
  users, or keep the alias and say why in your report.)
- **m2** `payload/features/src/config_store.cpp:37` — the file-scope
  `std::atomic<std::uint32_t> last_op_token` is **new global mutable state**,
  which ADR-0010 forbids ("State is passed explicitly or owned by an explicit
  context object"). The style guide's accepted exception is the Vulkan layer's
  C-ABI entry points, not `payload/features`.
  **Fix:** move the token state to the caller. Change the API to take an explicit
  state object, e.g.
  `apply_pending_op(Config&, const wire::Settings&, ConfigOpState&) -> bool`,
  where `ConfigOpState` holds the last token and is owned by the payload's
  `Runtime`. Also add
  `static_assert(std::atomic<std::uint32_t>::is_always_lock_free)` if any atomic
  remains, so a non-lock-free standard library cannot silently reintroduce the
  CRT dependency.
- **m3** `payload/features/include/cs2/features/model_publisher.hpp:83` declares
  `publish_settings_block` unconditionally, but its definition sits inside the
  `#if defined(_WIN32)` region of the `.cpp` → a non-Windows build has a
  declared-but-undefined symbol. Guard the declaration like its siblings.
- **m4** `payload/features/src/model_publisher.cpp:226` — the body of
  `apply_settings` was collapsed onto the signature line; `clang-format` reflows
  it. Run `clang-format --style=file` on every file you touch.
- **m5** `payload/features/src/config_store.cpp:832` — `apply_pending_op` is not
  `noexcept` and allocates (`std::string`, `std::filesystem::path`), yet it is
  called from the `noexcept` sink `on_movement_settings`. The payload is built
  with `/MD /EHsc` (exceptions **enabled**), so a `std::bad_alloc` inside the
  store unwinds into a `noexcept` frame → `std::terminate` → the game dies. It
  also does synchronous file I/O on the game's present thread → a frame hitch.
  **Fix:** make the op path not throw out of a `noexcept` frame (catch inside
  the store, or make the store's failure path allocation-free) and consider
  doing the file I/O off the render thread.
- **m6** `payload/src/payload_main.cpp:206` — the sink's contract is
  `const wire::Settings&`, but in the in-process path that reference **is**
  `runtime->menu.settings()` and the handler assigns to that very object. It is
  well-defined today, but the `const&` parameter is a lie. **Fix:** take a copy
  of the block at the top of the sink, or have the handler not write through the
  parameter.

## Nits (fix if cheap)

- **n1** `config_store.cpp:798-805` — a UTF-8 BOM (what Notepad adds by default)
  makes the first line not start with `#` and contain no `=`, so the whole file
  is rejected as corrupt. **Skip a leading BOM when parsing.**
- **n2** `config_store.cpp:90-110` — `parse_color` reads exactly four
  components and ignores the rest: `esp.color_enemy = 1,2,3,4,5` parses as
  `1,2,3,4`. **Reject a wrong component count.**
- **n4** `config_store.cpp:773-781` — `set_config_path` truncates the published
  path to 127 characters while `default_config_path()` is not truncated → the
  menu can show a different path from the one that is used. **Make them agree**
  (truncate the used path too, or publish the full path in a longer field).

---

## Not your problem (explicitly out of scope)

- The **neverlose** / scenario-API workstream is a separate task.
- `sizeof(wire::Shared)` differs by 3 bytes between gcc and clang-cl. The review
  proved this is **pre-existing** and harmless (all member offsets agree), and
  the `pack(1)` + `std::atomic` combination is the cause. Do not "fix" it, but
  do **not** make it worse: if you add a field to `Shared`, keep it inside the
  same `#pragma pack` region and check the offsets still agree.

## Verification

- `meson compile -C build && meson test -C build` → the suite must be green
  (18/18 at baseline; your new tests add more).
- `meson compile -C build-payload`, `-C build-overlay`, `-C build-injector` → all clean.
- **Write a hostile test for each blocker** and show it failing before the fix
  and passing after. The review's own reproductions are the model:
  - a "save" round trip that starts from a **non-default** configuration and
    asserts the file contains the **live** values (B1);
  - a "load" that asserts the loaded block reaches `set_esp_config` /
    `apply_settings` on the **next** frame (B2);
  - a **torn-write** simulation that interleaves two writers and asserts the
    reader detects it via the checksum (M1).
- Commit **one commit per finding group** with a `fix(config): …` message, or
  one commit per blocker/major if that is cleaner. Do not amend.

## Report

For each finding: **fixed / not fixed (with why)**. Plus: the new API of
`apply_pending_op`, the checksum design, and the exact test you added for each
blocker. And anything you found that the review missed.
