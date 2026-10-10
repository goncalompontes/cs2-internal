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

## Cross file

`windows-msvc.ini` targets `x86_64-pc-windows-msvc`. `msvcenv-native.sh`
supplies `INCLUDE`/`LIB`; the cross file only selects the compilers and the
target triple.
