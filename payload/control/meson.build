# Control channel (ADR-0020): a Win32 named pipe carrying newline-delimited
# JSON, plus the in-process `exec` primitive (docs/re/engine2.md). Windows-only
# logic; json/predicate/snapshot_json are also built host-side for their tests
# (see the root meson.build).
control_inc = include_directories('include')

control_lib = static_library('cs2_control',
  files(
    'src/json.cpp',
    'src/predicate.cpp',
    'src/snapshot_json.cpp',
    'src/console.cpp',
    'src/channel.cpp',
    'src/service.cpp',
  ),
  include_directories : control_inc,
  dependencies : [game_dep, diag_dep, tls_dep, sdk_dep],
)

control_dep = declare_dependency(
  link_with : control_lib,
  include_directories : control_inc,
  dependencies : [game_dep, diag_dep, tls_dep, sdk_dep],
)
