# T15 — clear the residual mypy errors (the gate must not need overrides)

**Goal:** the typing gate S6 landed runs mypy at `strict = true`, but it carries
**eight `[[tool.mypy.overrides]]` entries** that disable a check for the modules
other tasks owned. That is a weakened gate. **Clear the residuals and delete the
overrides** — then the gate is genuinely strict over the whole package.

**File ownership (you may ONLY touch these):**

- `scripts/scenario/screens.py`
- `scripts/scenario/session.py`
- `scripts/scenario/awaiting.py`
- `scripts/scenario/heatmap.py`
- `scripts/scenario/protocol.py`
- `scripts/scenario/mcp/schema.py`, `mcp/server.py`, `mcp/tools.py`
- `scripts/scenario/scenarios/*.py`
- `pyproject.toml` (to delete the overrides)
- `scripts/scenario/tests/**` (if a test needs adjusting for a signature change)

**Do NOT touch** `scripts/scenario/control.py` (it is clean), the root
`meson.build`, the C++ tree, or the wire format.

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`pyproject.toml` (the overrides and the residual breakdown), then
`docs/research/neverlose-mining.md`… no — read `pyproject.toml` and the modules.

---

## 1. The residuals, by module

S6 measured **81** errors in the modules it did not own, with this breakdown
(`mypy --strict scripts/scenario`):

| module | n | codes |
|---|---|---|
| `screens.py` | 27 | `call-overload` ×15, `arg-type` ×6, `return-value` ×3, `var-annotated` ×2, `no-any-return` |
| `session.py` | 8 | `arg-type` ×4, `return-value` ×2, `no-any-return` |
| `awaiting.py` | 6 | `arg-type`, `assignment`, `index`, `return-value`, `var-annotated` |
| `heatmap.py` | 5 | `assignment` ×3, `index` ×2 |
| `scenarios/*` | 30 | `arg-type` ×20, `assignment` ×3, `index` ×4, `call-overload` ×2, `no-any-return` ×2 |
| `protocol.py` | 2 | `attr-defined`, `arg-type` |
| `mcp/{schema,server,tools}.py` | 3 | `arg-type`, `attr-defined`, `operator` |

Run `python -m mypy --strict scripts/scenario` (or `python scripts/typecheck.py`)
in your worktree to get the **current** list — it may differ slightly from the
above.

## 2. What each class of error means, and how to fix it

**Do not silence them.** No `# type: ignore`, no `cast(Any, x)` to dodge a
mismatch, no widening a `TypedDict` to `dict[str, Any]`. Fix the code.

- **`call-overload` (15 in `screens.py`)** — most likely the `Screen`/`Can`
  constructors being called with a `str` where an enum is wanted, or a
  `@overload`ed function being called with the wrong argument type. The
  `Screen(str, enum.Enum)` mixin means `Screen("live")` is legal, but
  `Screen(some_str)` where `some_str: str` is not a valid value raises
  `ValueError` at runtime — so the fix is usually to **validate the string and
  raise a clear error**, and to type the parameter as `Screen | str` with a
  conversion, or to take `Screen` directly.
- **`arg-type` (20 in `scenarios/*`)** — the scenarios pass `dict[str, Any]`
  blobs into the now-typed API. The fix is to build the typed arguments
  (`StateSnapshotDict`, `PlayerSnapshotDict`, …) instead of raw dicts, or to
  annotate the local variables.
- **`screens.py`'s defensive `raw.get(...) or {}` pattern (26 of the errors)** —
  a `TypedDict` cannot express "this key may be missing, and the default is an
  empty dict". The fix is to make the **snapshot's shape honest**: the payload
  always sends `level` and `rules` (see `snapshot_json.cpp`), so the accessors
  should read them directly with the typed `TypedDict` — or the accessors should
  take a `LevelSnapshotDict` / `RulesSnapshotDict` argument rather than
  re-deriving it from the raw dict.
- **`no-any-return` (5)** — a function annotated to return a concrete type but
  returning an `Any` (usually a `dict[str, Any]` element). Annotate the
  intermediate.
- **`var-annotated` (2)** — a module-level variable whose type is not inferable
  (usually an empty container). Annotate it: `entries: dict[str, int] = {}`.
- **`index` (4 in `scenarios/*`, 2 in `heatmap.py`)** — indexing a `list` with
  a value a checker cannot prove is in range. Use `.get()` with a default, or
  `at()`-style access, or a length check.
- **`assignment` (3+3)** — assigning a value of one type to a variable of
  another. `heatmap.py` reuses `a`/`b`/`base` for different shapes inside
  `render` (a float in one place, a tuple in another) — **give them separate
  names**; that is also a readability fix.
- **`attr-defined` (2 in `protocol.py`)** — `shape.__required_keys__` is not on
  `type`. Use `getattr(shape, "__required_keys__", frozenset())` with a proper
  annotation, or a `cast` that is *honest* about the runtime attribute.
- **`operator` (1 in `mcp/`)** — a comparison mypy cannot type. Make the
  operands the same type.
- **`session.py`'s mis-annotations (4)** — `Game.players()` is annotated
  `list[dict[str, Any]]` but returns `list[PlayerSnapshotDict]`;
  `Game.local()` likewise. **Fix the annotation**, not the code.
- **`session.py`'s `via: str | None` (4)** — the typed control wants
  `InputBackend | None`. Fix the annotation.
- **`awaiting.round_state()` builds `list[str | None]`** — an event's name may be
  absent. That is honest; type it as such and make the consumer handle it.

## 3. The end state

- `python scripts/typecheck.py` exits **0** with **no overrides** in
  `pyproject.toml` — delete all eight `[[tool.mypy.overrides]]` entries.
- `mypy --strict scripts/scenario` reports **0** errors.
- `python -m unittest discover -s scripts\scenario\tests -t scripts` → green
  (**342** at baseline; it may grow if you add tests).
- `meson compile -C build && meson test -C build` → green (**39** at baseline).

**If a residual genuinely cannot be fixed without changing the wire format or
the payload's behaviour, say so in your report with the reason** — do not paper
over it with an override. A single justified exception is acceptable; eight
blanket ones are not.

## 4. Quality bar

- The package stays **stdlib only** at runtime.
- Python 3.11; `from __future__ import annotations`; the repo's naming and
  docstring conventions.
- **Do not change any runtime behaviour.** This is a typing task: the 342 tests
  must still pass **unchanged** (except where a *signature* changes, in which
  case update the test and say so).
- Commit **only your own files** (`chore(scenario): …`).

## 5. Report

- The **before/after** mypy error count, and the per-module breakdown after.
- Every change you made, per module, and **what each fix changed about the
  code** (an annotation, a renamed variable, a restructured accessor, a new
  validation) — a reviewer must be able to see that nothing was silenced.
- The overrides you deleted, and any you had to keep (with the reason).
- Anything you could not fix, and why.