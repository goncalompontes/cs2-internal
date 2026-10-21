# The project-wide quality pass (T21)

**What this was:** a refactoring pass over the C++ and Python trees — RAII and
modern C++ adoption, structural cleanups, and two tooling improvements — with
**no behaviour change**. Every commit below was verified with the full loop
(the host build and tests, the four builds, the Python suite, the type gate)
before it was committed.

**What it is not:** it is not a feature, and it is not a formatting sweep. The
tree is still not clang-format-clean, on purpose: a whole-file reformat of a
3000-line file produces a diff nobody can review.

**The rules this pass worked under** (from `docs/orchestration/spec-t21-quality-pass.md`):

- no behaviour change — every refactor must be provably equivalent;
- no reformatting drive-bys — only the lines a change touches are reflowed;
- one concern per commit;
- the verify loop after **every** commit;
- the four files the concurrent control-channel work owns
  (`payload/control/src/input.cpp`, `payload/control/src/service.cpp`,
  `payload/control/include/cs2/control/input.hpp`,
  `payload/control/include/cs2/control/service.hpp`) were **not** touched.
  §5 below says what would have been done in them.

---

## 1. The inventory

Every finding, with where it is, what is wrong, and what it costs. "Fixed" means
this pass fixed it; the rest are in the backlog (§4).

### 1.1 The handles

The tree managed its OS handles by hand: acquire, use, close, with the close
repeated on each path out of the function.

| # | where | what is wrong | the cost | status |
|---|---|---|---|---|
| F1 | `payload/control/src/rage_config.cpp:84-206` | the kernel32 resolution (`CreateFileA`/`ReadFile`/`WriteFile`/`CloseHandle` type aliases, the `GetProcAddress` lookups and the null check) is written out **twice**, once in `rage_config_load` and once in `rage_config_save` | a change to the resolution has to be made twice, and the two copies can drift; ~30 duplicated lines | **fixed** |
| F2 | `payload/control/src/rage_config.cpp:104-112, 193-200` | the config file's handle is closed by hand | a future early return between the open and the close leaks the handle; the type system says nothing about it | **fixed** |
| F3 | `payload/control/src/service.cpp:677-699` (`do_log`) | the payload log's file handle is closed by hand, on three paths | same fragility as F2 | **not fixed** — the file is owned by the concurrent work (§5) |
| F4 | `payload/control/src/service.cpp:3407-3408`, `service.hpp:459` | the control service's thread handle is a bare `void*` and is closed by hand | same fragility; a `void*` member cannot be moved or copied safely | **not fixed** — owned by the concurrent work (§5) |
| F5 | `payload/control/src/channel.cpp:28-45`, `channel.hpp:45` | the named pipe's server end is a bare `void*` member, closed by hand in `Channel::close` | same fragility; the handle's type is invisible at the use sites | **fixed** |
| F6 | `payload/features/src/config_store.cpp:741-768` | the settings store's read and write close the file by hand | same fragility; the read's "the file does not fit" exit is a `break` that happens to fall through to the close | **fixed** |
| F7 | `payload/features/src/model_publisher.cpp:292-310, 360-372`, `model_publisher.hpp:124-126` | the segment's file, its file mapping and the publisher's thread are three raw handles, closed by hand, in a class that owns them | same fragility; three members whose "is it set?" test is `!= nullptr` | **fixed** |
| F8 | `payload/features/src/present_hook.cpp:19-62` | the present hook carries **its own copy** of the payload log's writer: the same `CreateFileA` resolution, the same `data_file_path` call, the same append mode, the same two writes and the same close — all of it already in `cs2::game::append_log` | 44 duplicated lines; two places to change the log's format | **fixed** |
| F9 | `payload/game/src/log.cpp:87-95` | the log writer closes the file by hand | same fragility | **fixed** |
| F10 | `payload/diag/src/watchdog.cpp:90-104, 117-132` | the freeze watchdog opens a thread and a thread snapshot and closes both by hand | same fragility; this is the **cold path of a crash handler**, where a leak is the least of the problems | **fixed** |
| F11 | `overlay/src/transport.cpp:69-92` | the layer's transport opens the segment's file and creates its file mapping, and closes both by hand | same fragility | **fixed** |
| F12 | `overlay/src/transport.cpp:32, 92` | the layer keeps the mapping in a global that is **never** closed | a real (benign) leak: the mapping lives for the layer's lifetime and is released by the OS at unload | **not fixed** — see §4.4 (fixing it changes what happens at unload) |
| F13 | `injector/src/main.cpp:49-64, 72-85, 206-222, 453-462, 514-521, 667-676, 690-703, 880-890` | eight handles (four toolhelp snapshots, four remote threads) closed by hand | same fragility, in the tool that runs once and exits | **fixed** (the eight) |
| F14 | `injector/src/main.cpp:749` | the target process handle is never closed | benign: the tool exits and the OS reclaims it | **not fixed** — §4.4 |
| F15 | `injector/tests/trivial_dll.cpp:10` | the test aid's file handle is closed by hand | trivial | **not fixed** — §4.4 |

**A correction to the inventory this pass was given.** The task's debt list
says the manual handling in `do_log` is "`CreateFileA` → three early returns →
`CloseHandle` (a leak on two of them)". **That is not what the tree does.**
Every site above closes its handle on every path today; the `do_log` code
closes it explicitly on the `GetFileSizeEx` failure path and on the success
path, and the "no file yet" path returns before the handle exists. The finding
is therefore **fragility, not a live bug**: the next early return added
between an acquire and its close leaks the handle, and nothing in the types
says so. That is why this pass is a pure refactor: it changes no behaviour,
and it fixes no bug — it removes the shape in which the next bug can appear.

### 1.2 The tooling

| # | where | what is wrong | the cost | status |
|---|---|---|---|---|
| F16 | `scripts/clang-tidy-fix.py` | the sweep could *apply* fixes; nothing could ask it whether the tree was **clean** | the style debt is invisible and a change can regress it silently | **fixed** — a `--check` mode, a gate, and a unit test |
| F17 | (no `.github/workflows`) | no CI: the verify loop only ran by hand, on one machine | a regression is found by whoever runs the loop next | **fixed** — a workflow that runs the loop |
| F18 | `scripts/clang-tidy-fix.py` (the groups) | the tree is not clean under most groups: 670 findings across the ten groups (measured, §6.3) | the sweep's result cannot be a gate | **partly fixed** — the gate covers the two groups the tree *is* clean under; §4.2 has the rest |

### 1.3 The structure

| # | where | what is wrong | the cost | status |
|---|---|---|---|---|
| F19 | `payload/features/src/menu.cpp:178-260` and `overlay/src/menu.cpp:94-280` | the **window-input mechanism** is implemented twice: the WndProc subclass, the mouse/keyboard capture, the cursor handling and the toggle polling are ~150 lines in each renderer | a change to the input capture has to be made twice, and the two copies have already drifted (the payload also blocks the raw input through `GetRawInputBuffer`; the layer does not) | **not fixed** — §4.1 |
| F20 | `payload/control/src/occlusion_query.cpp` (1418 lines) | a self-contained subsystem in one translation unit | a compile unit nobody wants to touch | **not fixed** — §4.3 |
| F21 | `payload/control/src/service.cpp` (3274 lines), `input.cpp` (2029 lines) | one class per file, with the command dispatch, the ragebot glue and the settings plumbing in one place | the same as F20 | **not fixed** — §5 (owned by the concurrent work) |

---

## 2. The commits

One line each: the commit, what it changed, the finding it fixes, and how it
was verified. **Every** commit was verified with the full loop before it was
committed: `meson compile -C build`, `meson test -C build`, the three
`clang-cl` builds (payload, overlay, injector), `python -m unittest discover -s
scripts/scenario/tests -t scripts`, and `python scripts/typecheck.py`.

| commit | what it changed | finding | verified by |
|---|---|---|---|
| `ef52e5f` | **feat(core):** a shared RAII handle — `payload/core/include/cs2/core/handle.hpp` (a `ScopedHandle<Handle, Policy>` template) and `cs2/core/win_handle.hpp` (the Windows policy), plus the host test that proves the ownership rules | F2, F5-F7, F9-F11, F13 (the enabler) | the full loop; the new `core-handle` host test |
| `ad36dcb` | **refactor(core):** the policy is a value, not a type, so the wrapper can carry a closer that was resolved at run time | as above | the full loop; the host test (the policy travels with the handle) |
| `80f6c50` | **refactor(control):** one kernel32 resolution for the rage config's file I/O | F1 | the full loop |
| `c3acfcd` | **refactor(control):** the rage config's file handle is owned by the RAII wrapper | F2 | the full loop |
| `0a08f17` | **refactor(control):** the control channel's pipe handle is owned by the RAII wrapper | F5 | the full loop |
| `433a5e7` | **refactor(features):** the config store's file handles are owned by the RAII wrapper | F6 | the full loop |
| `d884957` | **refactor(features):** the model publisher's handles (file, mapping, thread) are owned by the RAII wrapper | F7 | the full loop |
| `c3b5676` | **refactor(features):** the present hook logs through the shared log writer (44 duplicated lines gone) | F8 | the full loop |
| `db35a96` | **refactor(game):** the payload log's file handle is owned by the RAII wrapper | F9 | the full loop |
| `d111fd4` | **refactor(diag):** the watchdog's thread and snapshot handles are owned by the RAII wrapper | F10 | the full loop |
| `1e20e4e` | **refactor(overlay):** the transport's file and mapping handles are owned by the RAII wrapper | F11 | the full loop |
| `11b4384` | **refactor(core):** a name for the kernel handle the wrapper owns (`core::ScopedKernelHandle`) — a rename | readability | the full loop |
| `d2b884c` | **refactor(injector):** the injector's eight handles are owned by the RAII wrapper | F13 | the full loop |
| `e1ff737` | **style(core):** the new code is clean under the sweep's `nodiscard` and `const` groups | F18 (the new code's own findings) | the full loop; the sweep's `nodiscard` group reports 0 findings |
| `79cb16e` | **tool(scripts):** the sweep can *check* the tree, not only fix it — `--check`, `--gate`, `--list-gate`, and a unit test for the decision | F16, F18 | the full loop; the new `scenario-clang-tidy-fix` suite (10 tests) |
| `c183928` | **ci:** a workflow that runs the verify loop on every push | F17 | every step of the workflow was run locally first (§7) |

---

## 3. The new module

`payload/core/` is a **header-only** module with one job: the owning handle.

```cpp
template <typename Handle, typename Policy>
class ScopedHandle {
 public:
  ScopedHandle() noexcept = default;
  explicit ScopedHandle(Handle handle) noexcept;
  ScopedHandle(Handle handle, Policy policy) noexcept;
  ~ScopedHandle();                       // releases what it holds
  // move-only; `reset()` releases and takes a new handle;
  // `release()` hands the ownership over without releasing.
};
```

Three decisions worth knowing about:

- **A policy, not a deleter.** The policy says how to recognise a live handle
  and how to release one. It is a *value*, not a type, because the payload
  resolves kernel32's entry points by name (the rage config does) and the
  closer it needs is only known at run time.
- **Not `std::unique_ptr`.** Its deleter is type-erased through a virtual
  call, which is exactly what the policy parameter replaces, and it drags in
  `<memory>` (and with it the CRT's allocator) for a payload that is manually
  mapped and avoids the CRT.
- **Platform-independent on purpose.** The ownership rules — the scope exit,
  `reset`, the move, the move-assignment, the hand-off — are therefore
  host-testable, which the Windows-only sources that use the wrapper are not.
  `payload/core/tests/test_handle.cpp` drives them with a counting policy.
- **A policy has to be copyable, and has to say whether it can work.** The
  move *copies* the policy (so a moved-from wrapper keeps a usable one and
  no path can call a moved-from policy), and the policy answers `usable()`
  before the wrapper asks it anything else (so a policy whose default state
  cannot work — a `std::function` closer that is empty — is never asked).
  Both are requirements on **every future policy**, and the header states
  them.

**The ownership rules themselves** (the review round tightened two of them,
§9):

- the wrapper tracks whether it **owns** what it holds, and the policy is
  asked about a handle only when the wrapper *takes* one. The destructor
  therefore never asks a policy about a handle it does not own — in
  particular it never asks a **moved-from** policy, which for a policy that
  carries a `std::function` closer would throw `std::bad_function_call` out
  of a `noexcept` destructor;
- the **empty value is reserved**: `Handle{}` is the wrapper's "holds
  nothing", it is never owned and never released. A handle kind whose zero
  value is live (a POSIX file descriptor, where 0 is stdin) must be wrapped
  in a type whose empty value is distinct; the wrapper's own rule keeps such
  a handle from being closed, at the price of leaking it.

The Windows policy (`cs2/core/win_handle.hpp`) is separate and Windows-only:
it includes `<windows.h>` and closes with the imported `CloseHandle`. The
headers that *own* a handle but must stay free of `<windows.h>`
(`cs2/control/channel.hpp`) declare their own closer and let the
implementation supply it.

**Why a new module and not a header in an existing one?** The wrapper is used
by the payload's modules *and* by the overlay and the injector, which are
separate Meson projects that share no dependency graph with the payload. A
header-only module of its own is the one place all of them can include.

---

## 4. The follow-up backlog

What this pass deliberately did **not** do, why, and the concrete plan. The
order is the priority order.

### 4.1 The two menus' window-input mechanism (F19) — *high*

**Why not now:** the two renderers' input handling is the most delicate code
in the tree (a WndProc subclass in the game's own window, with the cursor and
the raw input in play). Merging it is a *behaviour*-sensitive refactor of
something that has no host test, and it cannot be verified without the game.

**The plan.**

1. New header `payload/features/include/cs2/features/window_input.hpp`
   (the payload's side; the layer includes it through
   `overlay/include/../cs2/...` — or, better, move it to the shared
   `cs2/core`-style module so both projects can include it).
   It owns: the WndProc subclass (`SetWindowLongPtrW` + the restore), the
   mouse/keyboard capture, the cursor show/hide, the toggle polling and the
   keybind capture.
2. The seam is the *differences*, which are small and can be parameters:
   the payload blocks the raw input through a `GetRawInputBuffer` detour and
   the layer does not; the payload's window is the game's, the layer's is its
   own.
3. Move `overlay/src/menu.cpp`'s `hooked_wndproc` and
   `payload/features/src/menu.cpp`'s `Menu::window_proc` into it, one after
   the other, keeping the live behaviour: the payload first (it is the
   default renderer, ADR-0023), then the layer.
4. **The test:** the message dispatch is pure enough to host-test — a
   `window_input.hpp` that takes the message, the wparam/lparam and a
   "what do we do with it" callback can be driven headless, and the host
   suite can then assert the *same* decisions for both renderers.

**Risk:** medium. It touches the render thread's input path. The mitigation is
the extraction order above: the shared header first (no behaviour change, the
callers keep their own code), then one caller at a time.

### 4.2 The clang-tidy debt (F18) — *medium*

**Why not now:** the sweep's groups are not all *compatible* with the house
style, and the ones that are compatible still have findings in the files the
concurrent work owns. Applying them blindly would fight `docs/style-guide.md`.

**The measurement** (§6.3 has the method) and the exact remaining findings:

| group | findings | why it cannot be gated yet |
|---|---|---|
| `performance` | 0 | **gated today** |
| `nodiscard` | 0 | **gated today** (this pass cleared the one finding it had) |
| `const` | 3 | `payload/control/src/input.cpp:2049` (owned by the concurrent work), `payload/tests/test_settings_sink.cpp:55` (fixable: make `body` const), `:78` (a false positive: a `const std::ifstream` is not the point) |
| `modernize-safe` | 6 | `payload/control/src/service.cpp:722, 3213` (owned by the concurrent work); the other four want `return {x};` where the explicit constructor reads better |
| `modernize-types` | 8 | wants `auto` exactly where the style guide wants the type spelled out ("auto where the type is evident; spell the type out when it carries meaning") — e.g. `overlay/include/cs2/overlay/menu_pages.hpp:556` |
| `readability` | 9 | `payload/control/src/input.hpp:666` and `payload/control/src/service.cpp:187` (owned by the concurrent work), `external/Vulkan-Headers/.../vk_layer.h` (vendored) |
| `readability-extra` | 40 | 11 in `payload/control/src/service.cpp` (owned by the concurrent work) |
| `trailing-return-type` | 134 | the two biggest files are owned by the concurrent work |
| `modernize-extra` | 445 | includes the two biggest files |
| `misc-extra` | 3 | all three are false positives: `misc-redundant-expression` on comparisons that are not redundant (`payload/game/src/camera_live.cpp:99`, `payload/tls/src/bootstrap.cpp:49`, `payload/diag/src/watchdog.cpp:105`) |

**The plan.**

1. Fix `payload/tests/test_settings_sink.cpp:55` (make `body` const) and
   decide about `:78` (either a `// NOLINT(misc-const-correctness)` with a
   reason, or exclude the check for streams).
2. When T19/T20 land, fix the two `const` findings and the two
   `modernize-safe` findings in `service.cpp`/`input.cpp` and add
   `modernize-safe` to `GATED_GROUPS`.
3. For the groups that fight the house style (`modernize-types`' `use-auto`),
   either drop the check from the group or add the exceptions the style guide
   implies. The sweep's `GROUPS` table is the place.
4. For the false positives (`misc-extra`), either exclude the check or
   restructure the three comparisons so the check is happy.
5. Every step is a commit of its own, verified with the full loop, and the
   gate is widened in the same commit — so the gate never goes red.

### 4.3 The occlusion query's translation unit (F20) — *low*

**Why not now:** it is a self-contained subsystem that already has its own
unit and its own header, so the split is a *mechanical* move with a real risk
of a merge conflict with the concurrent work (both touch
`payload/control/`). The value is low: the file compiles in a few seconds.

**The plan:** split `payload/control/src/occlusion_query.cpp` along its
existing section dividers — the world-AABB/analytic-primitive part and the
engine-read part (`resolve_entity`, the overlap records) — into
`occlusion_query_geometry.cpp` + a private header
`payload/control/src/occlusion_query_detail.hpp`, the way
`payload/game/src/camera_detail.hpp` already does it. The public header
(`cs2/control/occlusion_query.hpp`) does not change. The seam is the
`OcclusionQueryStats`/`OcclusionQueryResult` structs and the engine-read
helpers, which are already grouped.

### 4.4 The remaining raw handles (F12, F14, F15) — *low*

- `overlay/src/transport.cpp:32` — `g_mapping` is never closed. Making it a
  `core::ScopedKernelHandle` would close it at the layer's unload, which is a
  **behaviour change** (a `CloseHandle` at unload), so it is a *fix*, not a
  refactor, and it belongs in its own commit.
- `injector/src/main.cpp:749` — the target process handle is never closed.
  Same argument: the tool exits, the OS reclaims it.
- `injector/tests/trivial_dll.cpp:10` — a two-line test aid.

### 4.5 The Linux/macOS preflight (the spec's "the preflight script is
Windows-only") — *low*

`scripts/preflight-windows.ps1` is Windows-only. **No decision was taken**:
ADR-0008 says the tooling languages are Python for glue and C++ for the
payload, and nothing in the tree claims a Linux build of the *payload*. The
host build does build on Linux (the root `meson.build` is written for it), and
the CI workflow runs on Windows because the payload is a Windows artifact. If
a Linux developer flow is wanted, the preflight's checks should be ported to a
Python script (`scripts/preflight.py`) that both platforms run, rather than
duplicated in a shell script. **Not started** — it needs a decision about
whether the project supports a Linux host build as a first-class target.

### 4.6 The format check (deliberately **not** in the CI) — *low*

The specification's CI bullet asks for "the format check". There is none,
and that is a decision, not an oversight: **the tree is deliberately not
clang-format-clean** (docs/style-guide.md §7 rule 3 — a whole-file reformat
of a 3000-line file produces a diff nobody can review), so a format check
would be **red on its first run** and would stay red until the whole tree was
reformatted, which is exactly the reformatting drive-by this pass's own rules
forbid. The style gate is the sweep's *check mode* over the two groups the
tree is clean under (§4.2), and that is what the CI runs.

**The plan, if the tree is ever reformatted on purpose:** the reformat is
its own commit (a separate, deliberate one), and the format check goes in
with it — `clang-format --dry-run --Werror` over the tracked sources. Until
then the gate that exists is the honest one.

---

## 5. The four contended files

`payload/control/src/input.cpp`, `payload/control/src/service.cpp`,
`payload/control/include/cs2/control/input.hpp` and
`payload/control/include/cs2/control/service.hpp` are owned by the concurrent
anti-aim (T19) and animfix (T20) work. This pass did not touch them. What
would have been done, and how risky it would have been:

**`service.cpp` — `do_log` (F3).** The log file's handle would become a
`core::ScopedKernelHandle`, exactly like the config store's
(`payload/features/src/config_store.cpp` is the model). The two `CloseHandle`
calls collapse into the wrapper's destructor, and the error return that
precedes one of them (`GetFileSizeEx` failed → close → report) stops being a
special case. **Risk: low** — the change is mechanical and the two files do
not interact — but the *merge* risk is high: T19 and T20 both edit
`service.cpp`, and a third edit of the same lines turns the merge into a
conflict minefield. That is why it was deferred rather than done.

**`service.cpp` / `service.hpp` — the thread handle (F4).**
`void* thread_` (service.hpp:459) would become a
`core::ScopedKernelHandle`, and `stop()` would `WaitForSingleObject` on it
and then reset it. **Risk: low** mechanically, **medium** in review: the
service's thread is started and stopped by the command dispatch, and the
member's type change touches the class's interface. Same merge reason.

**`input.cpp` (2029 lines) — the CreateMove hooks, the strafe, the bhop and
the silent-aim writers.** The measurable win would be to split the file (its
own seam: the silent-aim writers are independent of the strafe's geometry),
but the file *is* the input path, it has no host test of its own beyond
`test_control_input`, and the anti-aim work is changing exactly the angle
maths in it. **Risk: high** — a split that moves the silent-aim writers while
the anti-aim changes the same file's angle handling would be very hard to
review and impossible to verify without the game. Deferred on purpose.

**`input.hpp` (817 lines) / `service.hpp` (624 lines) — the large public
surfaces.** The win would be to separate the *interface* from the *state*
(the members are already grouped; the interface is not). **Risk: high** — it
changes every call site's type, and the two headers are the interface the
concurrent work is extending. Deferred.

**What this means for the merge:** the contended files are untouched, so
T19/T20/T21 merge in any order without a conflict in them. This pass does
touch two files *in the same module* — `payload/control/meson.build` (an
include directory) and `payload/control/include/cs2/control/channel.hpp` (a
private member's type) — neither of which is one of the four contended files.
If the concurrent work also edits `payload/control/meson.build` (e.g. to add
a source file), that one file may conflict; the conflict would be a one-line
include list on each side, and it is easy to resolve by hand.

**The root `meson.build`, and what a new host test has to take.** The pass
also changes the **root** `meson.build` — the file where a *new* host test is
registered — and the reason is the shared handle. `cs2/control/channel.hpp`
includes `cs2/core/handle.hpp`, and `cs2/control/service.hpp` includes the
channel, so **every** consumer of the control headers needs the core include
directory as well (`cs2/features/model_publisher.hpp` does the same for the
features headers). A test that forgets fails with

```
cs2/core/handle.hpp: No such file or directory
```

which is a confusing way to learn that a header two directories away is
included by the one you are compiling. The pass therefore *publishes* the
pairs instead of re-listing the include directories in every test:

```meson
control_host_dep = declare_dependency(
  include_directories : [include_directories('payload/control/include'), core_inc],
)
features_host_dep = declare_dependency(
  include_directories : [include_directories('payload/features/include'), core_inc],
)
```

**A new host test that includes a control header takes `control_host_dep`
(and one that includes a features header takes `features_host_dep`)** — that
is the whole of what it has to know. The root `meson.build` is otherwise
untouched by the pass: the module's own host test is wired in the module
(`payload/core/meson.build`, guarded by `if not meson.is_cross_build()`, like
the loader's and the hooks'), so the root file carries only the
`subdir('payload/core')` line and the two dependencies.

**What the concurrent work has to watch for in the root file:** the two
dependency definitions sit next to the `subdir('payload/core')` line, near the
top of the file, and the host tests that use them are in the middle of it. If
T19/T20 add a host test to the root `meson.build` *and* that test includes a
control header, it must take `control_host_dep`; a conflict, if there is one,
is a one-line `dependencies :` list on each side.

---

## 6. The measurements

### 6.1 The handles

**Method** (git bash; the *same* file list is used for both revisions, so the
comparison is like for like — the file list is the tip's, the base commit is
read out of the object store):

```sh
# the tracked C/C++ sources, without the vendored trees
git ls-files '*.c' '*.cpp' '*.h' '*.hpp' \
  | grep -v -e '^external/' -e '^payload/third_party/' -e '^payload/sdk/' > /tmp/t21-files.txt

# the occurrences of the token `CloseHandle`
git grep -o -h 'CloseHandle' e539bc6 -- $(cat /tmp/t21-files.txt) | wc -l
git grep -o -h 'CloseHandle' -- $(cat /tmp/t21-files.txt) | wc -l

# the raw `HANDLE` locals (one per matching line)
git grep -E -h -c '^\s*(const\s+)?HANDLE\s+\w+\s*=' e539bc6 -- $(cat /tmp/t21-files.txt) \
  | awk '{s+=$1} END {print s}'
git grep -E -h -c '^\s*(const\s+)?HANDLE\s+\w+\s*=' -- $(cat /tmp/t21-files.txt) \
  | awk '{s+=$1} END {print s}'
```

| what | before | after |
|---|---|---|
| `CloseHandle` (occurrences of the token) | 35 | 14 |
| raw `HANDLE` locals | 25 | 5 |

Before, the 35 occurrences were: 9 in `rage_config.cpp`, 8 in the injector, 4
in the model publisher, 3 in the contended `service.cpp`, 3 in the overlay's
transport, 2 in the watchdog, 2 in the config store, and one each in the
present hook, the payload log, the channel and the injector's test aid.

Of the 14 that are left: 7 are in `rage_config.cpp` (the type aliases, the
comments and the policy's own `close`), 2 are the shared policy in
`cs2/core/win_handle.hpp`, 1 is the channel's closer, **3 are in the
contended `service.cpp`** (F3, F4), and 1 is the injector's trivial test aid.
The count is of *occurrences of the token*, not of call sites: seven of the
fourteen are the type aliases and the comments the method also counts.

Of the 5 remaining raw `HANDLE` locals: 1 is the channel's local (handed
straight to the wrapper), 1 is the contended `service.cpp`, 1 is the overlay's
`g_mapping` global (F12), 1 is the injector's target process handle (F14) and
1 is the injector's test aid (F15). (The wrapper's own member is a
`KernelHandle` — an opaque `void*` — and not a raw `HANDLE` local, so it is
not one of the five.)

### 6.2 The files

**Method:** `git show <base>:<path> | wc -l` against the file at the tip.

| file | before | after |
|---|---|---|
| `payload/features/src/present_hook.cpp` | 182 | 144 |
| `payload/control/src/rage_config.cpp` | 192 | 230 |
| `payload/control/include/cs2/control/channel.hpp` | 38 | 54 |
| `payload/features/src/config_store.cpp` | 968 | 972 |
| `payload/features/src/model_publisher.cpp` | 359 | 361 |
| `payload/features/include/cs2/features/model_publisher.hpp` | 116 | 120 |
| `payload/game/src/log.cpp` | 90 | 94 |
| `payload/diag/src/watchdog.cpp` | 159 | 163 |
| `overlay/src/transport.cpp` | 203 | 210 |
| `injector/src/main.cpp` | 824 | 831 |
| `scripts/clang-tidy-fix.py` | 448 | 518 |

The only file that got *smaller* is the present hook (−38 lines: the
duplicated log writer). Everything else grew a little: the wrapper's
declarations and the comments that say why.

### 6.3 The clang-tidy findings

**Method:** the sweep's own dry run — `python scripts/clang-tidy-fix.py
--group <group> -j 8` — over the 101 translation units of the four build
dirs, with the sweep's header filters. It was run twice on the same tree:
once at the commit **before this pass's own style commit** (`d2b884c`, i.e.
with the refactors applied and the new code not yet tidied) and once at the
tip. The base commit's numbers were not measured separately: the sweep
analyses the tree in place, and the base is fifteen commits back.

| group | with the refactors | at the tip |
|---|---|---|
| `performance` | 0 | 0 |
| `nodiscard` | 1 | **0** |
| `const` | 24 | **3** |
| `modernize-safe` | 6 | 6 |
| `modernize-types` | 8 | 8 |
| `readability` | 9 | 9 |
| `misc-extra` | 3 | 3 |
| `readability-extra` | 40 | 40 |
| `trailing-return-type` | 134 | 134 |
| `modernize-extra` | 445 | 445 |

The 22 findings this pass removed are all in the code this pass added or
touched: the policies' `is_valid` was not `[[nodiscard]]` (1), and the handle
locals that are never reset were not `const` (21). The 3 that are left are
the pre-existing ones listed in §4.2. **The tree introduces no new finding
under the gated groups, and the gated groups are clean.**

### 6.4 The suites

| | before | after |
|---|---|---|
| the host suite (`meson test -C build`) | 42 tests | **44** (`core-handle`, `scenario-clang-tidy-fix`) |
| the Python suite (`unittest discover`) | 387 tests | **404** (17 new: the sweep's check mode) |
| mypy (`scripts/typecheck.py`) | 0 errors, 51 files | **0 errors, 52 files** |
| the four builds | all compile | all compile |

### 6.5 What was **not** measured

- **The hot path.** `payload/aim/` was **not** touched — not one line — so no
  benchmark was needed and none was run. The solver's tick budget
  (`docs/research/aim-solver-design-review.md`) is unchanged by
  construction.
- **The build time.** The only build-file change is an include directory
  (`payload/core/include`), which cannot measurably change the build time.
  The numbers would have been noise.
- **The live game.** See §7.

---

## 7. What could not be verified

Stated plainly, because the difference between "verified" and "believed" is
the whole point of this pass:

1. **The live game.** This session runs in Windows session 0: no GPU, no
   interactive desktop, so CS2 cannot be launched (the `CS2Launch` scheduled
   task needs session 1). **Nothing here was exercised in the game.** The
   payload's behaviour — the control channel, the menu, the shared segment —
   is unchanged by construction (§8), but "unchanged by construction" is not
   "observed".
2. **The CI workflow on a GitHub runner.** The workflow's *steps* were all
   run locally, in the same order, with the same compiler (clang-cl) — the
   host build and tests, the three Windows builds, the Python suite, the
   type gate and the style gate. The two steps that *fetch* something were
   run in the way the runner runs them: the Vulkan headers are cloned into a
   scratch copy of the tree (and the copy **without** them is what proves
   the step is needed — §9), and the payload is configured with the cross
   file — the command, the `dumpbin` result and the `build-payload/`
   directory that carries the artifact are in §9. What could not be verified is the
   *runner's* environment: that `ilammy/msvc-dev-cmd` and the runner's
   LLVM are where the workflow expects them. The style gate's clang-tidy is
   pinned (LLVM 22.1.8, the version the gated groups were measured with),
   so that one no longer depends on the runner's image. The **compiler**
   does depend on it: clang-cl comes from the runner image's LLVM, which is
   not pinned (the workflow's header says so, and records the image's
   facts).
   **The whole workflow was also run on a fresh checkout** (`git archive
   HEAD`: no build directories, no `external/Vulkan-Headers`, no generated
   SDK headers, no calibration dumps, no prebuilt dumper). Every step was
   green: the host build (which builds the Rust dumper from source and
   writes *empty* SDK headers), 44/44 host tests, the payload build (the
   cross file, `/MT`, and a `payload.dll` that imports no CRT), the Vulkan
   clone and the overlay build, the injector build, the Python suite (404),
   the type gate, and the style gate with the pinned clang-tidy.
3. **The SDK the payload is built against.** The SDK headers
   (`payload/sdk/*.hpp`) are generated from the calibration dumps, and the
   dumps are git-ignored: they come from a live calibration against a
   running game. **There are no dumps in the tree**, so the generator writes
   *empty* headers and the payload is built with **zero offsets** — here,
   on CI, and on any fresh checkout. The payload the CI compiles is
   therefore a *compile* of the payload, **not** the shipped artifact; the
   shipped one is built after a calibration (docs/windows-validation.md).
   The generated headers are also written *into the source tree*, so a build
   leaves `payload/sdk/*.hpp` behind (they are git-ignored).
4. **A Linux host build.** The CI runs on Windows because the payload is a
   Windows artifact and that is the configuration this repository is verified
   with. A Linux runner was not available here, so the host build's
   portability to Linux is **not** verified by this pass.
5. **The injector's runtime.** The injector compiles; it was not run against
   a target (no game, and the injector's own test aids are built, not
   exercised).

---

## 8. Why these refactors are equivalent

The reviewer will look hardest here, so the argument is spelled out.

**The wrapper's semantics.** `ScopedHandle` holds the handle, releases it when
it goes out of scope, and releases it on `reset()`. The two operations the
old code performed by hand — `CloseHandle(file)` at the end of a function and
the early `CloseHandle(file)` before an error return — are exactly what the
destructor and `reset()` do. The *order* of the releases was preserved
deliberately at every site (the review round found two places in the injector
where it was not, and they were put back — §9):

- the rage config's **load** closes the file *before* the buffer is parsed
  (the read is in an inner scope);
- the rage config's **save** closes the file *before* the result is returned
  (the write is in an inner scope);
- the config store's **read** closes the file after the loop, as before;
- the model publisher closes the **file** right after the mapping is created
  (the file is in an inner scope) and the **mapping** at the end of `map()`;
- the watchdog closes the **thread** at the end of `log_thread` and the
  **snapshot** at the end of `report_freeze`;
- the overlay's transport closes the **file** right after the mapping is
  created, and the **mapping** on the two failure paths; the mapping that
  succeeds is handed over with `release()` and stays in the global, exactly as
  the raw handle was;
- the injector closes each **remote thread** right after its exit code is
  read, and each **snapshot** when the walk is over.

**The two checks that changed shape.** The old code tested the raw handle
(`file == INVALID_HANDLE_VALUE`, `thread == nullptr`, `snapshot !=
INVALID_HANDLE_VALUE`); the new code asks the wrapper (`!file.valid()`). The
kernel policy treats a null handle as invalid *as well as*
`INVALID_HANDLE_VALUE`. `CreateFileA`, `CreateToolhelp32Snapshot`,
`CreateRemoteThread`, `OpenThread` and `CreateNamedPipeA` report failure with
`INVALID_HANDLE_VALUE` (or with null, for `OpenThread` and
`CreateRemoteThread`), and never with the other value — so the set of
reachable outcomes is unchanged. The one *theoretical* difference is a handle
that the OS returned as null where the old code would have carried on: the new
code returns early instead. That is the strictly safer of the two, and it is
unreachable.

**The one place the closer changed.** The rage config resolves kernel32's
entry points by name instead of importing them, so its file is released by
the *resolved* `CloseHandle`, not by the imported one. The wrapper's policy
carries that resolved pointer with it (`FilePolicy{api.close_handle}`), so
the file is released by exactly the entry point the rest of the file uses. The
resolution itself is unchanged: the same module, the same names, the same
null checks over the same subset of entry points (the load needs
create/read/close, the save needs create/write/close).

**The de-duplication.** The present hook's private log writer was replaced by
`cs2::game::append_log`. The two opened the same file
(`cs2-payload.log` under the payload's data directory), in the same append
mode, with the same share mode, wrote the same bytes (the message, then a
newline) and closed the file. The only difference was that one took a
`const char*` and measured it with a loop and the other takes a
`std::string_view` — the same number of bytes for a NUL-terminated string.

**The tests.** The new host test (`payload/core/tests/test_handle.cpp`) was
**broken on purpose** and watched to fail: with the destructor's `reset()`
removed, the suite fails at the first scope exit; with the move constructor
not releasing the source's handle, it fails at the move. The sweep's check
mode was broken on purpose too: with `gated_findings` always returning an
empty list, two of the ten new Python tests fail.

**The review round's tests** were broken on purpose in the same way (§9):
with the wrapper's old "ask the policy whether the empty value is live" rule
put back, the host test ends the process with `std::bad_function_call` (the
moved-from policy is called) and fails `!descriptor.valid()` (the zero value
was owned); with the sweep's compile-error rule removed, two of the three new
Python tests fail.

---

## 9. The review round

The pass was reviewed adversarially (the reviewer re-derived every release
point of every converted handle and mutation-proved the new tests). The
verdict was **ship with fixes**: the refactor itself is sound — no behaviour
change, no use-after-close, every number in §6 confirmed — but the **new CI
workflow was red on its first run**, which is the one failure the
specification forbade.

### 9.1 What the review found, and what was done

| finding | what it was | what changed | how it was proven |
|---|---|---|---|
| **B1** (blocker) | the workflow built the overlay without `external/Vulkan-Headers`, which is git-ignored and never fetched: the job died at configure time | the runner clones the headers (a pinned tag) before the overlay is configured | a scratch copy of the tree **without** the headers fails with `Include dir ../external/Vulkan-Headers/include does not exist` (exit 1); with the clone step it configures and links `cs2_overlay_layer.dll` |
| **M1** | the workflow built the payload *without* the cross file, so meson picked `/MD`: the DLL imported `MSVCP140.dll`, `VCRUNTIME140.dll` and the `ucrtbase` api-set — an artifact the injector's manual loader would have to resolve inside `cs2.exe` | the workflow configures the payload with `toolchain/windows-msvc.ini` (`b_vscrt = mt`) | the command is `meson setup build-payload payload --cross-file toolchain/windows-msvc.ini`; `dumpbin /dependents build-payload\payload.dll` on that build lists only `KERNEL32.dll`, `USER32.dll` and `D3DCOMPILER_47.dll`, while a build without the cross file lists `MSVCP140.dll`, `VCRUNTIME140.dll` and the `api-ms-win-crt-*` set. The **`build-payload/` directory of the branch worktree** carries the evidence: `b_vscrt = mt` in its `meson-info/intro-buildoptions.json`, and `payload.dll` next to it |
| **M2** | the move constructor moved the policy, so the moved-from wrapper's destructor called a **moved-from** policy (a `std::function` closer is empty after the move → `std::bad_function_call` out of a `noexcept` destructor) | the wrapper tracks the ownership itself (`owns_`); the policy is asked about a handle only when one is *taken* | the host test drives a `std::function` policy: with the old wrapper the test ends the process with `std::bad_function_call`; with the new one it passes |
| **M3** | the wrapper's empty state was `Handle{}` and the destructor asked the policy whether that value was live — an undocumented, unenforced requirement | the **empty value is reserved**: the wrapper never owns it and never releases it, and `reset()` with the handle the wrapper already holds keeps it instead of releasing it twice | the host test drives a policy whose zero value is live: with the old wrapper it fails `!descriptor.valid()` (and would close stdin); with the new one the zero value is left alone and a handle is released exactly once |
| **m1** | two releases in the injector happened later than they used to (the toolhelp snapshot, the entry thread) | both handles are in an inner block again | the diff against the pre-pass code: the release points are the same lines as the hand-written `CloseHandle` calls |
| **m2** | the pass changed the root `meson.build`, and a new host test that includes a control header would fail with `cs2/core/handle.hpp: No such file or directory` | the control and the features include paths are published as `control_host_dep` / `features_host_dep`; the module's test is wired in the module | a scratch translation unit that includes `cs2/control/channel.hpp`: without the core include directory it fails with exactly that error, with `control_host_dep`'s include set it compiles |
| **m3** | `docs/style-guide.md` §7 documented only the fix modes | §7 documents `--check`, `--list-gate` and `--gate`, and the rule that a compile error is a failed translation unit | — |
| **m4** | the spec's CI bullet asks for a format check; there is none, and nobody said why | §4.6 says why (the tree is deliberately not clang-format-clean, so the check would be red on its first run), and the workflow's header says the same | — |
| **m5** | a compile error was counted as a finding of the group that was running, so the gate said "performance reported a finding" | `clang-diagnostic-error` is not a finding; the sweep's "failed on N translation unit(s)" path carries it | a broken `payload/game/src/log.cpp`: the gate now says "failed on 1 translation unit(s)" and exits 1, where it used to say "performance reported a finding" |
| **m6** | the workflow installed the **latest** mypy and ran the runner's clang-tidy, while `requirements-dev.txt` pins mypy and says that a gate which changes its mind is not a gate | the workflow installs `requirements-dev.txt` and puts the **pinned** clang-tidy (LLVM 22.1.8) first on `PATH` | the gate was run with the pinned binary: "LLVM version 22.1.8", 0 findings |
| the nits | the new test's closing brace was glued to the method's; the report's `CloseHandle` row counted tokens and said "call sites"; four workflow steps shared two names; the module's test was wired in the root file; the single-argument constructor's default policy could not release | all five are fixed | the host suite is 44 and the workflow's step names are unique |

### 9.2 The commits

| commit | what it changed |
|---|---|
| `71fc87a` | **ci:** the runner fetches the Vulkan headers the overlay needs (B1) |
| `8fb7c2d` | **ci:** the payload is built the way it is shipped (static CRT) (M1) |
| `c0824ef` | **ci:** the gates run the tools they were baselined with (m6) |
| `58ea7dd` | **ci:** every step has its own name (a nit) |
| `98c1ca6` | **fix(core):** the wrapper owns what it holds, and never calls a moved-from policy (M2, M3) |
| `69686e5` | **refactor(injector):** the two releases happen where they happened (m1) |
| `87f9aed` | **build:** the control and the features headers come with the core include path (m2, and the module's test) |
| `e4eaf09` | **tool(scripts):** a compile error is not a finding of a check group (m5) |
| `5690e37` | **docs(style):** the sweep's check mode and the gate are documented (m3) |
| `2f7e775` | **fix(control):** a null closer is loud, not a silent leak (a nit) |

### 9.3 What the review round did **not** change

The refactor is untouched: no call site changed its behaviour, the four
contended files are still untouched, and `payload/aim/` is still untouched.
The two changes that reach into the C++ are the wrapper's own ownership
rules (in the module this pass added) and two inner blocks in the injector
that put the releases back where they were. The rest is the CI and the
tooling — which is the deliverable the specification asked for.

### 9.4 The re-review

The pass was reviewed a second time, on a **fresh checkout** (`git archive
HEAD` — no `external/Vulkan-Headers`, no calibration dumps, no generated SDK
headers, no prebuilt dumper). The reviewer ran the whole workflow end to end
there: every step passed, the old failure was reproduced (`exit 1`,
`ERROR: Include dir ../external/Vulkan-Headers/include does not exist`) and
the fix was reproduced (clone → configure → link `cs2_overlay_layer.dll`).
The verdict was **ship with fixes** again, this time with no blocker: the
findings were the wrapper's remaining contract hole, and the workflow's
undeclared dependencies. After the fixes, the whole workflow was re-run on a
fresh checkout of the final tree — every step green (§7.2).

| finding | what it was | what changed | how it was proven |
|---|---|---|---|
| **F2** | the ownership flag closed the destructor path, but `reset()` on a **moved-from** wrapper still reached `adopt()` → `policy_.is_valid()` — and a moved-from policy (a `std::function` closer) is empty: `std::bad_function_call` out of a `noexcept` function, `std::terminate` | a move now **copies** the policy, so there is never a moved-from policy for any path to call, and a moved-from wrapper stays a usable wrapper | the host test resets a moved-from wrapper; with the move put back (the policy moved), the test ends the process with `std::bad_function_call` — the reviewer's crash, reproduced |
| **F1** | the report claimed "the local build was reconfigured the same way", but the build directory that was handed over had no `payload.dll` in it: the claim was not backed by an artifact | the report gives the **command** and the `dumpbin` result, and names the `build-payload/` directory of the branch worktree that carries the artifact (`b_vscrt = mt`, `payload.dll`) | `dumpbin /dependents build-payload\payload.dll` lists only `KERNEL32.dll`, `USER32.dll`, `D3DCOMPILER_47.dll` |
| **F3** | the build needs a **Rust** toolchain (the SDK headers are generated by the Rust dumper) that the workflow never mentioned, and the calibration dumps are git-ignored, so the payload is built with an **empty** SDK | the workflow checks for `cargo` and fails loudly; the workflow's header and §7 say what the payload step actually builds | the check was run with and without cargo: without it the step fails with the reason |
| **F4** | the failures are collected per group, so one broken file that failed in both gated groups was reported as *two* translation units | the failed list is de-duplicated (`distinct_failures`), and a file's output is shown once, with the group that found it | a broken `payload/game/src/log.cpp`: the gate says "failed on 1 translation unit(s)" (it said 2) and exits 1 |
| **F5** | the compiler is the runner image's LLVM and is **not** pinned; the workflow header claimed the configuration this repository is verified with locally | the header says the compiler is the image's, states the exposure (the image ships 20.1.8, the tree was verified with 22.1.8; the same `-std=c++26`, no `__cplusplus` conditionals) and records the image facts the workflow relies on | — |
| **F6** | `meson` and `ninja` were installed unpinned in the same step that installs the pinned `requirements-dev.txt` | both are pinned in `requirements-dev.txt` (meson 1.11.2, ninja 1.13.0) and installed from it | — |
| **F7** | §6.1's numbers did not follow from the method printed next to them (35 → 14 tokens and 25 → 5 raw locals are what the documented regexes give) | §6.1 publishes the exact commands and the numbers they produce | the commands were run verbatim in git bash: 35, 14, 25, 5 |

### 9.5 The re-review's commits

| commit | what it changed |
|---|---|
| `479bd09` | **fix(core):** a move copies the policy, so there is no moved-from policy (F2) |
| `b1d89fe` | **ci:** the Rust toolchain is declared, and the payload is built with an empty SDK (F3) |
| `7926e1f` | **tool(scripts):** a translation unit that failed is counted once (F4) |
| `32f9630` | **ci:** the build system is pinned like the rest (F6) |
| `bf129e1` | **ci:** the compiler is the runner image's, and the header says so (F5) |
| `ab32d9c` | **docs(quality-pass):** the handle counts are reproducible (F7) |
| `0763061` | **docs(quality-pass):** the M1 evidence is the command and the artifact (F1) |

---

## 10. Where to look next

- `docs/style-guide.md` §7 — how the sweep is driven, and the exclusions
  that are deliberate.
- `payload/core/include/cs2/core/handle.hpp` — the wrapper, with the policy
  protocol in its comment.
- `payload/core/tests/test_handle.cpp` — the ownership rules, proven.
- `scripts/clang-tidy-fix.py --help` — the check mode and the gate.
- `scripts/scenario/tests/test_clang_tidy_fix.py` — the gate's decision, proven.
- `.github/workflows/ci.yml` — the verify loop, and what it does not cover.
