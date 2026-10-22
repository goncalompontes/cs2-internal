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
# is `nounwind`, and the `longjmp` out of a `nounwind` frame runs the frame's
# `std::terminate` cleanup instead of recovering, so the process dies with
# 0xC0000409. That is clang-cl's behaviour -- clang-cl is the compiler the
# payload ships with -- and not MSVC's, so a test built with `cl.exe` passes
# either way and can never see the defect.
#
# The test therefore lives in the **payload's own build** (clang-cl) and runs
# there. The host build registers the same test for the source check, but the
# behavioural half is only meaningful here.
if not meson.is_cross_build()
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
