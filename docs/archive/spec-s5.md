# S5 — the OpenCode integration: the scenario API as MCP tools

**Goal:** make the scenario/control API callable **inline from an agent**, the
way the REA tools are: a local MCP server whose tools appear in the Code Mode
`execute` runtime as `tools.cs2.*`.

**File ownership (you may ONLY touch these):**

- `scripts/scenario/mcp/__init__.py` *(new)*
- `scripts/scenario/mcp/server.py` *(new)* — the JSON-RPC/stdio server
- `scripts/scenario/mcp/tools.py` *(new)* — the tool definitions
- `scripts/scenario/mcp/schema.py` *(new)* — the TypedDict → JSON Schema bridge
- `scripts/scenario/mcp/session.py` *(new)* — the lazy pipe/session management
- `scripts/scenario/tests/test_mcp.py` *(new)*
- `scripts/scenario/__init__.py` (re-exports only)
- `docs/control-harness.md` (a new section: "The OpenCode / MCP integration")

**Do NOT touch** `protocol.py`, `control.py`, `screens.py`, `awaiting.py` or
`session.py` — parallel tasks (S3, S4) own those; **consume** their types.

**The global OpenCode config is NOT in the repo.** Register the server by
editing `C:\Users\gpmpo\.config\opencode\opencode.jsonc` — that is a **global**
file, and it is yours to change for this task (there is exactly one `mcp`
entry, `rea`; add a second, `cs2`). **Back it up first** and show the
resulting `mcp` block in your report. The MCP server hot-reloads — you do **not**
need to restart OpenCode for the change to take effect, and you must **not**
run `opencode service restart` (it kills the agent session).

Read `C:\Users\gpmpo\AppData\Local\Temp\opencode\feature-brief.md` first, then
`spec-s3.md` (the types) and `spec-s4.md` (the state machine).

---

## 1. What you are building

A **local MCP server** over stdio that exposes the scenario library as tools.

- The MCP stdio transport is **JSON-RPC 2.0**, one JSON object per line on
  stdin/stdout. The methods you must implement: `initialize`, `tools/list`,
  `tools/call`, and `notifications/initialized` (a notification — no response).
- **Implement it with the standard library only.** The `mcp` Python package
  (v2.0.0) happens to be installed on this machine, but do **not** depend on
  it: the server must run on a bare Python 3.11. (You may *use* the installed
  `mcp` package **in the test** to prove your server is protocol-correct — that
  is a test-time convenience, not a runtime dependency. If the package is
  absent, the test must skip rather than fail.)
- stdout is the protocol channel: **nothing else may ever be written to
  stdout**. Diagnostics go to stderr.

## 2. The tools

Each tool is a thin wrapper over the typed API. **One tool per API area**, so
the catalog stays readable — do not expose one tool per command.

| Tool | Wraps | Notes |
|---|---|---|
| `cs2_ping` | `Control.ping` | liveness + the protocol version + the payload's readiness flags |
| `cs2_state` | `Control.state` | the typed snapshot |
| `cs2_screen` | `Machine.observe` / `GameState` | the deduced screen, the capabilities, **and the reason** for each refusal |
| `cs2_ensure` | `Machine.ensure` | drive the game to a target screen (the state machine's action) |
| `cs2_exec` | `Control.exec` | a console line |
| `cs2_look` | `Control.look` | the view angles |
| `cs2_move` | `Control.move` | the analog move axes |
| `cs2_press` / `cs2_release` | `Control.press` / `release` | the buttons |
| `cs2_aim` | `Control.aim` | the ragebot solve |
| `cs2_rage` | `Control.rage` | the hold-key ragebot |
| `cs2_events` | `Control.events` | the captured game events (with the `since` cursor if S1 landed) |
| `cs2_wait_for` | `Control.wait_for` | the structured predicate wait |
| `cs2_settings_get` / `cs2_settings_set` | the new `settings` command (S1) | the feature settings block — the ESP / third person / chams / movement switches |
| `cs2_run_scenario` | the scenario registry | run one of the scenarios by name |
| `cs2_list_scenarios` | the scenario registry | list them |
| `cs2_launch` / `cs2_inject` | `session.py`'s launch/inject | **destructive**: they start the game / inject the payload |

If S1's `settings` command has not landed when you build, implement
`cs2_settings_get`/`cs2_settings_set` against the **wire block** through
whatever route is available and say so in your report — do not block on it.

### 2.1 Every tool's input schema must be **generated**

This is the point of the exercise: the type safety must reach the **tool
boundary**, so an agent gets a correct schema and a bad argument is rejected
before it reaches the game.

`schema.py` converts a `TypedDict` (plus optional `Literal`s) into a JSON
Schema:

- `TypedDict` → `{"type": "object", "properties": {...}, "required": [...],
  "additionalProperties": false}` — `required` from `total=False` keys.
- `Literal["a", "b"]` → `{"enum": ["a", "b"]}`.
- `int` / `float` / `str` / `bool` → the obvious JSON type.
- `list[X]` → `{"type": "array", "items": <X>}`.
- `X | None` → `{"anyOf": [<X>, {"type": "null"}]}`.
- A nested `TypedDict` → a nested object schema.
- Anything you cannot express → **fall back to `{}` (any)** and **say so** in
  your report — do not silently emit a wrong schema.

Use `typing.get_type_hints(..., include_extras=True)` and resolve the forward
references (the package uses `from __future__ import annotations`, so the
hints are strings).

### 2.2 The tool result

MCP's `tools/call` returns `{"content": [{"type": "text", "text": "..."}],
"isError": bool}`. For this API the useful payload is **JSON**, so:

- the tool's result is `json.dumps(result, indent=2, sort_keys=True)`.
- an **error** is `isError: true` with the message in the text — a `ControlError`
  becomes a tool error, not a crash.
- **Never** return a traceback to the agent as the tool result; return a
  precise, actionable message ("the control channel is not connected: is CS2
  running? launch it with `schtasks /run /tn CS2Launch` and inject
  `build-payload/payload.dll`").

## 3. The session management (`mcp/session.py`)

The MCP server is a **long-lived process**; the game is not. So:

- The pipe connection is **lazy**: the first tool that needs the game connects
  (with a bounded timeout), and the connection is **cached**.
- If the game is not running, the tool returns a **clear, actionable error** —
  not a hang and not a stack trace.
- A **dropped** pipe (the game closed) must be detected and the next call
  **reconnect** (or fail cleanly). The `Control` class has no reconnect today —
  implement the retry in the MCP layer (re-create the `Control` and connect).
- `cs2_launch` and `cs2_inject` are the **only** tools that start or change
  the game. They must be described as such in the tool description, and the
  launch must go through the **`CS2Launch` scheduled task** (the agent runs in
  session 0 with no GPU; running `cs2.exe` directly would fail). Never call
  `schtasks` or `taskkill` without the user asking.

**The MCP server must never kill the game on exit.** A client disconnecting
must leave the game running.

## 4. The state-machine tools

`cs2_screen` and `cs2_ensure` use the state machine (S4). `cs2_ensure`'s
`target` argument's schema must be **generated from the `Screen` enum** — an
`enum` of the kebab-case values, so an agent cannot pass a typo.

`cs2_screen` should return the screen, the capabilities, and for each refused
capability **the reason** (`GameState.why(capability)`) — that is what makes
the tool useful to an agent: it learns *why* it may not move right now.

## 5. The registration

Add to `C:\Users\gpmpo\.config\opencode\opencode.jsonc`:

```jsonc
"mcp": {
  "rea": { ...unchanged... },
  "cs2": {
    "type": "local",
    "command": ["python", "C:\\Users\\gpmpo\\cs2-internal\\scripts\\scenario\\mcp\\server.py"],
    "enabled": true,
    "environment": { "PYTHONPATH": "C:\\Users\\gpo\\cs2-internal\\scripts" }
  }
}
```

(Adjust the paths to this machine.) **Back the file up first** and keep the
existing `rea` entry **byte-identical** — it is a live, working registration.

The server must be runnable as `python scripts\scenario\mcp\server.py` from
**any** working directory (it must add its own package root to `sys.path`, the
way `scripts/run-scenario.py` does).

## 6. The tests (`scripts/scenario/tests/test_mcp.py`)

`unittest`. Cover:

- **The schema bridge**: a `TypedDict` → the expected JSON Schema; a
  `Literal` → an `enum`; a nested `TypedDict` → a nested object; an optional
  key → not in `required`; `additionalProperties: false`.
- **The server, over a pipe**: start `server.py` as a subprocess, speak MCP to
  it, and check:
  - `initialize` returns the protocol version and the server's capabilities;
  - `tools/list` returns the tools, each with a valid JSON Schema;
  - `tools/call` on a tool whose arguments are wrong returns an error (not a
    crash);
  - a tool that needs the game, with **no game running**, returns the
    actionable "the game is not running" message — **not** a hang and not a
    traceback.
- If the `mcp` package is importable, also drive the server with the **real
  `mcp` client** to prove protocol conformance, and `skip` that test when the
  package is absent.
- The tools' schemas must be **valid JSON Schema** (a quick structural check is
  enough; do not add a dependency).

**Prove the tests can fail**: break a schema mapping on purpose, watch it
fail, revert.

## 7. Quality bar

- `from __future__ import annotations`; `snake_case`; `SCREAMING_SNAKE`
  constants; docstrings that explain *why*.
- **stdlib only** at runtime.
- **Nothing may be printed to stdout** except the protocol messages.
- The server must **not crash** on a malformed request, an unknown method, an
  unknown tool, or a tool that raises — it answers with an error.
- Python 3.11 floor.

## 8. Verification

- `python -m unittest discover -s scripts\scenario\tests -t scripts` → green.
- `python scripts\scenario\mcp\server.py` starts and waits for input on stdin
  (Ctrl-C to stop); feed it a hand-written `initialize` line and check the
  response by eye **once**.
- The registration in the global config is valid JSON-with-comments and the
  `rea` entry is unchanged.
- Commit in logical pieces (`feat(scenario): …`).

## 9. Report

- The tool list with each tool's generated schema (or a pointer to it).
- The schema bridge's rules and its fallbacks.
- The session/connection lifecycle and the error messages.
- The global config change, with the `rea` entry verified unchanged.
- What you could not verify without a live game, and what the tool does when
  the game is absent.
