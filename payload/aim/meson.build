# cs2/aim — the ragebot aim model (docs/ragebot.md, ADR-0022).
#
# Platform-independent: the angular chart geometry, the damage field and the
# backtrack record ring. Built into the Windows payload and into the host tests.

aim_inc = include_directories('include')

aim_lib = static_library('cs2_aim',
  files(
    'src/geometry.cpp',
    'src/field.cpp',
    'src/hitbox.cpp',
    'src/solver.cpp',
    'src/reader.cpp',
    'src/records.cpp',
    'src/heatmap.cpp',
    'src/occlusion.cpp',
    'src/penetration.cpp',
    'src/spread.cpp',
  ),
  include_directories : [aim_inc, game_inc],
)

aim_dep = declare_dependency(
  link_with : aim_lib,
  include_directories : aim_inc,
  dependencies : game_dep,
)
