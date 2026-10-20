# Neverlose, deeply — the index, and what it changes for us

> Research note, 2026-10-21. The **index** for the T17 deep-reversal
> (`docs/orchestration/spec-t17-neverlose-deep.md`), covering Routes A–D of
> the neverlose CS2 module: a **shipped third-party product**, read as
> *reference material only*. **Nothing from it is copied**; every claim in the
> linked briefs is a **description** with an evidence address.
>
> **Ground truth.** The raw live image
> `re/cs2_212C3300000/cs2_212C3300000.bin`
> (sha256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`,
> base `0x212C3300000`, size `0x5000000`, **`VA = base + file_offset`**),
> decompiled with Ghidra 12.1.4 project **`gproj11`** (the *corrected*
> deobfuscation, `DEOBF.md`).

---

## 0. The four briefs

| Brief | Route | What it settles |
|---|---|---|
| [`neverlose-renderer.md`](neverlose-renderer.md) | **A** | The **four shader constant buffers**, byte-verified out of the DXBC reflection; the **hull record**; where the colour lives; **where the depth state comes from** |
| [`neverlose-hooks.md`](neverlose-hooks.md) | **B** | How the module reaches the game: **seven of the eight "zero-caller" functions are in fact referenced**, five through hand-placed **tail-jump stubs**; what actually **drives the auto-strafe** |
| [`neverlose-protection.md`](neverlose-protection.md) | **C** | The **reflective loader** end to end, the **ten version-gated ntdll patterns** decoded, the **encrypted region**, the **fingerprints**, the **HWID/PDH** collection, and the **server-side CJSON** feature delivery |
| this file | **D** | The rage cluster and the movement builder — a **comparison**, not a reconstruction: what their ragebot does *not* do, and the one idea worth taking |

The earlier, broader survey of the same binary lives in
[`neverlose-mining.md`](neverlose-mining.md) (movement, aim, ESP, protection,
with the engine-interface facts). **Read that first** for the orientation;
these four briefs go deeper on the parts it left open.

---

## 1. What each route established

### Route A — the renderer (the best of the four)

* The module ships **28 DXBC shader containers**; their `RDEF` chunks carry
  the D3D11 constant-buffer reflection, so the layouts are **readable from the
  bytes** — no disassembly needed.
* Four families: **`PolygonConstants`** (5 shaders), **`TrajectoryConstants`**
  (7), **`ImpactConstants`** (6), **`TracerConstants`** (2) — plus 8 UI/text
  shaders.
* The **hull record** is `HullPoint` = **48 B** = `{float4 position_radius @0,
  float4 parent_growth @16, float4 appearance @32}`, 256 of them.
* The **colour is per-draw** (`hull_color`, one `float4`); the **per-hull**
  colour is `HullPoint::appearance`.
* The **depth state is not in the shader**: it is copied from a caller-supplied
  template into the `D3D11_GRAPHICS_PIPELINE_STATE_DESC` at `FUN_212c36bde17`
  and applied per draw with `OMSetDepthStencilState`. The *numeric* value is
  `unknown` statically — and that limit is stated, not hidden.

### Route B — the hooks

* A **ground-truth disassembly** (capstone, 13 785 980 instructions) **corrects**
  `OPEN_ITEMS.md` item 3: seven of the eight functions the prior session called
  unreferenced **are** referenced — five through a **one-instruction tail-jump
  stub**, two by direct `call`.
* The stubs themselves have **no static reference of any form** (0 absolute
  pointers, 0 RVAs, 0 rip-relative references) ⇒ they are runtime hook entry
  points, and the *game-side* target of each hook is `unknown`.
* The **auto-strafe is driven by a per-tick handler** (`FUN_212c38c66b8`,
  reached through the stub `0x212c38c65e4`) that keeps a tick counter and a
  `tick * 1/64` clock, and calls the movement builder
  `FUN_212c37beb50(ctx, cmd, state)`.

### Route C — the protection layer

* The **reflective loader** rebuilds the erased PE header and then finds
  **two unexported ntdll routines** with a **wildcard byte-pattern scan**
  (`0xFF` = any byte), gated on the Windows **build number** — **ten**
  patterns, all decoded out of the image in the brief.
* **New:** the loader reads its own `SizeOfImage` / `SizeOfHeaders` inputs from
  **inside the encrypted region** (`base+0x27ff7f6/8`), so the loader cannot
  even run from the static image — the key is runtime-derived, confirmed.
* `.enc1` (16.5 MB) measures **9.159 bits/byte**; exactly **one** data
  reference to it exists in the whole image.
* The **feature list is server-side** (CJSON) ⇒ *"not in the binary" is not
  "not in the product"*, which conditions every other audit of this binary.

### Route D — the maths (a comparison, deliberately short)

* **Their ragebot has no hit-chance model at all.** The weapon's spread is
  read only to pick a **smoothing rank**:
  `sp = sqrtf(cfg[0xc]² + cfg[0x10]²) / cfg[0x1c4]`,
  `rank = (int)((min(1, (sp-0.34)/0.66)) * 5)`. There is no damage field, no
  probability integral, no per-pellet model.
  ⇒ **On the shot model, ours is strictly more advanced than theirs.** There
  is nothing to steal there, and nothing to change.
* **The one idea worth taking** is their **16-step smoothing profile**:
  a per-target **deterministic seed**
  (`RandomSeed((handle & 0x7FFF) + weaponSetting * input)`), 16 keys
  `k = (i+1)*0.0625` each jittered by `RandomFloat(-0.025, 0.025)` with the
  last key forced to `1.0`, and the angle written as a **lerp** with weight
  `f130 * k`. The *same* target with the *same* weapon therefore produces the
  *same* jitter sequence every time — stable aim that still looks human.
* **Our open σ question is not answerable from a third-party cheat.** The
  neverlose image contains **no** `MD5_PseudoRandom`, no
  `CUniformRandomStream`, no `CalculateBulletSpread` and no pellet-pattern
  code (all four strings absent, ASCII and UTF-16) — so the engine's radial
  transform remains unconfirmed by this route. Our own
  `payload/aim/src/spread.cpp` already reproduces the engine's random stream
  exactly; the transform must be settled from the **game**, not from a cheat.

---

## 2. What this changes for us — the short, concrete list

1. **Chams: use their data model.** One constant buffer; **one `float4` colour
   per draw**; **one `float4` `appearance` per hull point**; the hull points
   in **world space** with an `inverse_view_projection` in the buffer; and a
   **pass selector** (`render_pass`) plus per-pass opacities
   (`shadow_opacity` / `outline_opacity` / `gradient_opacity` / `glow_width_px`
   / `outline_width_px`). Our overlay can build this from the studio hitboxes
   we already read. *(Route A)*
2. **Chams: own the depth state per pass.** The draw must set its own
   `D3D11_DEPTH_STENCIL_DESC` and call `OMSetDepthStencilState`; "visible
   through walls" is a property of *that*, not of the shader. *(Route A)*
3. **Strafe: assert one write per user command.** Their movement builder is
   called from a **per-tick** handler that keeps `tick * 1/64`. Ours must run
   in the same place; and when we hunt our own hook target, look for a 5-byte
   `E9 rel32` **into** a function, not for a pointer *to* it. *(Route B)*
4. **Loader: fail loudly on an unknown configuration.** One line, like theirs.
   And consider a **cheap integrity check over our own mapped image** — as
   robustness, not as anti-analysis. *(Route C)*
5. **Do not build a hardware fingerprint.** Their HWID collection is an
   anti-abuse machine identity; it is a privacy liability for our users with
   no benefit to them. *(Route C)*
6. **Aim: consider a stable, per-target smoothing profile** instead of
   per-shot noise — the deterministic-seed 16-step lerp is cheap and it is the
   one thing in their aim code that is better than a plain lerp. *(Route D)*
7. **Audit caveat.** Their feature list is server-side, so neverlose's binary
   is a **lower bound** on the product. Any "they don't have X" claim must
   carry that caveat. *(Route C)*

## 3. What we should **not** copy

* **The loader's stealth design** — opaque predicates, the encrypted region,
  the fingerprint gates, the hand-placed hook stubs. These are *anti-analysis*
  mechanisms; reproducing them would be copying a third party's evasion
  techniques. Explicitly out of scope, and not something our users benefit
  from.
* **The hardware fingerprint** (§2.5).
* **The separate-device renderer framework.** Their renderer owns a D3D11
  device and a pass-table framework; ours draws into the game's own device.
* **No third-party code, at all.** Every address, constant, byte pattern and
  shader-constant name in these briefs is a *description* of a binary — facts
  about their product — and not source we may reuse. Each brief carries the
  licence note.

---

## 4. The one recommendation

**Route A paid best**, and by a wide margin:

* it produced a **complete, byte-verified data model** in a single pass
  (four constant buffers, every field, plus the struct layouts);
* it needed **no disassembly at all** — the information was sitting in the
  shader containers' reflection chunks, and the only hard part was deriving a
  chunk format and *validating* it against a reference compiled with the OS's
  own HLSL compiler;
* and it is the route that feeds a feature we currently **cannot ship**
  (chams is refused because the draw hook target is wrong).

**The next task should be: our own chams renderer, built to the recovered
model, with its own per-pass depth state — and the draw hook target fixed.**

Concretely:

1. define one constant buffer with the recovered layout (per-draw colour +
   per-hull `appearance` + a pass id), and fill it from the studio hitboxes we
   already read;
2. give the chams pass its **own** `D3D11_DEPTH_STENCIL_DESC` — with
   `DepthEnable = FALSE` if we want x-ray — and set it per draw;
3. fix the draw hook target by looking for the *pattern* rather than the
   pointer (Route B's lesson: a hook entry is a 5-byte jump **into** the
   handler, and it may have no pointer to it anywhere);
4. while we are in there, close our **own** open σ question from the game
   (neverlose cannot answer it — §1 Route D), because it is a *gameplay-visible*
   bug and it is the last open item in `docs/research/README.md`'s bug table.

**What should not be the next task:** more neverlose static RE. The two
remaining `unknown`s (the numeric depth value, and the game-side hook targets)
are **runtime** questions by construction — they need a live process, not
another disassembly pass.

---

## 5. Reproducing this work

Everything in these briefs is reproducible from the raw image plus the Linux
box's Ghidra projects:

| To reproduce | Use |
|---|---|
| the shader layouts (Route A) | a **byte-level parser** of the `RDEF` chunk — the format is documented in [`neverlose-renderer.md`](neverlose-renderer.md) §0, and the method is validated against a reference shader compiled with `d3dcompiler_47!D3DCompile` |
| the hook bindings (Route B) | a **forward capstone sweep** of the code region — a byte scan for `E8`/`E9` cannot tell an instruction from a displacement (see the brief's §1.2) |
| the loader (Route C) | Ghidra project **`gproj11`**, corpus `ghout_deobf/loader.c`; the wildcard patterns are read straight out of the image |
| the rage/movement comparison (Route D) | Ghidra project **`gproj11`**, corpora `closure_rage_v2.c` / `closure_movement_v2.c`, and the prior session's `recon_rage_full.cpp` / `recon_movement_full.cpp` |

**A note on method.** The prior session's item 3 ("these functions have zero
static references") was produced with a byte-pattern scan and was **wrong**,
because a byte scan cannot distinguish an opcode from a displacement. Any
future "nothing references X" claim in this project should be checked with a
real disassembler first. That single correction is most of Route B's value.

---

**Licence note.** The Neverlose CS2 module is a **commercial third-party
product** with no open-source licence. It is used here as *reference material
only*: these documents **describe** observed behaviour, quoting addresses,
constants, field names and byte patterns as *facts about that binary*. No
third-party code is reproduced in this repository, and nothing here may be
used to reproduce or redistribute their binary or its contents.

> **Housekeeping.** `docs/research/README.md` indexes the research briefs and
> should gain a row for these four; that file was out of scope for this task
> (only the four briefs named in the spec may be created or changed here).
