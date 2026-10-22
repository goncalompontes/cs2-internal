# T39 — the trace probe: the engine's own answer for the same ray

> **The one-line version:** the payload's occluder set and the engine's bullet
> trace are two different answers to "what is between the eye and the target",
> and today there is **no way to compare them** — the payload can only report
> its own set. This task adds a **probe** that asks the engine its own question
> (`TraceLineIntervals`, the call the real bullet makes) for a given ray and
> publishes **both** answers side by side, plus the **difference**. It is the
> instrument the occlusion investigation needs, and the first slice of the
> Tier-1 replacement the repo already planned.

---

## 1. Why this task exists

### 1.1 The symptom, reproduced live (2026-10-22)

On the merged tree, with the payload injected into a live bot match on
`de_dust2`, the heatmap renders **the target's silhouette and the crosshair and
nothing else** — the world's walls are not drawn, and the heatmap's own stat
says `occluders: 0`.

The repo recorded the same symptom on 2026-10-18 (`docs/re/bullet-trace.md`,
"What this means for the occlusion enumeration (the fix)"):

> *"**Live finding (2026-10-18).** Standing in de_dust2 with a wall between the
> eye and three enemies, the payload reported **no** occluder on the eye→head
> rays, yet the wall is clearly visible. Ray-testing the world body's six mesh
> shapes directly (Möller–Trumbore over their `+0x50` triangle indices) hits
> them at 94.8 / 323.2 / 267.6 units — i.e. the wall is a **mesh**, and the
> payload **skipped every mesh**."*

The mesh reader was implemented as the fix, and the fix was **never
live-validated** — `docs/ragebot-review.md` §5's next action 2 is literally
*"**Live re-validate** the floor: rerun the heatmap with a wall/floor between
eye and target and confirm `mesh_clusters > 0` and attenuation"*, and it was
never done.

### 1.2 What the repo already knows (do not re-derive it)

**The world body is NOT missing from the enumeration.** The user's working
hypothesis was that `EntitiesInBox` returns entities and the map's world
geometry is not one of them. The repo's live RE says the opposite, in three
places:

- `docs/re/occlusion.md:182-186` — *"the overlap record for the worldspawn
  handle `0x8000` carries the **world body**, whose shape count is **10149**
  (one `IPhysicsShape` per map brush) — the map-brush source."*
- `docs/re/bullet-trace.md:194-196` — the shape histogram is
  **`{type 2 (hull): 10143, type 3 (mesh): 6}`**, and *"the map's large/merged
  wall surfaces are the mesh shapes."*
- `docs/re/ragebot-review.md:222-227` — the live payload JSON:
  `mesh_clusters = 83` and `world_cache_hulls = 10226` (10143 hulls + 83 mesh
  clusters).

The world body reaches the payload **only** through the `EntitiesInBox` records
(`payload/control/src/occlusion_query.cpp:1242-1246` — the worldspawn is
explicitly never excluded), and the payload reads its type-3 meshes as a
triangle soup (`parse_mesh`, `emit_mesh_clusters`).

**So the question is not "is the world there" but "what does the engine's trace
hit that our set does not?"** — and that question is **not answerable with the
current tools**, because the payload cannot ask the engine.

### 1.3 The engine's own call — already reversed

`docs/re/bullet-trace.md` has it, and it is the call the real bullet makes:

> *"The trace itself is **`TraceLineIntervals`** = `FUN_180a19e90` (not
> `TraceShape`), called as
> `TraceLineIntervals(*(client+0x2227058), &results, start, end, filter+8, -1.0f)`;
> its results are `0x38`-byte interval records in a `CUtlVector`."*

| what | where |
|---|---|
| the query interface singleton | `*(client.dll + 0x2227058)` — the payload already reads it (`occlusion_query.cpp:1362-1363`, `:1388`, `:1394`) |
| the trace function | `FUN_180a19e90`, RVA **`0xa19e90`** |
| the call form | `TraceLineIntervals(iface, &results, start, end, filter + 8, -1.0f)` |
| the filter | the engine's static default at `client + 0x221f070`; the payload **already builds** the engine's own bullet filter — mask **`0x1c300b`**, group **3** (`build_bullet_filter`, `occlusion_query.cpp:892-905`) |
| the interval record | stride **`0x38`**; **pinned**: the distance at `+0x0C`, the enter/exit byte at `+0x32`. The rest is open (`bullet-trace.md` "Still open" 2) |
| the results container | a `CUtlVector` of interval records; the engine's own call sizes it for 64 entries |

**The payload's existing `EntitiesInBox` call already solves the two hard parts
of calling into the engine** — and its pattern is the template you must follow:

1. **the results container must not make the engine allocate.** The payload's
   `EntitiesInBox` call passes a `CUtlVector` with the **external-buffer flag
   `0x80000000`** and a pre-allocated buffer (`occlusion_query.cpp:1426-1429`).
   The engine's allocator is tier0's, and the payload's thread has no tier0 TLS
   — *"Engine calls that allocate must run on a game thread"*
   (`docs/handoff.md:552-558`). **Do the same for the trace's results vector.**
2. **the call must be fault-guarded.** The `EntitiesInBox` call is wrapped in
   `diag::call_guarded` (`occlusion_query.cpp:917`) — *"a wrong ABI faults the
   game (the payload has no registered exception tables), so this is not
   guessable"*. The trace call is **more** likely to be wrong than the overlap
   call (its results container and argument order are less pinned). **It must be
   fault-guarded too**, and the probe must report a fault as a **probe failure**,
   never as a crash.

---

## 2. What to build

### 2.1 The probe command

A new **control command** (the payload's command surface is
`payload/control/src/service.cpp`'s `do_*` family, dispatched from `do_batch` /
the command switch). Name it **`trace_probe`**.

It answers, for a given set of rays: **what does the engine's bullet trace
hit, and what does our occluder set say about the same rays?**

**Arguments** (all optional, with documented defaults):

| argument | meaning | default |
|---|---|---|
| `rays` | the rays to probe | the **current view ray** |
| `eye` | the ray origin | the local player's eye |
| `yaw` / `pitch` | the view direction, in **degrees** | the local player's eye angles |
| `spread` | an extra angular fan around the view ray, in degrees | `0` (a single ray) |
| `count` | how many rays in the fan | `1` |
| `depth` | the ray's length, in world units | the engine's own trace length |

**The ray specification.** A ray is `origin` + `direction`. The direction comes
from the yaw/pitch **in the repo's own chart convention** — use the chart's
`Basis` and its `unproject`, so the probe's ray is the *same* ray the occluder
projection uses. **Getting this wrong would make the whole comparison
meaningless**: the probe's ray and our ray must be the same 3-D segment. The
probe must therefore **publish the ray's endpoints** (`start`, `end`, and the
direction vector) so the operator and the reviewer can check that the two agree.

**A ray fan** (for `spread`/`count`) is how the operator finds a wall that our
logic misses: a small fan around the view ray shows which directions the engine
sees a solid in and which directions our set is empty. **Sample the fan
deterministically** (a documented order, not random) so two runs compare.

### 2.2 What the probe publishes

For **each** ray, in one answer:

**A. The engine's answer.**
* the number of intervals the engine returned;
* each interval: its **distance** (`+0x0C`) and its **enter/exit** byte
  (`+0x32`);
* the **surface/entity provenance** — **as far as it is pinned**. The interval
  record's full `0x38` layout is **open** (`bullet-trace.md` "Still open" 2), so
  publish the bytes you *can* read and be explicit about what is not mapped.
  **Do not invent a field's meaning.** If the probe cannot name the surface,
  say `"surface": "unmapped"` — that is a finding, not a failure;
* the **raw bytes** of each interval record, hex, so a later RE pass can map
  the rest **from the probe's own output** without re-running the game. **This
  is the point of the probe** — publish the evidence, not just a summary.

**B. Our answer.** For the same ray: which of **our** projected occluders it
hits, at what distance, and that occluder's **kind** (hull / mesh / sphere /
capsule / polytope) and, for a mesh, **which cluster**. Use the same
`OccluderCell::ray_span` the solver uses, so this is the *actual* answer the
solver gets, not a re-implementation. If our set says **nothing**, say so
plainly.

**C. The difference.** The point of the whole task:
* the intervals the engine found that **our** set has **nothing** for — the
  distance and the raw record;
* the occluders **our** set has that the engine's trace did **not** report —
  the over-inclusion (the repo's own note: the enumeration is a *superset*
  around the target);
* a one-line verdict: `"agrees"`, `"engine_only"`, `"ours_only"`, or
  `"both"`.

**D. The probe's own health.** Whether the engine call **succeeded**; if it
faulted (the guard caught it), say so **loudly** and report the fault as the
answer — a probe that faults is a *result*, not a crash.

### 2.3 The probe must be **safe**

* **Fault-guard the engine call** (`diag::call_guarded`), exactly as the
  `EntitiesInBox` call is guarded. A wrong ABI must not kill the game.
* **Never allocate on the render thread**, and never make the **engine**
  allocate (the external-buffer results vector, §1.3).
* **Bound the work**: a fixed maximum number of rays, a fixed maximum number of
  intervals, and a fixed results buffer. The probe is a diagnostic; it must not
  be able to stall the game.
* The probe must **not** change the ragebot's behaviour. It is a separate
  command; the solver's path is untouched.

### 2.4 The probe is a **measurement**, and the measurement is the deliverable

The task's deliverable is **not** a fix. It is the instrument, plus **one
recorded measurement** that answers the question the operator asked:

> *"run trace bullet with my view angles to check what the trace bullet is
> hitting that we aren't with our logic"*

**So the task must include the measurement**, run against the live game, and
the answer written down. The operator's part is small and they have already
offered it: stop the bots, point at a wall that is 100 % solid, and let the
agent run the probe with those view angles.

**The report must state, for the probe's ray:**
* what the **engine's trace** hit (the intervals, their distances, and the raw
  records);
* what **our** occluder set said about the same ray;
* the **difference**;
* and therefore **where the world geometry is lost** — which of these it is:
  1. the world body is not in the `EntitiesInBox` results (the user's
     hypothesis — the repo's RE says it is, so this should be *disproved* by
     the measurement);
  2. the world body's shapes are not parsed (the mesh reader fails or is
     skipped);
  3. the shapes are parsed but **dropped** — the cluster cap
     (`k_max_mesh_clusters = 128`), the compound skip, the BVH's broad phase,
     or the per-query selection;
  4. they are selected but **project to nothing** in the chart;
  5. they are projected but the **ray does not hit them** — the silhouette's
     convex hull, or `ray_span`'s "two nearest triangle crossings" rule;
  6. the engine's trace sees something we have **no shape for at all**.

**That is the deliverable: the numbered answer.** The fix is a **later** task,
and it must be driven by this answer — not guessed at.

---

## 3. The tests

### 3.1 Host tests (the parts that can be tested without the game)

* The **ray construction**: the probe's ray and the solver's ray must be the
  same segment for the same yaw/pitch — a test that builds both and compares
  the endpoints. **This is the test that keeps the comparison honest.**
* The **difference logic**: given a synthetic set of engine intervals and a
  synthetic set of our occluders, the verdict (`agrees` / `engine_only` /
  `ours_only` / `both`) must be right in every combination — including the
  empty cases, and a case where an engine interval is at a distance where we
  have an occluder but at a different depth.
* The **interval record reader**: the distance at `+0x0C` and the enter/exit
  byte at `+0x32` must be read from the right offsets — a test with a
  hand-built record.
* The **bounds**: the probe must refuse more rays than its maximum, and more
  intervals than its buffer.

### 3.2 The live measurement (the point of the task)

* The probe must be run against the **live game**, with the operator's view
  angles on a wall that is 100 % solid, and the numbers recorded in the report
  and in `docs/re/occlusion.md`.
* **The live measurement is run by the ORCHESTRATOR, with the operator** — the
  operator points at the wall and tells the orchestrator, and the orchestrator
  runs the probe. **Your deliverable is the instrument**: the command, the host
  tests, and a **documented, copy-pasteable invocation** (the exact control
  command with its arguments) that the orchestrator can run immediately.
* You **may** run the live measurement yourself if the tools are available to
  you — but **do not** inject into a game that already has the payload, and
  **do not** try to talk to the operator. If you cannot complete the live part,
  say so plainly and hand the invocation over; the orchestrator will run it.
* **Prove the probe can fail**: a deliberately wrong argument order (or a wrong
  filter) must be shown to produce a visibly wrong answer or a caught fault —
  **not** a crash. The payload has no exception tables; the guard is the only
  thing between a wrong ABI and a dead game, and the reviewer must see it work.

---

## 4. What is out of scope

* **Fixing** the occlusion. This task **measures** it. The fix is a separate
  task, driven by the answer this one produces.
* The **Tier-1 replacement** of the enumeration by the trace. This task calls
  the trace **for a probe**; it does not replace the solver's occluder set.
* **Mapping the interval record's full layout.** Publish the raw bytes; a
  later RE pass maps the rest. Do not guess a field's meaning.
* The **spread patterns**, the **semi-auto fire** and the **"can we shoot"**
  gate — they are separate tasks (they are in the same review cycle as this
  one, but they are not this task).
* The engine's **penetration** loop (`FUN_18089ca80`) — the probe reports the
  intervals; it does not model the wall-bang.
* Any change to the **solver's** behaviour.

---

## 5. File ownership

You own:

* `payload/control/src/occlusion_query.cpp` and
  `payload/control/include/cs2/control/occlusion_query.hpp` — the probe's engine
  call, the results vector, the interval reader, and the difference logic
* `payload/control/src/service.cpp` and
  `payload/control/include/cs2/control/service.hpp` — the new `trace_probe`
  command and its publication
* `payload/control/tests/**` (the new host tests)
* `scripts/scenario/protocol.py` and `scripts/scenario/tests/**` — the
  command's typed answer and its test, **if** the command is exposed to the
  harness (it must be, or the operator cannot run it)
* `docs/re/occlusion.md` — **append only**: the measurement, the raw records,
  and the numbered answer
* `docs/re/bullet-trace.md` — **append only**: the interval record's bytes as
  observed, and the probe's call form as verified

You do **not** own the aim module (`payload/aim/**` — the solver's behaviour is
untouched), `payload/features/**`, `overlay/**`, `wire::Settings`, the dumper or
the game. If you need a file you do not own, **stop and report it** instead of
editing it.

---

## 6. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++23, 2-space indent,
  100 columns, K&R braces, trailing return types on declarations **and**
  definitions, fully qualified `std::`, errors as values, **no exceptions**,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`, `static_cast` /
  `reinterpret_cast` only, no new global mutable state (ADR-0010).
* The payload is **manually mapped**: no CRT static-init, no `<print>`, no
  exceptions, no namespace-scope `std::vector`, **keep scratch containers
  local**, no `thread_local` anywhere.
* **No allocation on the render thread**, and **never make the engine
  allocate** (§2.3).
* **Every engine call is fault-guarded.**
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 7. The verify loop

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

**The live measurement** needs the game: the payload must be built, the game
launched (`schtasks /run /tn CS2Launch`) and the payload injected **once**
(`build-injector\injector.exe cs2.exe build-payload\payload.dll --load-missing`).
The agent can do all of that, but it **cannot** see the screen — the operator
points at the wall and tells the agent. Coordinate with the operator; do not
inject twice into the same game.

---

## 8. What the report must state

1. **The probe's call** — the exact engine call, the argument order, the
   filter, the results container, and the memory orders — and which of those
   were **verified** against the RE and which were **inferred**.
2. **The fault-guard** — the evidence that a wrong ABI is caught and reported
   rather than killing the game.
3. **The measurement** — for the operator's ray: the engine's intervals (with
   their raw records), our set's answer, the difference, and the verdict.
4. **The numbered answer** — which of §2.4's six places the world geometry is
   lost.
5. **The interval record's bytes**, so the next RE pass has the evidence.
6. What is still open, and what you could not verify.
