#!/usr/bin/env python3
"""Build the LLVM pass plugin (Cs2Protect.dll).

The protection pass is an LLVM pass plugin: a DLL that the payload's
clang-cl loads with `-fpass-plugin=`. It is built against the LLVM *release*
SDK, because `C:\\Program Files\\LLVM` is a clangd-only install without the
headers, the libraries or `opt.exe`.

The script is idempotent (a second run reuses the CMake build directory) and
needs nothing from the payload's build environment.

Usage: build-protect-plugin.py [--sdk DIR] [--build-dir DIR] [--clean]

  --sdk       the LLVM SDK directory (default: $CS2_LLVM_SDK, else the
              extracted release tarball under F:\\llvm-sdk)
  --build-dir the CMake build directory (default: tools/protect/build)
  --clean     remove the build directory before configuring

The path of the built DLL is printed on stdout; everything else goes to
stderr, so a caller can capture the path with

    python scripts/build-protect-plugin.py
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
PLUGIN_SOURCE = REPO_ROOT / "tools" / "protect"

# The extracted LLVM release tarball. `C:\Program Files\LLVM` is a
# clangd-only install: no headers, no LLVMConfig.cmake, no opt.exe.
DEFAULT_SDK = Path(r"F:\llvm-sdk\clang+llvm-22.1.8-x86_64-pc-windows-msvc")
SDK_URL = (
    "https://github.com/llvm/llvm-project/releases/download/llvmorg-22.1.8/"
    "clang%2Bllvm-22.1.8-x86_64-pc-windows-msvc.tar.xz"
)
SDK_SHA256 = "d96c2cc1736f4eb7fa43cb9bbdf56d93551a9ae0a9aadb9c99c3c3b2b712a234"

# The CMake that ships with Visual Studio; it is not on PATH.
DEFAULT_CMAKE = Path(
    r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE"
    r"\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
)
VSWHERE = Path(r"C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe")


def fail(message: str) -> int:
    print(f"build-protect-plugin: {message}", file=sys.stderr)
    return 1


def find_sdk(explicit: Path | None) -> Path | None:
    """The LLVM SDK directory, or None when it is missing."""
    if explicit is not None:
        return explicit if explicit.is_dir() else None
    env = os.environ.get("CS2_LLVM_SDK")
    if env:
        candidate = Path(env)
        return candidate if candidate.is_dir() else None
    return DEFAULT_SDK if DEFAULT_SDK.is_dir() else None


def find_cmake() -> Path | None:
    """The CMake to configure with."""
    env = os.environ.get("CS2_CMAKE")
    if env and Path(env).is_file():
        return Path(env)
    found = shutil.which("cmake")
    if found:
        return Path(found)
    if DEFAULT_CMAKE.is_file():
        return DEFAULT_CMAKE
    return None


def find_clang_cl() -> str:
    """The compiler that will load the plugin.

    It must be the *same* compiler that compiles the payload: the plugin and
    the compiler share the CRT and the ABI.
    """
    env = os.environ.get("CS2_CLANG_CL")
    if env and Path(env).is_file():
        return env
    found = shutil.which("clang-cl")
    if found:
        return "clang-cl"
    return "clang-cl"


def find_vcvars() -> Path | None:
    """The vcvars script that sets up the MSVC toolchain."""
    if VSWHERE.is_file():
        try:
            output = subprocess.run(
                [str(VSWHERE), "-latest", "-property", "installationPath"],
                capture_output=True,
                text=True,
                check=True,
            ).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            output = ""
        if output:
            script = Path(output) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
            if script.is_file():
                return script
    known = Path(
        r"C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat"
    )
    return known if known.is_file() else None


def msvc_environment_ready() -> bool:
    """Whether the shell already has the MSVC toolchain."""
    return bool(os.environ.get("INCLUDE")) and bool(os.environ.get("LIB"))


def run(command: list[str], use_vcvars: bool) -> int:
    """Runs a command, under vcvars64 when the shell has no MSVC toolchain.

    The plugin links the static CRT (`/MT`), so it needs the MSVC import and
    resource libraries; the link step needs `link.exe` as well.

    The command goes through a temporary batch file: `cmd.exe` does not
    understand the quoting that Python uses for an argument that contains
    quotes of its own.
    """
    if not use_vcvars:
        return subprocess.run(command, check=False).returncode

    vcvars = find_vcvars()
    if vcvars is None:
        print(
            "build-protect-plugin: no vcvars64.bat found; run the script from a "
            "Visual Studio developer prompt",
            file=sys.stderr,
        )
        return 1

    with tempfile.NamedTemporaryFile(
        "w", suffix=".bat", delete=False, encoding="utf-8", newline="\r\n"
    ) as batch:
        batch.write("@echo off\n")
        batch.write(f'call "{vcvars}" >nul 2>&1\n')
        batch.write(subprocess.list2cmdline(command) + "\n")
        batch.write("exit /b %ERRORLEVEL%\n")
        script = Path(batch.name)
    try:
        return subprocess.run(["cmd", "/c", str(script)], check=False).returncode
    finally:
        script.unlink(missing_ok=True)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--sdk", type=Path, default=None)
    parser.add_argument("--build-dir", type=Path, default=PLUGIN_SOURCE / "build")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args(argv)

    sdk = find_sdk(args.sdk)
    if sdk is None:
        wanted = args.sdk or Path(os.environ.get("CS2_LLVM_SDK", DEFAULT_SDK))
        print(
            "build-protect-plugin: the LLVM SDK was not found.\n"
            f"             looked for: {wanted}\n"
            "             download the release tarball and extract it:\n"
            f"               {SDK_URL}\n"
            f"               sha256: {SDK_SHA256}\n"
            "             then point $CS2_LLVM_SDK at the extracted directory.",
            file=sys.stderr,
        )
        return 1

    llvm_dir = sdk / "lib" / "cmake" / "llvm"
    if not (llvm_dir / "LLVMConfig.cmake").is_file():
        return fail(f"the SDK at '{sdk}' has no lib/cmake/llvm/LLVMConfig.cmake")

    cmake = find_cmake()
    if cmake is None:
        return fail(
            "no cmake found; set $CS2_CMAKE or install CMake "
            f"(the Visual Studio one is at '{DEFAULT_CMAKE}')"
        )

    build_dir = args.build_dir
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)

    use_vcvars = not msvc_environment_ready()
    configure = [
        str(cmake),
        "-S",
        str(PLUGIN_SOURCE),
        "-B",
        str(build_dir),
        "-G",
        "Ninja",
        "-DCMAKE_BUILD_TYPE=Release",
        f"-DCMAKE_C_COMPILER={find_clang_cl()}",
        f"-DCMAKE_CXX_COMPILER={find_clang_cl()}",
        f"-DLLVM_DIR={llvm_dir}",
    ]
    if run(configure, use_vcvars) != 0:
        return fail("the CMake configure step failed")

    if run([str(cmake), "--build", str(build_dir)], use_vcvars) != 0:
        return fail("the build failed")

    dll = build_dir / "Cs2Protect.dll"
    if not dll.is_file():
        return fail(f"the build did not produce '{dll}'")

    print(dll)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
