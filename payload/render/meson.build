render_inc = include_directories('include')

render_lib = static_library('cs2_render',
  files('src/frame_log_backend.cpp', 'src/imgui_backend.cpp', 'src/native_menu.cpp'),
  include_directories : [render_inc, include_directories('../../overlay/include')],
  dependencies : [imgui_dep, game_dep, tls_dep],
)

render_dep = declare_dependency(
  link_with : render_lib,
  include_directories : [render_inc, include_directories('../../overlay/include')],
  dependencies : [imgui_dep, game_dep, tls_dep],
)

if native_windows
  rage_debug_test = executable('rage_debug_test', 'tests/rage_debug.cpp', dependencies : render_dep)
  test('rage-debug-panel', rage_debug_test)
endif
