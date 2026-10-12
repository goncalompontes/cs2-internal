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
  # The payload never uses gamepad input, and the backend's XInput LoadLibraryA
  # loop hangs when called from a manually mapped module inside Present.
  cpp_args : ['-DIMGUI_IMPL_WIN32_DISABLE_GAMEPAD'],
)

imgui_dep = declare_dependency(
  link_with : imgui_lib,
  include_directories : imgui_inc,
)
