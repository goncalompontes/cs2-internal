game_inc = include_directories('include')

game_lib = static_library('cs2_game',
  files(
    'src/memory.cpp', 'src/pattern.cpp', 'src/schema.cpp',
    'src/log.cpp', 'src/rage_debug.cpp',
    'src/module.cpp',
    'src/entity_system.cpp',
    'src/state_reader.cpp',
  ),
  include_directories : game_inc,
  dependencies : sdk_dep,
)

game_dep = declare_dependency(
  link_with : game_lib,
  include_directories : game_inc,
  dependencies : sdk_dep,
)

if native_windows
  fast_snapshot_test = executable('fast_snapshot_test', 'tests/test_fast_snapshot.cpp', dependencies : game_dep)
  test('game-fast-snapshot', fast_snapshot_test)
endif
