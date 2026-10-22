# The protection pass lives in another repository

The LLVM pass plugin `Cs2Protect.dll` — the call encryption, the string
encryption, the cross-TU routing, the per-build layout, the VM front end and
the integrity work — is its own project, checked out as a git submodule:

```
tools/protect/          ->  https://github.com/goncalompontes/cs2-protect
```

It has its own CMake build, its own LLVM SDK, its own test suite and its own
documentation (`tools/protect/README.md`, `tools/protect/docs/handoff.md`, and
`tools/protect/CONTRIBUTING.md`).

## Initialize it

```powershell
git submodule update --init --recursive
```

A clone that does not need the protection pass can skip this; `protect=false`
(the default) does not touch the submodule.

## Build the plugin once

```powershell
python tools\protect\scripts\build.py
# prints the absolute path of tools\protect\build\Cs2Protect.dll
```

The build needs the LLVM 22.1.8 release SDK (see `tools/protect/README.md` §1);
`CS2_LLVM_SDK` overrides the default path.

## How this tree consumes it

The payload build loads the built DLL and points at the submodule's checkers:

```powershell
meson configure build-payload `
  -Dprotect=true

$env:CS2_PROTECT_CONFIG = 'C:\...\cs2-internal\tools\protect\policy.local.json'
$env:CS2_PROTECT_SEED   = '<64-bit hex>'
meson compile -C build-payload
```

`protect_tools` and `protect_plugin` default to `tools/protect` and
`tools/protect/build/Cs2Protect.dll`. Pass them explicitly when the repository
lives elsewhere:

```powershell
meson configure build-payload `
  -Dprotect=true `
  -Dprotect_tools=C:/path/to/cs2-protect `
  -Dprotect_plugin=C:/path/to/cs2-protect/build/Cs2Protect.dll
```

`protect_tools` is where `verify_plugin.py`, `check_policy.py`,
`check_payload.py` and `find_objdump.py` live. The plugin's digest, the
policy's digest and the coverage checks are wired in `payload/meson.build`.

`policy.local.json` is gitignored: copy `tools/protect/policy.example.json`
and set a seed. Do not commit your local policy or seed.

## What is in this repository

Only the payload-side wiring:

- `payload/meson.build` (the options, the plugin load, the policy/seed checks,
  the coverage targets),
- `payload/meson_options.txt` (`protect`, `protect_plugin`, `protect_tools`,
  `protect_reference`).

Protection **documents** (the handoff, the plan, the specs and the research)
are in the submodule's `docs/`. This file is the pointer, so a reader who lands
here knows where to go.
