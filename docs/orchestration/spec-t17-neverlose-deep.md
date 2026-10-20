# T17 — the deep neverlose reversal: the renderer, the hooks, and the protection layer

**Why this is the most important task in the project.** The neverlose CS2 module
is a **shipped product** with every feature we want and more, and the prior
session already reversed its **first-party surface**: 12 subsystems, **890 unique
functions**, with a corrected deobfuscation pass (`gproj11`). What it did **not**
do is the three things that matter to us, because they are the hard parts:

1. the renderer's **per-pass constant-buffer layouts** — the chams recipe;
2. the **hook bindings** — how they reach the game's functions at all;
3. the **protection layer** — the loader, the anti-tamper, the encrypted payload.

**Your worktree:** `C:\Users\gpmpo\cs2-wt\t17` (branch `feat/t17-neverlose-deep`).
**Do all your work there.** You will also need the **Linux box** (below).

---

## 0. Where the material is

| What | Where |
|---|---|
| The Linux box (Ghidra 12.1.4, the warm projects, the deobfuscator) | `ssh pontes@100.104.244.127` — **Tailscale SSH: the first connection asks for a one-time browser check**; the prompt prints the URL. |
| The working directory there | `/var/tmp/rea-work/` |
| A local copy of the notes and the corpora | `C:\Users\gpmpo\nea\rea-work\` (the `*.md` notes, `all/` the decompilation, `gproj3/`, `ghout_deobf/`) |
| The reconstructed PE (importable by REA **here**) | `C:\Users\gpmpo\nea\rea-work\neverlose_seeded3.exe` (85 MB, valid MZ, sha `21efa969…`) |
| The raw memory image | `C:\Users\gpmpo\cs2-internal\re\cs2_212C3300000\cs2_212C3300000.bin` (also on the box) |

**On the box, the Ghidra projects are the valuable part:** `gproj11` is the
**corrected** deobfuscation (`DEOBF.md`), and `gproj3`/`gproj10` are the earlier
ones with **9222 bytes of possibly-corrupted patches** — the prior session
documented that the earlier deobfuscation passes *mis-classified real
branches* and NOP'd live code, so **pseudocode from `gproj3`/`gproj10` may be
wrong**; prefer `gproj11` and the `_v2` corpora.

**The deobfuscation is already done correctly** — do not re-derive it:
`deobffix.tsv` (3100 verified sites) + `Rebuild.java` (restore the raw bytes,
then apply the verified patches). `DEOBF.md` is the definitive method and
`MANUAL_DEOBF.md` the verification.

**The corpora to read** (the authoritative pseudocode, from `gproj11`):
`closure_movement_v2.c`, `closure_rage_v2.c`, `closure_entity_v2.c`,
`closure_schema_v2.c`, `closure_visuals.c`, `closure_econ.c`, `closure_hwmon.c`,
`closure_lua.c`, `closure_render.c`, `loader.c`, and the per-subsystem
`recon_*.cpp` (which cite the `closure_*.c` line numbers).

---

## 1. Route A — the renderer's constant buffers (**the chams recipe**) — do this first

`SUBSYSTEM_visuals.md` §3 and `OPEN_ITEMS.md` item 1 leave exactly this open:
the four functions that `lea` a DXBC blob (`3668e6f`, `3c2b560`, `3c2b62f`,
`3d9e860`) are near-leaves; the pipeline is in their callers; the closure is
**128 functions** (`render_closure.txt`, `ghout_deobf/closure_render.c`); and the
**per-pass constant-buffer layouts are not extracted**.

**Deliverable:** the exact layout of each of the four shader families'
constant buffers — every register name, its byte offset, its type and its
meaning — **byte-verified from the DXBC reflection** (the prior session
reproduced `0x5D700411 = "RepresentativeTexture"` exactly, so the method works).
The four families are **Tracer**, **Impact**, **Trajectory**, **Polygon**.

Then answer, in the same report:

- which of the four draws a **player model** (the Polygon one, per the mining
  report), and therefore **which one the chams use**;
- how the **hull record** is built: the mining report says a `HullPoint` is
  `{position_radius, parent_growth, appearance}` and the colour is a
  `hull_color` float4 with a `global_alpha` and per-pass opacities — **confirm
  each field's offset and meaning from the reflection**, and state whether the
  colour is per-hull, per-entity or per-draw;
- whether the draw is **depth-tested** (i.e. whether it can be seen through
  walls) and what controls that — this is the single question our chams need.

**Why it matters here:** our chams are currently *refused* because the hook
target is wrong (`payload/features/src/chams.cpp`,
`k_draw_hook_target_verified = false`). Their renderer is a **separate D3D11
device**, not the game's — so it cannot be copied directly — but the **shader
model** (a world-space convex hull with a per-hull colour and an alpha) is
exactly the shape our overlay could draw from the studio hitboxes we already
read. Confirming the layout tells us whether our hull rendering can match it.

## 2. Route B — the hook bindings (how they reach the game)

`OPEN_ITEMS.md` item 3: seven functions have **zero** static references and are
reached only via **runtime-registered hooks/trampolines**:
`212c3790e70` (the driver), `212c37ff41e` (rage), `212c38c66b8`,
`212c3820bd0`, `212c3864260`, `212c387ada0`, `212c38af310`, and the **movement
handler `212c37beb50`** — which is the auto-strafe.

**Deliverable:** for each, the **binding** — what calls it, or what installs it.
The static image cannot answer it; the routes are:

- the **vtable** of a class it clearly belongs to (`C_BaseEntity` /
  `C_CSPlayerPawn` / the input object) — a member function that is only ever
  reached through the vtable shows up as a vtable entry;
- a **runtime-registered callback**: the game's event system
  (`EventClient{Pre,Post}Output_t`, `EventFrameBoundary_t`,
  `EventPostDataUpdate_t` — `FULL_DESCRIPTION.md` §3) registers them by name;
  find the registration;
- the `__Func`/closure tables the loader builds at start-up.

**Report** the mechanism per function, with the address that proves it, and
**state plainly which ones remain runtime-only** (the honest answer is allowed
and expected — the prior session marked them "not statically recoverable").

**Why it matters here:** the **movement handler** is their auto-strafe, and
knowing *what drives it* (a per-frame callback? the input system? a render
hook?) is worth more to us than the function body, because it tells us when our
strafe must run.

## 3. Route C — the protection layer (**the "safety features"**)

`FULL_DESCRIPTION.md` §1-2 and `DEOBF*.md` already cover the loader and the
obfuscation. What is **not** yet written down as a coherent account:

- the `.stub` reflective loader end to end: the header rebuild, the
  **version-gated ntdll routine** (the wildcard-scanned one; the build table
  `0x3AD6=15062, 0x42EE=17134, 0x47BA=18362, 0x585D=22621, 0x65F4=26100`), how
  the unwind info is registered, and what the module's entry point does next;
- the **`.enc1` region** (16.5 MB, `0x212c5cc0000`–`0x212c6d40000`, entropy
  ≥ 8.00) and the parts of `.mid`: the prior session proved there is **no
  static decryption loop** and exactly **one** data pointer to it
  (`0x212c5ca6516`), so the decryption is **runtime-derived**. Document what
  the runtime path must look like and what a **runtime dump** would have to
  capture to recover it;
- the **anti-tamper / integrity** machinery: the schema-validation checks (the
  `.enc1` reference, the version table, the fingerprint compares) and any
  self-check the module performs on its own image;
- the **HWID** path (`SUBSYSTEM_hwid.md`, WMI/SMBIOS) and the **hardware
  monitoring** (`SUBSYSTEM_hwmon.md`, PDH) — *what* it collects and *why*, with
  the addresses;
- the **server-side** side: `FULL_DESCRIPTION.md` mentions a server-side CJSON
  feature list. Document **what the client asks the server for and what it
  reports back** — that is the *feature delivery* mechanism, and it explains
  why some of their features are not in the client binary at all.

**Scope note, and it is a real one:** the *evasion* side of this (how the
module avoids VAC's detection, and what anti-cheat-evasion measures it
performs) is **not** what this task delivers, and nothing here may become an
evasion feature in our project. What this task delivers is the **architecture**
— how a shipped product loads itself, protects its own code and delivers
features — because that is what informs our **loader** (`payload/loader/`) and
our own integrity story. Document what the binary does; do not port the
mechanisms that defeat a third party's detection, and do not design a bypass.

## 4. Route D — the maths worth stealing (only if Routes A-C are done)

- the **rage cluster** (`recon_rage_full.cpp`, 25 KB) — the damage field, the
  hit-chance model, the spread/inaccuracy handling, the jitter. We have our own
  (`payload/aim/`); the question is whether theirs is **better**, and where.
- the **movement command builder** (`recon_movement_full.cpp`, 20 KB) — the
  strafe, the bhop, the anti-aim, and the command construction.
- the **schema system** (`recon_schema.cpp`, 41+ name-based class resolvers and
  the class-chain walk) versus ours — ours resolves classes by the generated
  dumper; theirs walks the schema chain at runtime, which is more robust to a
  game update.

## 5. Deliverables

Write into the repo, from your worktree:

- `docs/research/neverlose-renderer.md` — Route A: the four CB layouts, the
  hull record, the depth question.
- `docs/research/neverlose-hooks.md` — Route B: the bindings, with the
  runtime-only ones named as such.
- `docs/research/neverlose-protection.md` — Route C: the loader, the encrypted
  region, the integrity and anti-tamper machinery, the HWID/hwmon collection,
  and the server-side feature delivery.
- `docs/research/neverlose-deep.md` — the index and the cross-links, plus
  **"what this changes for us"**: a short, concrete list of what we should
  change in our code, and what we should **not** copy.

Use the repo's research-brief house style (`docs/research/README.md`): a
summary table, the evidence with **addresses**, the confidence label
(`verified` / `inferred` / `unknown`) on every claim, and the licence note
(third-party material is **described**, never copied).

## 6. Rules

- The repo's style guide and the licence note in `docs/research/README.md` apply.
- **Describe, do not copy.** No third-party code goes into our tree.
- The evidence must be an **address in the neverlose image** (or the box's
  corpus), not a paraphrase.
- If a claim cannot be established, say `unknown` and say what you tried. The
  prior session's `OPEN_ITEMS.md` is a good model for that.
- Commit in logical pieces (`docs(research): …`).

## 7. Report

Per route: what you established (with addresses), what you could not, and the
"what this changes for us" list. Then the **one** recommendation: which of
Routes A-D gave us the most, and what the next task should be.
