# Diagnostics: crash handler (vectored + unhandled exception logging with stack
# traces) and a freeze watchdog. Allocation-free, so it is usable from inside
# exception handlers and while the process is wedged.
diag_inc = include_directories('include')

diag_lib = static_library('cs2_diag',
  files('src/text.cpp', 'src/crash_handler.cpp', 'src/watchdog.cpp'),
  include_directories : [diag_inc, core_inc],
  dependencies : game_dep,
)

diag_dep = declare_dependency(
  link_with : diag_lib,
  include_directories : [diag_inc, core_inc],
  dependencies : game_dep,
)

# The fault guard's own test (`cs2::diag::call_guarded`).
#
# The guard is the payload's only safety net for an engine call that faults,
# and **what it does depends on the compiler**: a `noexcept` vectored handler
# is not `nounwind` -- clang-cl gives its frame a C++ exception-handler unwind
# record whose cleanup action is `std::terminate`, and the `longjmp` out of
# that frame runs the action, so the process dies with 0xC0000409. That is
# clang-cl's behaviour -- clang-cl is the compiler the payload ships with --
# and not MSVC's: under `cl.exe` the same source recovers.
#
# **How the test is run.** It is registered in **two** builds, and both of them
# have to be run:
#
#   meson test -C build          # the host copy (the root meson.build)
#   meson test -C build-payload  # this copy -- the payload's own clang-cl build
#
# The host copy's behavioural half is only meaningful in a **clang-cl host
# build** (with `cl.exe` the guard recovers either way); this repository's host
# build is clang-cl, which is why the host copy is worth registering. The
# defect survived because **there was no test at all**, not because the
# compiler was wrong.
#
# The copy here is the one that ships with the payload, so it is registered
# whenever the build machine can run the binaries: a cross build that still
# runs on the build machine (the CI job configures the payload with
# `toolchain/windows-msvc.ini` on a Windows runner) registers it, and a cross
# build to a foreign system does not.
if not meson.is_cross_build() or meson.can_run_host_binaries()
  diag_guard_test = executable('diag_guard_test',
    files('tests/test_diag_guard.cpp'),
    dependencies : diag_dep,
  )
  test('diag-guard', diag_guard_test,
    # The source check reads the payload's own sources: the tree must hold
    # exactly one recovery mechanism.
    args : [meson.current_source_dir() / '..'],
  )
endif
