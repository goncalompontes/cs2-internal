# Vendored Dear ImGui (core + D3D11/Win32 backends). See LICENSE.txt.
# Windows-only (the backends use d3d11/user32).
imgui_inc = include_directories('.', 'backends')

imgui_lib = static_library('imgui',
  files(
    'imgui.cpp',
    'imgui_draw.cpp',
    'imgui_tables.cpp',
    'imgui_widgets.cpp',
    'backends/imgui_impl_dx11.cpp',
    'backends/imgui_impl_win32.cpp',
  ),
  include_directories : imgui_inc,
  cpp_args : ['-DIMGUI_DISABLE_OBSOLETE_FUNCTIONS'],
)

imgui_dep = declare_dependency(
  link_with : imgui_lib,
  include_directories : imgui_inc,
)
