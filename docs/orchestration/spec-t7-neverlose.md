# T7 — Neverlose mining (item 6 of the feature wave)

**Goal:** use the **reversed Neverlose CS2 module** as a second source of truth for
the features this wave builds, and steal the specific techniques that are worth
stealing.

**Status: blocked.** The Neverlose reversal work products live on a Linux machine
(`/var/tmp/rea-work/`) that the operator is making available. This spec is ready
to run the moment that machine is reachable.

## 0. Where the artifacts are NOW (they were pulled locally)

The Linux box (`pontes@100.104.244.127`, ssh alias `laptop`) is reachable, and
the reversal artifacts have been **copied to this machine**:

| What | Local path | Size |
|---|---|---|
| the reversal notes + reconstructions | `C:\Users\gpmpo\nea\rea-work\` (the `*.md` and `recon_*.cpp`) | ~250 KB |
| the raw decompilation | `C:\Users\gpmpo\nea\rea-work\all\funcs_000.c` … `funcs_006.c` + `index.tsv` | 47 MB |
| **the Ghidra project** (the full analysis, 277 patches) | `C:\Users\gpmpo\nea\rea-work\gproj3\myproj3.gpr` + `myproj3.rep` | 283 MB |
| the deobfuscated dumps | `C:\Users\gpmpo\nea\rea-work\ghout_deobf\`, `ghout\` | 38 MB |
| **the reconstructed loadable PE** | `C:\Users\gpmpo\nea\rea-work\neverlose_seeded3.exe` (85 MB, valid MZ, sha256 `21efa969cacf8e8f5c36066a0ac9de0bd11ae3184bcc1a562892839e31e16832`) | 83 MB |
| the PE's imports / export seeds | `imports.txt`, `seeds.txt` | 0.5 MB |
| the Ghidra scripts and the run logs | `Deobf.java`, `DumpAll.java`, `DecompTargets.java`, `*.log`, `deobffix.tsv` | ~2 MB |

**The reconstructed PE is the important one for future work**: it is a valid PE
(imports from `imports.txt`, 57 076 export seeds), so REA/Ghidra can import it
**on this machine** and the analysis no longer has to live on the Linux box.

The remote box is still reachable (`ssh laptop`) for anything the local copy
lacks — e.g. the Ghidra projects `gproj4/5/10/11` and `export/` (1.6 GB total
there; only the useful part was pulled).

## 0.1 What is where (the original map, for reference)

| Where | What |
|---|---|
| **Linux box, `/var/tmp/rea-work/`** | The reversal work: `neverlose_cs2.dll` (a live memory image), `recon_*.cpp` (the per-subsystem pseudo-C++), `SUBSYSTEM_*.md` (the notes), `all/index.tsv` + `all/funcs_*.c` (the decompilation), `gproj3` (the Ghidra project), `Deobf.java` (the deobfuscator). |
| **This repo, `re/cs2_212C3300000/`** | The raw image (`cs2_212C3300000.bin`, SHA-256 `3224604483481c57a18ccfb20448a5e634a9583421abfac3a2712bba5cc77f27`, base `0x212C3300000`, size `0x5000000`) plus a PE wrapper (`cs2_212C3300000.pe.bin`), a strings dump and four small Python helpers (`kw.py`, `rvdis.py`, `ctxs.py`, `hexd.py`). |
| **This repo, `NEXT_SESSION_PROMPT.md`** | The reversal plan the Linux box was following. |

**The image is obfuscated and encrypted at rest**: opaque predicates
(`mov rax,gs:[0x60]` → fingerprint → `je`), arithmetic constant obfuscation, junk
decoy blocks, and an encrypted region (`~0x29c0000–0x3a40000`, `.enc1`). A plain
strings grep over the image returns almost nothing useful — it returns the
*libraries* (OpenSSL, Crypto++, LuaJIT, asio, websocketpp, protobuf, camellia).
**Do not treat a "no match" as "the feature is absent"** — the string may be
encrypted or decoded at runtime.

## 1. Access

The operator is making the machine available. Confirm the access method with
them before starting (SSH? a mounted share? a copy into this repo?). Do **not**
assume, and do not start a multi-hour Ghidra analysis before you know you can
read the results.

## 2. What to mine — in priority order

Each item is a question Neverlose has already answered, asked **because this
wave needs the answer**.

### 2.1 Movement / autostrafe (task T3)

`NEXT_SESSION_PROMPT.md` §7.4 points at the roots `FUN_212c37beb50` (movement),
`FUN_212c3617466` (tick/fakelag), `FUN_212c382b860` (auto-duck) and a
pre-computed closure (`movement_closure.txt`, ~104 functions).

Questions:
1. How does Neverlose compute the **wish direction**? Does it use the player's
   held keys (WASD) as a heading, or only the camera? **This is the direct
   comparison for T3's "directional autostrafe"** — if Neverlose does the same
   thing, compare the formulas; if it does something smarter, say what.
2. How does it pick the **side**? Is there a dead-zone / hysteresis / a
   "last side" latch? (T3 invents one; Neverlose may have solved it better.)
3. What **magnitude** does it write, and on which channel?
4. Does it read the **buttons before or after** its own writes (the latch
   landmine)?

### 2.2 Camera / third person (task T5)

Question: does Neverlose touch the camera at all? If it has a third-person or a
camera-modification feature, find:
- which struct/offset it writes,
- the field layout (distance, angle, pivot, FOV, side),
- whether it goes through a convar or a direct struct write.

If Neverlose has no such feature, that is a valid answer — say so and stop.

### 2.3 Chams / visuals (task T6)

`NEXT_SESSION_PROMPT.md` §7.6 names the roots `FUN_212c38d7fa0`, `FUN_212c3860600`,
`FUN_212c3ac3d10`, `FUN_212c3ac8880` and the DXBC-referencing functions
(`36d4b00`, `377b21a`, `3668e6f`, `36782f0`, `377ac39`, `39f47b0`, `39f6f90`,
`39f4b00`, `39eee66`, `3a39070`) with the shader constant buffers
**Tracer / Impact / Trajectory / Polygon**.

Questions:
1. How does it get a **flat material**? (a name, a `CreateMaterial` call, a
   pre-existing engine material?)
2. How does it **override the material** for a player model? (a `DrawModel` hook?
   a scene-layer? a render-mode flag?)
3. How does it get the **colour and the alpha**? (`SetColor`? a `CMaterialParams`?
   a per-draw uniform?)
4. What are the **Tracer / Impact / Trajectory / Polygon** shaders for — are some of
   them the ESP's tracers and world markers (which would tell us how their ESP
   draws lines and impact effects)?

This is the single most valuable answer for T6: Neverlose demonstrably does
chams, so it has a working recipe.

### 2.4 ESP / visuals generally

How does Neverlose decide **what to draw** and **when**? Which entity classes
does it special-case (grenades, C4, dropped weapons, smokes)? Does it have a
radar? What does it do for **off-screen targets**? (T4 invents these; compare.)

### 2.5 The anti-cheat / protection layer (informational only)

`NEXT_SESSION_PROMPT.md` §7.1 and §4 describe the loader, the protection and the
deobfuscator. **Report what the protection is and how it is detected**, so this
project knows what it is up against — but **do not** implement evasion, and do
not port anything that exists to hide the payload from a third party. The
project's own loader (`payload/loader/`) is already written for this.

## 3. Method

1. Read `/var/tmp/rea-work/README_export.md` for the artifact inventory and
   `NEXT_SESSION_PROMPT.md` for the reversal state.
2. **Read the existing `recon_*.cpp` and `SUBSYSTEM_*.md` first** — they are the
   product of the prior sessions. The prompt warns that they contain *inferred*
   parts: "do NOT trust `reconstruction_all.cpp` /
   `reconstruction_aim_movement.cpp`; regenerate them from recovered bodies."
   So: read them for orientation, then **verify every claim you intend to use**
   against the decompiled body.
3. For each question: find the function, read its body, and record the
   **address** of the evidence.
4. Use `Deobf.java` where the body is still obfuscated.

## 4. Deliverable

`docs/research/neverlose-mining.md` in this repo:

- a table: **question → Neverlose's answer → our answer → same or different →
  what we should change**;
- for each answer, the **evidence address** in the Neverlose image;
- an explicit list of the answers you could **not** get, and what you tried;
- a short "ideas worth stealing" list with an effort estimate for each.

Then, for each idea worth stealing that is **cheap and safe**, implement it in
this repo as a separate, small commit — but only after the operator has seen the
mining report and agreed.

## 5. Rules

- **Never** copy Neverlose code verbatim into this repo. Read it, understand it,
  write our own. (The repo already has a licence-notes discipline in
  `docs/research/`; follow it.)
- Never invent an answer. "Not present" and "not found" are valid answers; a
  plausible fabrication is not.
- The image is a **live memory dump of a third-party product**. Treat it as
  read-only reference material.
