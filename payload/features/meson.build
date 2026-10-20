features_inc = include_directories('include')

features_lib = static_library('cs2_features',
  files('src/present_hook.cpp', 'src/overlay.cpp', 'src/esp.cpp', 'src/dxgi_interposer.cpp',
        'src/model_publisher.cpp', 'src/game_present_hook.cpp', 'src/menu.cpp',
        'src/settings.cpp', 'src/config_store.cpp', 'src/thirdperson.cpp',
        'src/esp_players.cpp', 'src/esp_skeleton.cpp', 'src/esp_hitboxes.cpp',
        'src/esp_radar.cpp', 'src/esp_world.cpp', 'src/esp_world_class.cpp',
        'src/esp_sound.cpp', 'src/esp_providers.cpp'),
  include_directories : [features_inc, include_directories('../../overlay/include')],
  dependencies : [hooks_dep, render_dep, game_dep, diag_dep, tls_dep, aim_dep],
)

features_dep = declare_dependency(
  link_with : features_lib,
  include_directories : [features_inc, include_directories('../../overlay/include')],
  dependencies : [game_dep, render_dep, diag_dep, tls_dep, aim_dep],
)
