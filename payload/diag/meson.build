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
