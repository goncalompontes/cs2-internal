render_inc = include_directories('include')

render_lib = static_library('cs2_render',
  files('src/frame_log_backend.cpp', 'src/imgui_backend.cpp'),
  include_directories : render_inc,
  dependencies : imgui_dep,
)

render_dep = declare_dependency(
  link_with : render_lib,
  include_directories : render_inc,
  dependencies : imgui_dep,
)
