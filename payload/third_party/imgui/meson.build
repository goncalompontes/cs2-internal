# Vendored Dear ImGui (core + D3D11 backend). See LICENSE.txt.
# Windows-only (the backend uses d3d11).
#
# The Win32 platform backend is intentionally not built: the overlay is
# display-only and the backend's window/mouse calls from the render thread
# deadlock under Wine (ADR-0015).
imgui_inc = include_directories('.', 'backends')

imgui_lib = static_library('imgui',
  files(
    'imgui.cpp',
    'imgui_draw.cpp',
    'imgui_tables.cpp',
    'imgui_widgets.cpp',
    'backends/imgui_impl_dx11.cpp',
  ),
  include_directories : imgui_inc,
)

imgui_dep = declare_dependency(
  link_with : imgui_lib,
  include_directories : imgui_inc,
)
