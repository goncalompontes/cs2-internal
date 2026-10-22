# T32 — the animfix never captures: the codec seam is not the one that runs

> **The one-line version:** the live test showed the animfix's **anim-tick hook
> fires** (1542 client ticks) but the **graph codec never captures anything**
> (0 captures, 376 skips, all of them `no_capture`). The payload hooks
> **one** of the engine's **three mirrored** snapshot codecs and the local
> player's data evidently comes from another one. This task makes the
> diagnostic visible, finds the codec that actually runs, and hooks it.

---

## 1. The live evidence (2026-10-21, the operator's bot match)

The scenario `animfix` failed with:

```
the animfix is on but the payload never captured the server's graph inputs
(captures +0, applies +0):
{ 'screen': 'buy-time', 'animfix': True, 'animfix_ready': True, 'animfix_status': 'ok',
  'animfix_acted': False, 'captures_during_window': 0, 'applies_during_window': 0,
  'skips_during_window': 376, 'skips_no_capture': 376, 'skips_stale': 0,
  'skips_controller': 0, 'skips_write_failed': 0,
  'anim_ticks': 1542, 'client_ticks': 1542, 'corrected_ticks': 0,
  'snapshot_interval_ms': 0, 'rejected': 0, 'server_aim_yaw': None, ... }
```

Read it carefully — it is narrow:

* `animfix_ready: True` and `animfix_status: "ok"` — **both hooks installed**;
  the new dumper signature resolved (`animfix_graph_codec` → `0x8d9740`,
  `animfix_anim_tick` → `0x35aeb0`) and the offsets are in the dump ✓;
* `anim_ticks: 1542`, `client_ticks: 1542` — the **anim-tick hook fires** ✓
  (the payload is live and the anim tick is where the review said it was);
* `captures: 0` — the **codec never captured**;
* `skips_no_capture: 376` — every anim tick found no capture ✓ (consistent);
* `snapshot_interval_ms: 0` — no decode was ever observed ✓ (consistent).

So the capture path never runs. Two possibilities, and **the payload cannot
currently tell them apart**:

| | hypothesis | what it means |
|---|---|---|
| **H1** | the hooked codec **never fires** | the local player's snapshot is decoded by a **different** codec |
| **H2** | the codec fires, but the capture **rejects** its argument | the codec's `dst` is not the pointer the payload compares against |

The capture (`payload/control/src/input.cpp:1573-1577`) is:

```cpp
const std::uintptr_t local = animfix_controller();       // *(pawn + 0x34f8)
if (local == 0 || controller != reinterpret_cast<void*>(local)) {
  return;  // another entity's controller
}
```

`controller` is the codec's **first argument** (`dst`).

## 2. What the RE says about the codecs

`docs/re/animfix.md` §3 (the pipeline table) row 7 and §"the table":

> | 7 | Snapshot codec of the graph inputs | the controller's netvar codec
> (**two mirrored functions**) | `0x8d9740` / `0x8daab0` |
> `void Codec(CCS2PawnGraphController* dst, CClass* src, CSchemaTable*)` | …

and the hookability table:

> | the graph-input codec | **`0x8d9740`** | YES (14 B) | `48 89 5C 24 08 / 55 56 57 / 41 54 41 55 41 56` |
> | the mirrored codec | **`0x8daab0`** | YES (14 B) | `48 89 5C 24 18 / 48 89 7C 24 20 / 55 / 48 8B EC` |

**The payload hooks only `0x8d9740`.** The RE never says which of the two
carries the **local player's** data — and the live test says the one it hooks
does not.

### 2.1 The mirrored codec has three byte-identical siblings

Measured on the live `client.dll` (sha256 `659940c1…`, the build the RE used)
with the dumper's own scanner:

| pattern | matches |
|---|---|
| `48 89 5C 24 18 48 89 7C 24 20 55 48 8B EC` (the mirror's 14 bytes) | **10** |
| … + `48 83 EC 30 48 89 74 24 40` (18/20 bytes) | 6 / 4 |
| … + `E8 ?? ?? ?? ?? 48 83 BB 40 01 00 00 00 48 8D BB 48 01 00 00` (44 bytes) | **3** — `0x8daab0`, `0x90eb20`, `0xd29b30` |
| the same with the call's exact displacement (31 bytes) | 1 — but the displacement is **build-specific** and must not be in a signature |

So the three are the **same codec shape for three different classes** — the
same family, different entities. **A byte pattern alone cannot say which one
is the local player's**; the discriminators are the class's own constants
deeper in the body.

(The hooked codec `0x8d9740` is different: its extended pattern — with the
`lea rbp,[rsp-0x610]` / `sub rsp,0x710` — **is** unique ✓.)

## 3. What to build

### 3.1 The diagnostic first

**Do not guess.** The payload must be able to say, in one live run, which
hypothesis holds. Add to the animfix's status:

| counter | what it counts |
|---|---|
| `animfix_codec_calls` | every time the **hooked codec's detour** runs (whatever the capture then does) |
| `animfix_codec_other_controller` | the calls where the codec's `dst` did **not** match the local controller (H2) |
| `animfix_codec_not_local` | the calls where the codec ran for **another** entity's controller (H2, the other direction) |

`animfix_codec_calls == 0` ⇒ **H1** (the hooked codec never runs for us).
`animfix_codec_calls > 0` and `animfix_codec_other_controller == animfix_codec_calls`
⇒ **H2** (the codec runs, but not for the local player — or the comparison is
wrong).

The counters must be **atomic**, cheap (a fetch_add), and reported in the
animfix's status block **and** in the scenario's result, so the next live run
answers the question in one line. The scenario must **not** raise a bare
"never captured" error any more: it must say **which** case it was (the
counters) — that is the difference between a diagnosis and a symptom.

### 3.2 Then the fix

Depending on what the counters say:

* **H1 (the codec never runs):** hook the codec that **does** carry the local
  player's data. That means identifying it among the three siblings of
  `0x8daab0` — the discriminators are the class's own constants deeper in the
  body (the field offsets the codec writes). The dumper's scanner is the tool:
  extend the pattern with the bytes that differ between the three and check
  that the pattern is **unique** in the module. **A signature that matches
  more than one place is not a signature** — the dumper's `find_first` would
  take the wrong one, exactly like the animfix's first signature had to be
  extended.
  The new signature goes into `known_signatures()` (ADR-0012: no hard-coded
  RVA) and the payload hooks it.
  **Hooking more than one codec is acceptable** if that is what the evidence
  demands — the capture is filtered by the controller anyway.
* **H2 (the codec runs, the comparison fails):** fix the comparison — the
  local controller's address or the codec's argument. The RE's own evidence
  for `pawn + 0x34f8` is `FUN_180c905e0`:
  `FUN_1808e3540(*(longlong *)(param_1 + 0x34f8), param_1)` — the graph
  controller is at `pawn + 0x34f8` ✓ (on this build). If the codec's `dst` is
  a *different* object (the RE's `dst` is a `CCS2PawnGraphController*` and the
  payload compares it against the pointer at `pawn + 0x34f8` ✓ — those should
  be the same object; **prove it** with the counters, and if they are not,
  say what `dst` actually is).

Either way, the fix must be **verified by the live run**: `captures > 0` and
`applies > 0` with a sane `server_aim_yaw` and a `predicted_aim_yaw` that
differs from it.

### 3.3 The capture must also be honest about what it stored

When the capture does run, the status must show the **server's** aim yaw and
the **client's predicted** one (the spec's promise) — that is the operator's
evidence that the fix does anything. If the two are equal, say so in the
report.

## 4. The rules

- The payload is **manually mapped**: no `thread_local` in a detour, no CRT
  static init, no exceptions.
- **ADR-0012**: every address goes through a dumper signature; a signature
  must be **unique in the module** — verify it with the dumper's own scanner
  and report the match count.
- The counters are **atomic** and cheap; the anim tick runs 60+/s.
- `docs/re/animfix.md` is **append only**.
- The scenario must stay **observation-only** apart from switching the feature
  on and off.
- The house style (`docs/style-guide.md`, `AGENTS.md`): C++23, trailing
  return types, fully qualified `std::`, errors as values, no exceptions.

## 5. File ownership

You own `payload/control/src/input.cpp`, `payload/control/include/cs2/control/input.hpp`,
`payload/control/include/cs2/control/animfix.hpp` (the pure part), the
animfix's host test, `payload/control/src/service.cpp` (the status block),
`external/dumper/src/analysis.rs` (the signature, if you add one),
`scripts/scenario/scenarios/animfix.py` and its test, and `docs/re/animfix.md`
(append only).

You do **not** own the chams, the third-person camera, the prediction's copy,
the other scenarios, or `wire::Settings`.

## 6. The verify loop

```powershell
. C:\Users\gpmpo\cs2-wt\t32-animfix-codec\.orchestration\env.ps1
meson compile -C C:\Users\gpmpo\cs2-build\t32-animfix-codec\build
meson test   -C C:\Users\gpmpo\cs2-build\t32-animfix-codec\build
$vc='C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat'
cmd /c "call `"$vc`" >nul 2>&1 && set CC=clang-cl && set CXX=clang-cl && meson compile -C C:\Users\gpmpo\cs2-build\t32-animfix-codec\build-payload"
python -m unittest discover -s scripts\scenario\tests -t scripts
python scripts\typecheck.py
```

**Known harness artefact:** the SDK generation looks for the dumper at
`external/dumper/target/release/cs2-dumper.exe`, but the environment sets
`CARGO_TARGET_DIR`. Copy the built dumper exe into
`external/dumper/target/release/` of your worktree (gitignored) if the build
needs it.

## 7. The live run — the acceptance criterion

**The game is up and injected on this machine right now** (the operator is in
a 60-minute bot match on `de_dust2` with `bot_stop 1`). You can:

* build the payload and **ask the orchestrator to re-inject** (a restart is
  needed: **never inject twice into the same game**);
* run the scenario yourself: `python scripts\run-scenario.py animfix`.

The acceptance criterion is the live run: **`captures > 0` and `applies > 0`**,
with the server's and the client's aim yaw both reported and (ideally)
different. If the counters say the codec still does not fire for the local
player, **report that with the counters** — a precise negative result is a
result.

## 8. What the report must state

1. **The diagnosis** — which hypothesis the counters settled, and the numbers.
2. **The fix** — what was changed, and the signature (with its **match count**
   in the module) if you added one.
3. **The live result** — the scenario's output after the fix.
4. **The tests** — and the proof that each can fail.
5. **What is still open** — the anim-state divergence, the frame ordering, and
   anything the live run still does not settle.
6. The file list and the commit hashes.
