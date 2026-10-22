# T33 — the chams' desc cache: safe for two writers

> **The one-line version:** the chams' **per-owner guard** (T26/T27) deliberately
> lets **two** render jobs into the draw path at the same time — but the
> **desc cache** is published with **three separate atomics**, which is a
> protocol that is safe for **one** writer and not for two. Two descs that hash
> to the same slot can therefore leave a reader seeing **desc A with entity B**,
> so the chams are decided on the **wrong player**. This task makes the cache
> safe for concurrent writers, closes the test gaps the guard exposed, fixes the
> shared hook's publication ordering, and turns the draw path's guard
> **fail-closed** so a refused draw is visible instead of silent.

---

## 1. What is wrong today

The finding is **H2a** in [`docs/known-issues.md`](../../known-issues.md) §5, and
§11.1 is the orchestrator's recipe for this task. Read both before you write
code. This spec is the **contract**; the ledger is the **history**.

### 1.1 The race, concretely

The scene system renders the draw list on the **tier0 thread pool**
(`g_pThreadPool`, `Thread_RenderSceneDrawList`, `"rendering in %i jobs"`,
`sc_disableThreading`), so **two jobs can be inside the detour at once**.

`payload/features/include/cs2/features/chams.hpp:257-261`:

```cpp
struct DescEntry {
  std::atomic<std::uint64_t> desc{0};
  std::atomic<std::uint64_t> entity{0};
  std::atomic<std::uint8_t> view_model{0};
};
```

The **publish** path — `payload/features/src/chams.cpp:461-463`:

```cpp
entry.entity.store(entity, std::memory_order_relaxed);
entry.view_model.store(view_model ? 1 : 0, std::memory_order_relaxed);
entry.desc.store(desc_address, std::memory_order_release);
```

The **read** path — `payload/features/src/chams.cpp:453-455`:

```cpp
if (entry.desc.load(std::memory_order_acquire) == desc_address) {
  entity = static_cast<std::uintptr_t>(entry.entity.load(std::memory_order_relaxed));
  view_model = entry.view_model.load(std::memory_order_relaxed) != 0;
}
```

**Why it breaks.** All three fields are *distinct* atomics, so each has its own
modification order. Writer A stores `entity=A`; writer B stores `entity=B`;
writer A stores `desc=A` (release). A reader loads `desc` (acquire) → sees `A`,
and the acquire synchronises-with A's release → A's `entity` store is visible.
**But** the reader's `entity` load is `relaxed`, and `entity`'s modification order
now has `B` after `A` — the reader may legally observe `entity=B`.

**Result: desc A with entity B.** The chams are decided on the **wrong
player** (the CT's model chamsed, the T not). The reviewer reproduced the
protocol verbatim (four threads, two descs colliding in one slot) and measured
**965–3048 mismatches per run** (`known-issues.md:87`).

The slot index is `(desc_address >> 4) & 63` — `chams.cpp:447-449`,
`k_desc_cache_size = 64` (`chams.hpp:262`). Direct-mapped, 16-byte
granularity, no probe and no tag.

### 1.2 The invalidation path — a second race

`payload/features/src/chams.cpp:440-446`:

```cpp
const std::uint64_t stamp = frame_stamp_.load(std::memory_order_relaxed);
if (stamp != desc_cache_stamp_) {
  desc_cache_stamp_ = stamp;
  for (DescEntry& entry : desc_cache_) {
    entry.desc.store(0, std::memory_order_relaxed);
  }
}
```

* `desc_cache_stamp_` (`chams.hpp:265`) is a **plain `std::uint64_t`**, not an
  atomic. Two scene render threads writing it concurrently is a data race.
* The invalidation stores `desc = 0` in all 64 slots with **relaxed** stores,
  racing a concurrent publisher's **release** store — the invalidation can be
  **silently undone** by a writer that was mid-publish.
* It invalidates the **key only**; `entity` and `view_model` keep their old
  values, relying on `desc == 0` never matching a real desc.

### 1.3 The other shared state

| item | declared | atomic? | written by | read by |
|---|---|---|---|---|
| `g_active` | `chams.cpp:33` (`Chams*`) | **no** | payload thread: `628`, `632`, `239`, `287` | render thread: `186` |
| `InlineHook::trampoline_` | `payload/hooks/include/cs2/hooks/inline.hpp:43` (`void*`) | **no** | payload thread: `inline.cpp:418`, `432` | render thread: `chams.cpp:187`, `404` |
| `InlineHook::installed_` | `inline.hpp:47` (`bool`) | **no** | `inline.cpp:422`, `433` | `chams.cpp:187`, `235`, `275`, `284`, `317`, `538` |

**The uninstall ordering is a use-after-free.** `payload/hooks/src/inline.cpp:426-434`:

```cpp
void InlineHook::uninstall() noexcept {
  if (!installed_) { return; }
  std::memcpy(target_, saved_, patched_size_);
  free_executable(trampoline_, trampoline_size_);   // <- freed here
  trampoline_ = nullptr;                            // <- nulled here
  installed_ = false;
}
```

A render thread that read `trampoline_` before the free holds a **dangling
pointer** and calls **freed executable memory**. Making `trampoline_` atomic
alone does **not** fix this: the null-store must precede the free.

The **install** side already documents the opposite discipline
(`inline.cpp:410-416`): *"The trampoline is **published before the patch** …
so a detour that runs in the window between the patch and the publication would
find `original() == nullptr` and drop the call."* The uninstall side has the
mirror defect.

### 1.4 The counters

`payload/features/include/cs2/features/chams.hpp:243-245`:

```cpp
std::atomic<std::uint64_t> seen_{0};
std::atomic<std::uint64_t> drawn_{0};
std::atomic<std::uint64_t> skipped_{0};
```

The invariant is `seen == drawn + skipped` (`chams.hpp:240-242`). It holds
**per call**, but the **reset** breaks it — `payload/features/src/chams.cpp:306-310`:

```cpp
seen_.store(0, std::memory_order_relaxed);
drawn_.store(0, std::memory_order_relaxed);
skipped_.store(0, std::memory_order_relaxed);
```

Three separate relaxed stores, on the **payload's** thread (`configure()`, when
the feature is switched **off**), while the scene render threads are
concurrently `fetch_add`ing the same counters (`chams.cpp:403`, `408`, `414`,
`423`, `466`, `492`, `502`, `515`, `521`, `534`). A `seen_` increment landing
between two of those stores leaves `seen=1, drawn=0, skipped=0` — an
**off-by-one that never heals** until the next reset.

**And the guard's refusal is not counted at all.** The refusal at
`chams.cpp:223-227` returns **before** `on_draw`, so neither `seen_` nor
`skipped_` moves. A refused draw is invisible in the status.

### 1.5 The test gaps the guard exposed

The real "chams guard" test is **`payload/features/tests/test_chams_draw_hook.cpp`**
(521 lines) — **not** `test_chams_guard.cpp` (that name is wrong in
`docs/re/chams.md:600` and `docs/re/chams.md:693`; finding **H8**).

Its harness is a global `int failures` plus an `expect(bool, const char*)`
(`test_chams_draw_hook.cpp:65-72`) — deliberately **not** `assert`, so it still
bites in a release build. **Follow that convention in any new chams test.**

| # | severity | finding | pinned today? |
|---|---|---|---|
| **H3** | minor | The latch clear's **wiring** is not pinned: deleting the clear of `install_failed_` (`chams.cpp:301-303`) leaves the suite green. | **no** — no test file mentions `install_failed_` |
| **H6** | minor | Nothing pins that the guard is held **across** the trampoline call. `test_the_chams_hold_the_shared_guard` (`test_chams_draw_hook.cpp:419-434`) only does `source.find("guard::Scope") != npos` — **presence, not ordering**. | **no** — no test greps for `original(` or `trampoline` |
| **H7** | minor | The shared guard's **fail-open** rationale is the camera's. For the chams the arithmetic is the other way round: a refusal drops **one primitive**, while failing open lets the recursion run until the stack overflows (`0xc00000fd`) and takes the renderer with it. | **no** |
| **H8** | nit | `docs/re/chams.md` names `test_chams_guard.cpp`; the file is `test_chams_draw_hook.cpp` (its Meson test is `features-chams-draw-hook`). | — |

The **desc cache is not tested at all**: no test references `DescEntry`,
`desc_cache_`, `k_desc_cache_size`, `frame_stamp_` or `desc_cache_stamp_`.

### 1.6 Why the guard's fail-open is dangerous *here* — the exact mechanism

`payload/features/include/cs2/features/recursion_guard.hpp:119-148`:

1. **Pass one** — scan every slot asking "am I already inside?"; if so →
   **refuse** (return `false`).
2. **Pass two** — claim the first free slot with
   `compare_exchange_strong(state, pack(owner,1), acq_rel, acquire)`.
3. If every one of the 9 slots (`TOTAL_SLOT_COUNT`, `recursion_guard.hpp:99`)
   is taken by **other** owners → **fail open**, return `true`.

Under fail-open the calling thread is **not recorded in any slot**. So when the
trampoline comes back on that thread, **pass one does not find it** — the
recursion is **not** broken, and it runs until the stack overflows.

That is the camera's rationale ("refusing would drop a camera update"), and it
is **wrong for the draw path**.

---

## 2. What to build

### 2.1 The desc cache must be safe for two writers

**The requirement: a reader must never observe a `desc` from one primitive
paired with an `entity` from another.**

The natural form is a **per-slot seqlock**: one sequence word per slot, **odd**
while a writer is inside the slot and **even** when the entry is stable. The
reader takes the sequence, reads the entry, re-reads the sequence, and retries
if it changed or was odd.

The requirements, whichever form you choose:

1. **A reader never pairs fields from two different writes.** This is the whole
   point. State it in a comment and cite this spec.
2. **A writer never blocks a reader.** The seqlock's writer is a *sequence
   store*, not a mutex. The scene system renders on the tier0 pool; a lock here
   would serialise the renderer. **No `std::mutex`, no `std::vector`, no heap
   allocation in the draw path.**
3. **The memory orders are correct and each one is justified** in a comment. The
   usual seqlock needs **acquire** on the reader's sequence loads and
   **release** (or acq_rel) on the writer's sequence stores.
4. **The slot index stays `(desc >> 4) & 63`** unless you can justify a change.
   A different hash changes the collision rate and therefore the exposure; the
   reviewer's probe collides two descs **in one slot** deliberately, so it must
   keep colliding after the fix.
5. **The invalidation must be part of the protocol**, not a separate store. The
   invalidation writes `desc = 0` in all 64 slots with relaxed stores; that races
   a concurrent publisher and can be silently undone. Whatever you do, a
   **stale entry must not survive an invalidation** and a **concurrent
   publisher must not resurrect one**.

**If you choose the generation + re-check form instead of a seqlock**, it must
satisfy the same five requirements, and the reader must **re-check the
generation after the fill** and discard the entry if it changed. Say in your
report why you chose it over the seqlock.

### 2.2 The other shared state must be atomic, and the free must be safe

* `desc_cache_stamp_` → atomic, with a justified memory order.
* `g_active` → atomic. It is the accepted C-ABI exception to ADR-0010
  (`chams.cpp:25-33`) — keep the exception, make the access safe.
* `InlineHook::trampoline_` and `InlineHook::installed_` → atomic.

**The uninstall ordering must change.** The requirement is:

> **No thread can ever execute freed trampoline memory.**

The current order (`inline.cpp:430-433`) frees **before** it nulls. The minimum
acceptable change is:

1. `installed_` → `false` (**release**) — stop new callers from proceeding;
2. `trampoline_` → `nullptr` (**release**) — publish the null **before** the
   free, so a detour that enters after this point finds `original() == nullptr`
   and bails;
3. only then `free_executable(...)`.

That closes the window for every detour that **enters** after the null, but a
detour that **already holds** the pointer can still call freed memory. So you
must also close that window. Two acceptable answers:

* **(a) Defer the free.** Keep the retired trampoline alive and free it when
  the `InlineHook` is destroyed (or at the next `install()`, when the hook is
  known to be idle). The cost is a bounded leak — one trampoline per
  uninstall — and you must say how it is bounded.
* **(b) A grace period.** A call counter or a reader-writer scheme so the free
  waits until no detour is in flight. More machinery; justify the cost.

**Whatever you choose, you must be able to say, in one sentence, why no thread
can execute freed trampoline memory.** And you must **prove it with a test**
(§2.4).

`InlineHook` has four users — the chams (`chams.hpp:206`), the third-person
camera (`thirdperson.hpp:169`), the present hook (`game_present_hook.hpp:41`)
and the animfix's two hooks (`input.hpp:1488-1489`). They all read through the
`original()` / `installed()` accessors, so making the **members** atomic should
not change any **call site**. If it does, you are changing behaviour you did not
intend — say so.

The animfix's comment at `payload/control/src/input.cpp:1963-1966` documents
the residual (*"the write is a plain pointer store, which is atomic on x86"*).
**Update that comment** — it will no longer be true.

### 2.3 The counters' reset

The requirement: **`seen == drawn + skipped` must hold at every observation.**

The cheapest honest answer, per the recipe in `known-issues.md` §11.1 item 3,
is to **not reset them** — the operator reads the difference between two
`[state]` lines. If you keep a reset, it must be atomic with respect to the
render thread's increments and must not break the relation for the width of
the reset window.

State which you chose and why.

### 2.4 The tests must pin what the fix is for

Three tests must be added, and **all three must be able to fail**:

1. **The concurrency probe (the acceptance test).** Take the protocol the
   reviewer copied — four threads, two descs colliding in one slot — and make
   it a **host test** that reports **zero** mismatches. It must fail against the
   **pre-fix** protocol and pass against the fixed one. **Prove it**: run the
   probe against the old protocol in a scratch copy, watch it report mismatches,
   then run it against the new one and watch it report zero. **Report both
   numbers.**
2. **The latch-clear wiring (H3).** A test that fails when the clear of
   `install_failed_` (`chams.cpp:301-303`) is deleted.
3. **The guard's scope order (H6).** A test that fails when the guard's scope is
   moved **after** the trampoline call.

Plus a fourth, if §2.2 changes the hooks module:

4. **The trampoline's lifetime.** A test that installs a real hook whose detour
   blocks on a flag, calls `uninstall()` from another thread while the detour
   is inside, then lets the detour proceed to call `original()` — and shows
   that **no freed memory is executed**. `payload/hooks/tests/test_inline.cpp`
   already installs a real hook; build on it.

The chams tests read the repo's own sources, so they need the **repo root on
`argv[1]`**. The registration passes `args : [meson.current_source_dir()]`
(`meson.build:509`) — your new test needs the same.

If the wiring/scope tests are not practical as unit tests, say so and write the
closest test you can that still **fails when the defect is reintroduced**.

### 2.5 The draw path's guard must fail **closed** (H7)

The guard's **state** is already per-instance — the chams hold
`draw_guard_` (`chams.hpp:210`), the camera holds `camera_guard_`
(`thirdperson.hpp:172`) — so the **policy** can differ per instance without the
camera changing behaviour.

**The requirement:** for the **chams**, the guard must **fail closed** — when no
slot can be claimed, the draw is **refused** rather than allowed in. The reason
is in §1.6: a fail-open thread is recorded in no slot, so the recursion is not
broken and the stack overflows.

Requirements:

1. The chams' refusal must be **visible**: the refusal path currently returns
   **before** `on_draw` (`chams.cpp:223-227`), so it moves no counter at all.
   The refusal must be **counted** and must appear in the **status line**.
2. A refusal must **not** be counted as a `seen` draw if `seen` means "draws the
   detour saw" — decide, state the definition, and keep `seen == drawn +
   skipped` true under it. The status line's meaning is documented in
   `docs/re/chams.md`; keep the three numbers consistent.
3. **The camera's behaviour must not change.** `payload/features/src/thirdperson.cpp`
   is out of scope. If the shared header's default changes, the camera's call
   site must keep its current behaviour, and you must say how you know.

### 2.6 The documentation (H8)

* `docs/re/chams.md`: fix the file name (`test_chams_draw_hook.cpp`, the Meson
  test `features-chams-draw-hook`) at `docs/re/chams.md:600` and `:693`, and
  **append** a section recording the fix — the protocol that was replaced, why
  it was unsafe, the probe's mismatch count before and after, the fail-closed
  decision, and the trampoline's lifetime rule. **Append only**: do not rewrite
  the existing sections.
* The shared header's citation of `docs/re/thirdperson.md` must name the file
  that exists.

---

## 3. What is out of scope

* The **third-person camera's behaviour** (`payload/features/src/thirdperson.cpp`)
  — it must not change.
* The **material system**, the colour setter, the material's parameter names
  and the restore path — all done (`docs/re/chams.md` §1–§5). Do not change
  them.
* The **draw hook's target** and its prologue verification — done by T26 and
  merged. Do not re-derive it.
* `wire::Settings` — unless your change forces it, and then say so in your
  report **before** you touch it (see `docs/orchestration/feature-brief.md` §0.5:
  it is shared between two separately compiled binaries and a version bump
  touches three files).
* The animfix's **behaviour**, the anti-aim, the ragebot and the control
  channel's protocol. (`payload/hooks/` **is** in scope for §2.2 — but only the
  publication ordering and the atomics, not the hook's mechanics.)

---

## 4. File ownership

You own:

* `payload/features/src/chams.cpp`
* `payload/features/include/cs2/features/chams.hpp`
* `payload/features/include/cs2/features/recursion_guard.hpp`
* `payload/hooks/include/cs2/hooks/inline.hpp` and
  `payload/hooks/src/inline.cpp` — **only** the publication ordering and the
  atomics (§2.2)
* `payload/hooks/tests/test_inline.cpp`
* `payload/features/tests/test_chams_draw_hook.cpp` (and a new test file if
  you want one)
* `payload/features/meson.build` and the root `meson.build` (registering any
  new test)
* `docs/re/chams.md` — **append only**

You do **not** own the overlay, the ESP, the ragebot, the anti-aim, the
animfix, the third-person camera's behaviour, the control channel's protocol or
the dumper. If you need something outside this list, say so in your report
instead of editing it.

---

## 5. The rules

* `docs/style-guide.md` and `AGENTS.md` are normative. C++23, 2-space indent,
  100 columns, K&R braces, trailing return types on declarations **and**
  definitions, fully qualified `std::`, errors as values, **no exceptions**,
  `[[nodiscard]]` / `noexcept`, value-init with `{}`, `static_cast` /
  `reinterpret_cast` only, no new global mutable state (ADR-0010).
* **New constants are `SCREAMING_SNAKE_CASE`** (the style guide's current
  form — `recursion_guard.hpp:93-106` already uses `SLOT_COUNT`,
  `TOTAL_SLOT_COUNT`). `k_desc_cache_size` is **legacy**: you may leave it
  alone, but do not add another `k_`-prefixed constant, and do not
  mass-rename the old ones as a drive-by.
* The payload is **manually mapped**: no CRT static-init, no `<print>`, **no
  `thread_local` in a detour** (the game's threads have no TLS block — that is
  what the third-person crash was). The fix must not introduce one.
* **No allocation on the draw path.** `status_line()` already had a
  `std::string` per frame (fixed by T26's fix round); do not put one back. The
  install/report path may allocate; the draw path may not.
* Every engine read goes through the fault-safe `game::read` helpers.
* Commit as you go, `type(scope): summary`. **Never** `git reset --hard`,
  `git checkout -- .`, `git clean`, `git stash`; never amend or rebase.

---

## 6. The verify loop

```powershell
# the project's environment FIRST - it keeps the cargo target directory and
# the tool caches out of the worktree
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

(The build directories are **outside** the worktree; do not run `meson setup`
inside it.)

**You cannot launch CS2 from here** — the agent runs in Windows session 0 with
no GPU. If you cannot run it, say plainly what is unverified.

---

## 7. What the report must state

1. **The protocol** — what the cache's publish/read protocol is after the fix,
   in words, with the memory orders and why they are right.
2. **The probe** — the host test, and the proof that it **fails** against the
   pre-fix protocol, with the numbers before and after.
3. **The other shared state** — every `g_active` / `trampoline_` /
   `installed_` / `desc_cache_stamp_` reader and writer you found, what you
   changed, and **one sentence** on why no thread can execute freed trampoline
   memory.
4. **The counters** — whether they are reset, why `seen == drawn + skipped`
   holds, and what each number now means.
5. **The fail-closed decision** — what the chams do on a refusal, why, and how
   the refusal is reported.
6. **The tests** — each one, and how it fails when the defect is reintroduced.
7. **What is still open** — the live run, and anything you could not verify.
8. The file list and the commit hashes.
