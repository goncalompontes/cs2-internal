features_inc = include_directories('include')

features_lib = static_library('cs2_features',
  files('src/present_hook.cpp', 'src/overlay.cpp', 'src/esp.cpp'),
  include_directories : features_inc,
  dependencies : [hooks_dep, render_dep, game_dep, kiero_dep],
)

features_dep = declare_dependency(
  link_with : features_lib,
  include_directories : features_inc,
  dependencies : [game_dep, render_dep],
)
