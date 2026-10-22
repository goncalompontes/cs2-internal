# S6 — the typing gate and the test wiring

**Goal:** make "fully type safe" **enforceable** rather than aspirational, and
get the Python suite under the same roof as the C++ one.

**File ownership (you may ONLY touch these):**

- `pyproject.toml` *(new)*
- `requirements-dev.txt` *(new, optional)*
- `scripts/typecheck.py` *(new)*
- `scripts/scenario/tests/**`
- the root `meson.build` (the test wiring only)
- `docs/control-harness.md` (a short "the Python tooling" section)

**Do NOT touch** `scripts/scenario/protocol.py`, `control.py`, `screens.py`,
`awaiting.py`, `session.py`, `settings.py`, `heatmap.py` or the scenarios —
unless a test proves they are **wrong**, in which case fix the bug and say so.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`spec-s3.md` (the types) and `spec-s4.md` (the state machine).

---

## 1. The static type checker

**`mypy` is installable on this machine** (the wheel downloads; the network is
available). The environment is **Python 3.11.9**.

- Add a `pyproject.toml` with a `[tool.mypy]` section. The repo has **no**
  Python packaging config at all today, so this is the first — keep it small and
  honest: `requires-python`, the package to check, the strictness, and the
  exclusions.
- **The strictness is a decision you must justify.** The existing code is
  thoroughly annotated but leans on `dict[str, Any]` for the wire payloads, and
  S3 replaces most of that with `TypedDict`s. Try `strict = true` over
  `scripts/scenario/` and **measure** what it reports. If it is clean, take it.
  If it is not, take the strictest setting that is green **and** enable the
  checks that matter for this codebase — at minimum:
  `disallow_untyped_defs`, `disallow_incomplete_defs`, `check_untyped_defs`,
  `warn_return_any`, `warn_unused_ignores`, `no_implicit_optional`,
  `strict_equality`.
- **Do not silence errors with `# type: ignore`** to get green. If mypy
  reports a real problem in the existing code, **fix the code** (or report it
  if the fix is out of your ownership).
- Add a `requirements-dev.txt` with the pinned dev dependency, and a
  `scripts/typecheck.py` that runs the checker over the package and **exits
  non-zero on an error** — a gate, not a report.
- `pyright` is an alternative (the editor's LSP already uses it) but it is not
  installed and needs node; **mypy is the gate**.

## 2. The Python tests under `meson test`

The Python suite (`scripts/scenario/tests/`) is **not** part of `meson test`
today — it is run by hand only. Wire it in:

```meson
python = find_program('python')
scenario_tests = python '-m' 'unittest' 'discover' '-s' 'scripts/scenario/tests' '-t' 'scripts'
test('scenario-screens', scenario_tests)
```

(one `test()` per test module is nicer than one big one — the C++ side registers
each suite separately, so match that: `scenario-screens`, `scenario-protocol`,
`scenario-mcp`, `scenario-state-machine`).

- The test must be **skipped, not failed**, when the environment cannot run it
  (a missing `python`, a missing package). Meson's `suite:`/`protocol:` and a
  wrapper that returns 77 (the automake "skip" code) or Meson's own skip
  mechanism are both acceptable — pick one and use it consistently.
- `meson test -C build` must stay green, and the C++ suite must not slow down
  materially (the Python suite is fast — it is pure logic, no game).

## 3. The missing test coverage

The review of the scenario library found that most of it is **untested**. The
C++ side has 21 host tests; the Python side has **one** file. Close the gaps,
in this order of value:

1. **The transport** (`control.py`) — **zero** tests today. Write a **fake
   pipe** (a small object with the same `read`/`write`/`close` surface the
   `Control` class uses) and test:
   - the request envelope (`{"id", "cmd", "args"}`);
   - the response matching (a stale `id` is skipped, a non-JSON line is
     skipped, `ok: false` raises `ControlError` with the server's message);
   - the framing (a response split across two reads, a response with a `\r\n`);
   - the error paths (the pipe closes mid-request).
2. **The `wait_for` predicate** — the client's side of the DSL. The server's
   side is tested in C++ (`control-core`); the Python builder is not tested.
3. **`awaiting.py`** — `is_movable`, `recenter`, `settle`, `prepare`,
   `pick_clear_heading`, `measure_move`, `add_bots`, `ensure_bot_match`: all
   untested. The pure helpers (the ones that take a snapshot and return a
   value) are testable with a synthetic snapshot.
4. **`session.py`** — the tool paths and the process-manifest parsing.
5. **`settings.py`** — the shared-memory settings reader.
6. **`heatmap.py`** — the PNG rasteriser: a small image, assert the pixel
   values.
7. **The `SCENARIOS` registry** — assert that every registered name maps to a
   callable with the `run(game, **kwargs)` signature, and that **every** module
   in `scenarios/` is registered (a scenario file that is not in the registry
   is dead code).
8. **The `GameState` accessors** — `valid`, `in_game`, `in_real_map`,
   `buy_time`, `round_over`, `level`, `rules`, `local`, `team`, `alive`,
   `life_state`, `on_ground`, `origin`, `players`, `summary()`, `as_dict()` —
   none is asserted today.
9. **The `CAPABILITIES` table's *content*** — the existing test only proves
   `can()` agrees with the table (a wrong-but-consistent table passes). Assert
   the content: that `LIVE` has `MOVE`, that `DEAD` has only `STATE`, and so
   on for every screen.

**Rules for the tests:**
- `unittest`, the repo's convention (`scripts/scenario/tests/test_screens.py`).
- `from __future__ import annotations`; `snake_case`; the same docstring style.
- A test must be able to **fail**: write it, then break the thing it tests on
  purpose, watch it fail, and revert. Report that you did, per test group.
- **No test may need the game.** They are host tests; the live behaviour is the
  `scenarios/`'s job.

## 4. The quality bar

- Python 3.11 floor; **stdlib only** for the package and the tests (mypy is a
  *dev* dependency, not a runtime one).
- The `pyproject.toml` must be a real, valid file (parse it and assert that in
  a test).
- `meson test -C build` must show the Python suites in its output, and
  `meson test -C build` must stay green.

## 5. Verification

- `meson compile -C build && meson test -C build` → green, with the new Python
  suites listed.
- `python -m mypy --config-file pyproject.toml scripts/scenario` → clean (or the
  exact residual list with the reasons).
- `python scripts/typecheck.py` → exit 0.
- `python -m unittest discover -s scripts/scenario/tests -t scripts` → green.

## 6. Report

- The mypy configuration, **the strictness you chose and why**, and the exact
  number of errors it reports before and after your work.
- The meson wiring and how a missing interpreter is handled.
- The coverage you added, per area, with the before/after test counts.
- The problems you found in the existing code that the new tests exposed, and
  what you did about each.
- Anything you could not cover, and why.
