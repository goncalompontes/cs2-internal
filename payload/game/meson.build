game_inc = include_directories('include')

game_lib = static_library('cs2_game',
  files(
    'src/memory.cpp',
    'src/log.cpp',
    'src/module.cpp',
    'src/entity_system.cpp',
    'src/state_reader.cpp',
    'src/camera.cpp',
    'src/material.cpp',
  ),
  include_directories : game_inc,
  dependencies : sdk_dep,
)

game_dep = declare_dependency(
  link_with : game_lib,
  include_directories : game_inc,
  dependencies : sdk_dep,
)
