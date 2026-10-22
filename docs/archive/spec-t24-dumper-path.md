# T24 - the SDK generation finds the dumper wherever cargo put it

## The problem

`scripts/gen-sdk.py` looks for the dumper binary at a hard-coded path:

```python
binary = root / "external" / "dumper" / "target" / "release" / exe
```

Cargo does not always put it there. `CARGO_TARGET_DIR` moves the whole
target directory - which is exactly how a task worktree keeps its cargo
output out of itself, and out of the 42 worktrees this project used to have
(the orchestrator's plugin sets it to a directory in the shared build root) -
and then the dumper is at `<CARGO_TARGET_DIR>/release/cs2-dumper.exe`, where
the script does not look for it.

The symptom is a **fresh worktree that does not build**: the script says

```
gen-sdk: building cs2-dumper (release)
```

runs cargo, and then runs a binary that is not where cargo put it - or, when
the cargo build did not run, the generation writes **empty** SDK headers and
the payload builds against nothing. A green build against an empty SDK is
worse than a red one.

It was found on 2026-10-21, in the T23 worktree, where the build failed on
the first run and the fix was to copy the binary by hand - a copy in a
gitignored directory, which is not a fix.

## What to do

Make the dumper path follow `CARGO_TARGET_DIR`:

- when `CARGO_TARGET_DIR` is set, the binary is at
  `<CARGO_TARGET_DIR>/release/<exe>`;
- when it is not set, the path is what it is today,
  `external/dumper/target/release/<exe>`.

Resolve the path in a **function**, so it can be tested without a build, and
keep the rest of the behaviour identical - the script still builds the
dumper on demand when the binary is not there.

## The test

The generator's own unit test, next to the settings-table one
(`scripts/scenario/tests/test_settings_table.py`), checks the same thing:
that the dumper path follows `CARGO_TARGET_DIR` when it is set, and does
not when it is not. It must be registered so that it really runs - the
Python suite discovers `scripts/scenario/tests/test_*.py`, and the meson
wiring for the script tests is in the root `meson.build` (the same place
the settings-table test is listed).

## File ownership

- `scripts/gen-sdk.py`
- the new test file under `scripts/scenario/tests/`
- the root `meson.build`, if the new test has to be listed there

Nothing else. Do not touch the payload, the overlay or the injector.

## Verify

The project verify loop: `meson compile -C build`, `meson test -C build`,
the payload, the overlay, the injector, the Python suite and the type gate.

Prove the new test can fail: break the resolution on purpose, watch it fail,
revert.

## Report

What changed, per file. The test you added and how you proved it can fail.
What you could not verify.
