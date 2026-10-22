features_inc = include_directories('include')

features_lib = static_library('cs2_features',
  files('src/present_hook.cpp', 'src/overlay.cpp', 'src/esp.cpp', 'src/dxgi_interposer.cpp',
        'src/model_publisher.cpp', 'src/game_present_hook.cpp', 'src/camera.cpp'),
  include_directories : [features_inc, include_directories('../../overlay/include')],
  dependencies : [hooks_dep, render_dep, game_dep, diag_dep, tls_dep, kiero_dep],
)

features_dep = declare_dependency(
  link_with : features_lib,
  include_directories : features_inc,
  dependencies : [hooks_dep, game_dep, render_dep, diag_dep, tls_dep, kiero_dep],
)

if native_windows
  camera_test = executable('camera_test', 'tests/camera.cpp', dependencies : features_dep)
  test('camera-features', camera_test)
  native_overlay_test = executable('native_overlay_test', 'tests/native_overlay.cpp',
    dependencies : [features_dep, cpp.find_library('d3d11'), cpp.find_library('dxgi')],
  )
  test('native-overlay', native_overlay_test, timeout : 30)
endif
