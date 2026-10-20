features_inc = include_directories('include')

features_lib = static_library('cs2_features',
  files('src/present_hook.cpp', 'src/overlay.cpp', 'src/esp.cpp', 'src/dxgi_interposer.cpp',
        'src/model_publisher.cpp', 'src/game_present_hook.cpp', 'src/menu.cpp',
        'src/settings.cpp', 'src/config_store.cpp'),
  include_directories : [features_inc, include_directories('../../overlay/include')],
  dependencies : [hooks_dep, render_dep, game_dep, diag_dep, tls_dep],
)

features_dep = declare_dependency(
  link_with : features_lib,
  include_directories : [features_inc, include_directories('../../overlay/include')],
  dependencies : [game_dep, render_dep, diag_dep, tls_dep],
)
