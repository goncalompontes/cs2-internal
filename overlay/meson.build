# cs2-overlay — portable Vulkan present layer (ADR-0016).
#
# Builds the Linux/Proton `.so` and the native Windows `.dll` from the same
# sources (both consume the Vulkan loader's implicit-layer mechanism).
#
#   meson setup build-overlay overlay
#   meson compile -C build-overlay
#
# The generated manifest (build-overlay/cs2_overlay_layer.json) points at the
# built library and is enabled by the CS2_OVERLAY=1 environment variable. On
# Windows, register the manifest under
# HKLM\SOFTWARE\Khronos\Vulkan\ImplicitLayers (see overlay/README.md).

project('cs2-overlay', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'warning_level=3',
    'buildtype=release',
  ])

# C++23 dialect (see the root meson.build): the spellings avoid Meson's
# "use the built-in cpp_std option" warning (see the root file for details).
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'msvc' or cpp.get_id() == 'clang-cl'
  add_project_arguments('-std:c++latest', language : 'cpp')
else
  add_project_arguments('--std=c++23', language : 'cpp')
endif

on_windows = host_machine.system() == 'windows'

vulkan_headers = include_directories('../external/Vulkan-Headers/include')
overlay_inc = include_directories('include')

# The vendored ImGui is shared with the payload. Reference the payload copy
# directly: the in-tree `overlay/third_party/imgui` is a git symlink, which does
# not materialise as a directory on a native Windows checkout.
imgui_dir = '../payload/third_party/imgui'
imgui_inc = include_directories(imgui_dir, imgui_dir / 'backends')
imgui_sources = files(
  imgui_dir / 'imgui.cpp',
  imgui_dir / 'imgui_draw.cpp',
  imgui_dir / 'imgui_tables.cpp',
  imgui_dir / 'imgui_widgets.cpp',
  imgui_dir / 'backends' / 'imgui_impl_vulkan.cpp',
)

# The layer resolves Vulkan entry points through the loader chain, so it does
# not link the loader. `dependency('vulkan')` is a convenience where a Vulkan
# SDK/pkg-config exists; the vendored headers are authoritative.
vulkan_dep = dependency('vulkan', required : false)
overlay_deps = []
if vulkan_dep.found()
  overlay_deps += vulkan_dep
endif

if on_windows
  name_prefix = ''
  library_name = 'cs2_overlay_layer.dll'
else
  name_prefix = 'lib'
  library_name = 'libcs2_overlay_layer.so'
endif

# `gnu_symbol_visibility` is a GNU/ELF concept; on Windows the entry points are
# exported explicitly with `__declspec(dllexport)` in layer.cpp.
overlay_kwargs = {}
if not on_windows
  overlay_kwargs += {'gnu_symbol_visibility' : 'hidden'}
endif

overlay_layer = shared_library('cs2_overlay_layer',
  'src/layer.cpp',
  'src/renderer.cpp',
  'src/menu.cpp',
  'src/transport.cpp',
  imgui_sources,
  include_directories : [vulkan_headers, overlay_inc, imgui_inc,
                         include_directories('../payload/core/include')],
  dependencies : overlay_deps,
  # ImGui must resolve Vulkan entry points via the layer chain, not the loader's
  # global exports (see renderer.cpp).
  cpp_args : ['-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES'],
  name_prefix : name_prefix,
  install : false,
  kwargs : overlay_kwargs,
)

# The manifest references the built library by absolute path, matching how
# MangoHud/Steam register their implicit layers.
manifest = configure_file(
  input : 'manifest/cs2_overlay_layer.json.in',
  output : 'cs2_overlay_layer.json',
  configuration : {
    'LIBRARY_PATH' : meson.current_build_dir() / library_name,
  },
)
