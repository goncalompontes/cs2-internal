render_inc = include_directories('include')

# Embed the ESP pixel font (vendored TTF) as a byte array at build time.
gen_font = find_program(meson.project_source_root() / '..' / 'scripts' / 'gen-font.py')
esp_font_src = custom_target('esp-font',
  input : meson.current_source_dir() / '..' / 'third_party' / 'fonts' / 'Silkscreen-Regular.ttf',
  output : 'esp_font.cpp',
  command : [gen_font, '@INPUT@', '@OUTPUT@'],
)

render_lib = static_library('cs2_render',
  files('src/frame_log_backend.cpp', 'src/imgui_backend.cpp') + [esp_font_src],
  include_directories : render_inc,
  dependencies : imgui_dep,
)

render_dep = declare_dependency(
  link_with : render_lib,
  include_directories : render_inc,
  dependencies : imgui_dep,
)
