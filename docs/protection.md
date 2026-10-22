# The protection pass lives in another repository

The LLVM pass plugin `Cs2Protect.dll` — the call encryption, the string
encryption, the cross-TU routing, the per-build layout, the VM front end and
the integrity work — is its own project:

```
C:\Users\gpmpo\cs2-protect
```

It has its own CMake build, its own LLVM SDK and its own test suite, and its
own documentation (`docs/` in that repository, starting with
[`handoff.md`](../../cs2-protect/docs/handoff.md) and the plugin's
`README.md`).

## How this tree consumes it

The payload build loads the **built** DLL and points at the repository's
checkers:

```powershell
meson configure build-payload `
  -Dprotect=true `
  -Dprotect_tools=C:/Users/gpmpo/cs2-protect `
  -Dprotect_plugin=C:/Users/gpmpo/cs2-protect/build/Cs2Protect.dll
$env:CS2_PROTECT_CONFIG = 'C:\Users\gpmpo\cs2-protect\policy.local.json'
$env:CS2_PROTECT_SEED   = '<64-bit hex>'
meson compile -C build-payload
```

`-Dprotect_tools` is required when `-Dprotect=true`: it is where
`verify_plugin.py`, `check_policy.py`, `check_payload.py` and
`find_objdump.py` live. The plugin's digest, the policy's digest and the
coverage checks are wired in `payload/meson.build`, unchanged in behaviour.

## What is in this repository

Only the payload-side wiring:

- `payload/meson.build` (the options, the plugin load, the policy/seed checks,
  the coverage targets),
- `payload/meson_options.txt` (`protect`, `protect_plugin`, `protect_tools`,
  `protect_reference`).

Protection **documents** (the handoff, the plan, the specs and the research)
are in the other repository. This file is the pointer, so a reader who lands
here knows where to go.
