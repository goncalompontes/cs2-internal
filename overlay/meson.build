# cs2-overlay — portable Vulkan present layer (ADR-0016).
#
# Native build for the Linux/Proton layer `.so`. A Windows `.dll` target is
# added later with the same sources (both consume the Vulkan loader's implicit
# layer mechanism).
#
#   meson setup build-overlay overlay
#   meson compile -C build-overlay
#
# The generated manifest (build-overlay/cs2_overlay_layer.json) points at the
# built .so and is enabled by the CS2_OVERLAY=1 environment variable.

project('cs2-overlay', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'cpp_std=c++23',
    'warning_level=3',
    'buildtype=release',
  ])

vulkan_headers = include_directories('../external/Vulkan-Headers/include')
overlay_inc = include_directories('include')
# For the shared payload header (esp_font.hpp) used by the renderer.
render_inc = include_directories('../payload/render/include')

# The vendored ImGui is shared with the payload via a symlink; this project
# builds the core + Vulkan backend (the payload builds the DX11 one).
imgui_dir = 'third_party/imgui'
imgui_inc = include_directories(imgui_dir, imgui_dir / 'backends')
imgui_sources = files(
  imgui_dir / 'imgui.cpp',
  imgui_dir / 'imgui_draw.cpp',
  imgui_dir / 'imgui_tables.cpp',
  imgui_dir / 'imgui_widgets.cpp',
  imgui_dir / 'backends' / 'imgui_impl_vulkan.cpp',
)

# Embed the ESP pixel font (shared with the payload's render backend).
gen_font = find_program(meson.project_source_root() / '..' / 'scripts' / 'gen-font.py')
esp_font_src = custom_target('esp-font',
  input : meson.project_source_root() / '..' / 'payload' / 'third_party' / 'fonts' / 'Silkscreen-Regular.ttf',
  output : 'esp_font.cpp',
  command : [gen_font, '@INPUT@', '@OUTPUT@'],
)

vulkan_dep = dependency('vulkan')

overlay_layer = shared_library('cs2_overlay_layer',
  'src/layer.cpp',
  'src/renderer.cpp',
  'src/transport.cpp',
  imgui_sources,
  esp_font_src,
  include_directories : [vulkan_headers, overlay_inc, render_inc, imgui_inc],
  dependencies : [vulkan_dep],
  # ImGui must resolve Vulkan entry points via the layer chain, not the loader's
  # global exports (see renderer.cpp).
  cpp_args : ['-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES'],
  name_prefix : 'lib',
  install : false,
  gnu_symbol_visibility : 'hidden',
)

# The manifest references the built library by absolute path, matching how
# MangoHud/Steam register their implicit layers.
manifest = configure_file(
  input : 'manifest/cs2_overlay_layer.json.in',
  output : 'cs2_overlay_layer.json',
  configuration : {
    'LIBRARY_PATH' : meson.current_build_dir() / 'libcs2_overlay_layer.so',
  },
)
