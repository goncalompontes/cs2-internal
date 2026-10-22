# T38 — the spread patterns: the engine's real rules

> **The one-line version:** the engine's spread sampler has **three** rules the
> payload does not have, and the repo's own model of it is **wrong for
> multi-pellet weapons**: the engine draws the **spread** radius and angle
> **once per shot** (pellet 0 only) and the **inaccuracy** radius and angle
> **per pellet** — the payload draws all four per pellet, so its RNG stream
> **desynchronises from pellet 1 onward**. On top of that, the attack type is
> `m_weaponMode` (already in the payload's SDK), the revolver's `1 − r²` also
> requires `attackType == 1`, and the negev's `r → 1 − r^(2^k)` is
> **per-shot**, with `k = 3 − recoilIndex`, not a fixed `k` and not a distance
> test.

---

## 1. What the RE established (2026-10-22, build `659940c1`)

The full RE report is the source of truth for this task. The findings that
matter here, with their confidence:

### 1.1 The pellet draw order — the payload's model is **wrong**

> *"The sampler does **not** draw `r1, a1` per pellet. The engine draws the
> **spread** radius + angle **only for pellet 0** (or for every pellet when
> `weapon_accuracy_shotgun_spread_patterns` is on); the **inaccuracy** radius +
> angle are drawn **per pellet**."* — **verified on both builds.**

The payload's `payload/aim/src/spread.cpp::pellet_offsets` draws
`r1, a1, r2, a2` **per pellet** (`spread.cpp:155-165`). For a **single-pellet**
weapon the two agree. For a **multi-pellet** weapon the RNG stream
**desynchronises from pellet 1 onward**, so every pellet after the first is
wrong.

**This is a defect in the payload's spread model**, not a documentation
discrepancy, and it propagates into the Monte-Carlo solver's per-pellet
attribution and its union-over-pellets.

### 1.2 The attack type is `m_weaponMode`

`*(int32*)(weapon_entity + 0x1A00)` = `C_CSWeaponBase::m_weaponMode`,
`Primary_Mode = 0` / `Secondary_Mode = 1` (`CSWeaponMode`).

**The payload's SDK already has it** — `payload/sdk/schemas/client_dll.hpp:1005`:
`{ "weapon_mode", "SWeaponMode", 0x1a00, "m_weaponMode" }`. **No new dumper
signature is needed.**

**Caveat the implementer must respect:** `m_weaponMode` is a **sticky** flag —
one path sets it to 1, another consumes **and clears** it. The payload
computes the cone *before* the engine fires, so the field holds the engine's
**previous** decision. The primary-fire path reads it and clears it
afterwards. **Say what the payload should read and why**, and state the
ambiguity: three of the four `CSBaseGunFire` call sites do **not** pass
`m_weaponMode` (they pass `ammo_in_clip & 1`, or a literal `0`, or a shot-struct
field). **The implementer must not pretend this is settled** — if the payload
cannot know the engine's attack type for every path, say so, and make the
solver **refuse** rather than guess where it matters.

### 1.3 The revolver needs **two** conditions

`r → 1 − r²` requires **the weapon id == the `weapon_revolver` item id** *and*
**`attackType == 1`**. The payload's current rule is the **designer name**
alone (`rage_objective.hpp`: `same_name(weapon_short_name(weapon), "revolver")`),
which is **not** equivalent.

The VData has a boolean discriminator: **`m_bIsRevolver` at `+0x71E`**
(verified). The engine compares **ids**.

### 1.4 The negev's rule is per-shot, and `k` is not fixed

* the "distance" is **`m_flRecoilIndex` = `*(float*)(weapon_entity + 0x1A28)`**
  — a per-shot counter, `+1.0` after every shot, reset to 0 when the weapon
  stops firing. **Not a distance.** The repo's `docs/re/aim.md` wording
  (*"negev (dist < 3)"*) is misleading.
* **`k = 3 − recoilIndex`**: recoil index 0 → `r → 1 − r⁸`, 1 → `1 − r⁴`,
  2 → `1 − r²`, ≥ 3 → no transform. So it is the **first three shots of a
  burst**.
* the SDK has it: `recoil_index_2` at `0x1a28`
  (`payload/sdk/schemas/client_dll.hpp:1013`).

### 1.5 The pattern table is a hash map the payload can reproduce

* a global map at **`0x1822195e8`** (RVA `0x22195e8`): `uint16 weapon id` →
  a **0x608-byte block**;
* the block: `+0x004` / `+0x204` / `+0x404` = three regions of 64 ×
  `{float angle, float radius}`; `+0x604` = an "invalid" flag;
* **region 2** is the **shotgun pellet pattern** — the one the *bullet* path
  reads. Regions 0 and 1 are the **recoil/crosshair** path, which the bullet
  does not use;
* the region-2 values are generated from the weapon's **VData** —
  `num_bullets 0x730`, `recoil_angle 0x790`, `recoil_angle_variance 0x798`,
  `recoil_magnitude 0x7A0`, `recoil_magnitude_variance 0x7A8`,
  `recoil_seed 0x7D4`, `spread_seed 0x7D8` — with a `CUniformRandomStreamImpl`
  seeded with **`spread_seed`**, `angle = U(0, 2π)` and
  `radius = U(k/N, (k+1)/N)` with `N = num_bullets`, `k = min(counter, N−1)`;
* the index is **`recoilIndex * numBullets + pelletIndex`**, and it must be
  `< 0x40` — otherwise the engine **falls back to the random cone**;
* the whole path is gated on the convar
  **`weapon_accuracy_shotgun_spread_patterns`**, which is **off by default** —
  so a shotgun uses the **random cone** unless the server enables it.

### 1.6 The repo's addresses are stale

`docs/re/aim.md` carries the **old** build's addresses. On the current build:

| | old (`44af2aa0`) | current (`659940c1`) |
|---|---|---|
| `FX_FireBullets` | `0xd22c50` | **`0xd237d0`** |
| the cone sampler | `0xd23460` | **`0xd23fe0`** |
| the pattern lookup | `0x7cd1b0` | **`0x7cdad0`** |
| the pattern table global | `0x1822195f8` | **`0x1822195e8`** |

**A trap:** on the current build the *old* `0x7cd1b0` now falls **inside a
different function** (`FUN_1807cd010`, the crosshair/recoil consumer). Anyone
re-deriving by address translation will get the wrong function.

**And the live module has moved again**: the file on disk is now sha256
**`5a46a09b521bbd607012146364dafae2abfa50364246638391990d4c7dfb9d49`** (mtime
2026-10-21 18:37) — the game **auto-updated**. The RE re-checked every cited
RVA against the on-disk build with capstone and **all of them still hold**.
**The schema offsets are unaffected.** (The payload's *calibration dump* is the
thing that can go stale — see §2.6.)

---

## 2. What to build

### 2.1 Fix the pellet draw order — the core's correctness

`pellet_offsets` (and the Monte-Carlo core's `draw_pellet`) must draw:

* the **spread** radius and angle (`r1`, `a1`) — **once**, for pellet 0 — and
  **again for every pellet** only when the shotgun-pattern convar is on;
* the **inaccuracy** radius and angle (`r2`, `a2`) — **per pellet**.

**The rule must be a parameter, not a constant**: whether the spread is
redrawn per pellet depends on the convar, which the payload may or may not be
able to read. The `SpreadKernel` (the MC core's type) is the natural place for
the flag.

**Requirements:**

* the single-pellet case must be **bit-identical** to today's — that is the
  regression that matters for the rifles and the R8, and it is what the
  existing `aim-spread` test pins;
* the multi-pellet case must match the engine: **verify it against the
  engine's rule**, not against the repo's current code;
* **both** the payload's `pellet_offsets` and the MC core's draw must change
  together — they are deliberately two independent assemblies, and the
  cross-check between them is what keeps them honest. **The test that compares
  them must be updated to the new rule** and must still be a real comparison.

### 2.2 The attack type

Read `m_weaponMode` (`weapon + 0x1A00`) — **it is already in the SDK**. It
becomes part of the weapon profile (or of the `SpreadKernel`), because the
core's `spread_kernel` must know it.

**The ambiguity must be handled, not hidden.** The RE found **four**
`CSBaseGunFire` call sites and only one of them passes `m_weaponMode`; the
others pass `ammo_in_clip & 1`, a literal `0`, or a shot-struct field whose
owner is unknown. **Decide and state**: does the payload read the field, and
what does it do when the field is stale or the path is one of the other three?

**Where the payload cannot know the engine's attack type, the conservative
choice is to treat the weapon as NOT in secondary mode** — the revolver's
`1 − r²` is then not applied, and the cone is the standard one. **Say that is
what you do.**

### 2.3 The revolver's rule

`1 − r²` requires **the weapon id** (or the VData's `m_bIsRevolver` at
`+0x71E`) **and `attackType == 1`**. The designer-name rule is **not**
equivalent and must be **replaced**, not supplemented.

**The `m_bIsRevolver` boolean is the cleaner discriminator** (it is a VData
field, so it is a plain read) — but the engine compares **ids**. **Say which
you use and why**, and make the deagle and the R8 distinguishable (the R8 is
the revolver; the deagle is **not** — the engine's rule is `attackType == 1`
and the deagle is a primary-mode weapon).

### 2.4 The negev's rule

`r → 1 − r^(2^k)` with **`k = 3 − recoilIndex`**, where `recoilIndex` is
`m_flRecoilIndex` (`weapon + 0x1A28`) — the per-shot counter.

**Requirements:**

* the transform applies to **both** the spread radius and the inaccuracy
  radius (the engine has an identical copy of the branch for each);
* it applies to the **first three shots of a burst** (recoil index 0, 1, 2) and
  **not** after;
* the payload must **read the recoil index** — it is in the SDK. **When the
  payload does not know it** (the weapon is not in the snapshot, the counter is
  unreadable), say what it does: the conservative choice is **no transform**.

### 2.5 The shotgun's pattern table — only if it can be reproduced

The region-2 values are derived from the VData with a
**`CUniformRandomStreamImpl`** seeded with `spread_seed`. The payload's
`UniformRandomStream` is the **Source `CUniformRandomStream`** (Park–Miller +
Bays–Durham). **Are they the same generator?**

→ **Establish that first, and do not assume it.** If the engine's
`CUniformRandomStreamImpl` is **not** the repo's `UniformRandomStream`, the
pattern values **cannot be reproduced** and the shotgun's pattern table must
stay **unmodelled** — in which case the MC core must keep **refusing** the
shotgun, and the task must say so.

**If** the generators are the same (or the pattern is reproducible another
way), implement region 2: `angle = U(0, 2π)`, `radius = U(k/N, (k+1)/N)` with
`N = num_bullets` and `k = min(counter, N−1)`, indexed by
`recoilIndex * numBullets + pelletIndex`, with the `< 0x40` bound and the
fallback to the random cone — and the **convar gate**
(`weapon_accuracy_shotgun_spread_patterns`, **off by default**).

**Either outcome is a good outcome**: "reproducible, here it is" and "not
reproducible, here is the proof, and the refusal stays" are both deliverable.
**A guess is not.**

### 2.6 The docs must be corrected

`docs/re/aim.md` — **rewrite** (it is not append-only; the existing sections
are *wrong*):

* the **addresses** (the table in §1.6);
* a new section **"The attack type"** — `m_weaponMode` at `+0x1A00`, the enum,
  the four call sites, and the ambiguity;
* **"The spread cone"** — `r1, a1` drawn **once per shot**, `r2, a2` **per
  pellet**;
* a new section **"The pattern table"** — the global, the block layout, the
  three regions, the two builders, the VData inputs, the index formulas, the
  convar gate;
* the **negev** bullet — `dist` is `m_flRecoilIndex`, `k = 3 − recoilIndex`,
  per-shot;
* the **three convar identities** — `weapon_debug_max_inaccuracy`,
  `weapon_debug_inaccuracy_only_up`,
  `weapon_accuracy_shotgun_spread_patterns`;
* the **live build's** sha256 (`5a46a09b…`) and the note that the RVAs still
  hold.

**Also**: note that the **game auto-updated** and the module on disk is no
longer `659940c1`. The payload's **schema offsets** are unaffected (they come
from the schema, not from a signature), but the **calibration dump** — the
*dumper's signatures* — can be stale. **Check whether the dump is still
current** and say so in the report; if it is stale, that is a finding for the
operator (the offsets the features resolve would be the old build's).

---

## 3. What is out of scope

* The **occlusion / world-geometry** defect (T39 is its instrument).
* The **fire path** (T37).
* The **control channel's settings** (T40).
* The **live path's wiring** of any of this — the spread rules land in the
  core and the profile; making a new objective reachable is not this task.
* The **recoil/crosshair pattern** (regions 0 and 1) — the bullet path does
  not use it.
* **Calling the engine's `weapon_calculate_spread` / `FX_FireBullets`.** The
  payload re-implements the sampler; this task keeps that shape and makes it
  correct.
* Any **new dumper signature** — the RE says none is needed. If you find
  yourself wanting one, **stop and report it**.

---

## 4. File ownership

You own:

* `payload/aim/include/cs2/aim/spread.hpp` and `payload/aim/src/spread.cpp` —
  the draw order, the pattern rules
* `payload/aim/include/cs2/aim/montecarlo.hpp` and
  `payload/aim/src/montecarlo.cpp` — the core's draw, **only** to keep it in
  step with `spread.cpp` (§2.1's two independent assemblies)
* `payload/aim/include/cs2/aim/field.hpp` and `payload/aim/src/reader.cpp` —
  the weapon profile's new fields (`weapon_mode`, the recoil index, the
  revolver flag), **only** as far as §2.2–§2.4 need them
* `payload/aim/tests/test_aim_spread.cpp` and
  `payload/aim/tests/test_aim_montecarlo.cpp` — the tests
* `payload/control/include/cs2/control/rage_objective.hpp` — the revolver's
  rule, **only** as §2.3 requires
* `docs/re/aim.md` — the corrections in §2.6
* `docs/re/README.md` — **append only**: the index entry for the new sections

You do **not** own `payload/control/src/service.cpp` (the live path's wiring),
`payload/aim/src/field.cpp` (the objective solver), `payload/features/**`,
`overlay/**`, `wire::Settings`, the dumper, or `docs/re/bullet-trace.md`. **If
you need a file you do not own, stop and report it** instead of editing it.

---

## 5. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++: errors as values,
  **no exceptions**, trailing return types on declarations **and**
  definitions, fully qualified `std::`, no new global mutable state,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`,
  `static_cast`/`reinterpret_cast` only, 2-space indent, 100 columns, K&R
  braces.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no namespace-scope `std::vector`, **keep scratch containers
  local**, no `thread_local` anywhere.
* The payload's RNG is the repo's `UniformRandomStream`; **do not add a second
  generator**.
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 6. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\<task>\.orchestration\env.ps1

meson compile -C C:\Users\gpmpo\cs2-build\<task>\build
meson test   -C C:\Users\gpmpo\cs2-build\<task>\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-payload"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-overlay"
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\<task>\build-injector"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

(The build directories live **outside** the worktree; do not run `meson setup`
inside it.)

**The live check is the operator's** — the spread rules can only be confirmed
against the live engine by **firing the weapon** and observing where the
bullets land. Say plainly that you could not do that, and leave the exact
steps for the operator (the scenario harness has the pieces: a `bot-place`d
target, a `weapon_accuracy_nospread` toggle, and the hit log).

---

## 7. What the report must state

1. **The draw order** — what you implemented, and the proof that the
   single-pellet case is **bit-identical** to today's.
2. **The attack type** — what the payload reads, and how it handles the four
   call sites' ambiguity.
3. **The revolver** — the rule you implemented and the deagle/R8 consequence.
4. **The negev** — the rule, and what happens when the recoil index is
   unknown.
5. **The shotgun's pattern** — **reproducible or not**, with the evidence. If
   not reproducible, say so plainly and keep the refusal.
6. **The calibration dump** — whether it is still current for the live build
   (`5a46a09b…`), and what that means for the features that resolve offsets.
7. **The tests** — each one, and how it fails when the defect is reintroduced.
8. What is still open, and what you could not verify.
