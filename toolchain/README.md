# Toolchain

## msvc-wine (MSVC ABI without Windows)

The payload must use the **MSVC ABI** to interoperate with the target's C++
classes and vtables. We get MSVC headers/libs on Linux via
[`mstorsjo/msvc-wine`](https://github.com/mstorsjo/msvc-wine), and compile with
**clang-cl + lld** using Meson (no Wine needed at build time).

Run [`setup-msvc-wine.sh`](setup-msvc-wine.sh) (downloads MSVC + WinSDK into
`toolchain/msvc/`, which is gitignored — it is not redistributable).

Then build the payload:

```sh
BIN=toolchain/msvc/bin/x64 . toolchain/msvc/msvcenv-native.sh
meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
meson compile -C build-payload
```

## Host tools

```sh
meson setup build
meson compile -C build
meson test -C build
```

## Native Windows (clang-cl)

The same Meson projects build natively on Windows. The C++23 dialect is selected
per compiler in each `meson.build` (GNU-like `-std=c++23`; MSVC/clang-cl
`/std:c++latest`, which is what Meson exposes for those drivers), and SDK header
generation uses `scripts/gen-sdk.py` (cross-platform Python) so no POSIX shell
is required.

From a VS Developer (or `vcvars64.bat`) environment with LLVM on `PATH`:

```bat
set CC=clang-cl
set CXX=clang-cl
meson setup build
meson compile -C build
meson test -C build

:: payload + injector (native Windows targets)
meson setup build-payload payload
meson compile -C build-payload
meson setup build-injector injector
meson compile -C build-injector
```

Notes:

- The Rust dumper builds on Windows and now has a native live-memory backend
  (`ReadProcessMemory` + `VirtualQueryEx` + toolhelp), so `--process`/`--pid`
  work there too; only the `--mcp` server is Unix-only (`/proc`).
- The injector's synthetic PEB loader-list entry is skipped by default on native
  Windows (it faults `LdrpInitializeThread`); it is kept for Wine/Proton. See
  `--register` / `--no-register`.
- The `overlay/` project builds a native Windows `cs2_overlay_layer.dll` as well
  as the Linux `.so`; register the generated manifest under
  `HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers` (see `overlay/README.md`).
- Windows Defender quarantines `injector.exe` after it runs (a manual-map
  injector trips the heuristic). Add a folder exclusion if you need to re-run
  it; this is environmental, not a build issue.

## Cross file

`windows-msvc.ini` targets `x86_64-pc-windows-msvc`. `msvcenv-native.sh`
supplies `INCLUDE`/`LIB`; the cross file only selects the compilers and the
target triple.
