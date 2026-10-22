# kiero (swapchain method-table discovery) + vendored MinHook (inline hooks).
# kiero::bind() installs a MinHook inline hook on the target function, matching
# the approach used by the reference overlay. D3D11-only (see kiero.h).
kiero_inc = include_directories('.', 'minhook/include')
minhook_inc = include_directories('minhook/include', 'minhook/src')

minhook_lib = static_library('minhook',
  files(
    'minhook/src/buffer.c',
    'minhook/src/hook.c',
    'minhook/src/trampoline.c',
    'minhook/src/hde/hde64.c',
  ),
  include_directories : minhook_inc,
)

kiero_lib = static_library('kiero',
  files('kiero.cpp'),
  include_directories : kiero_inc,
  link_with : minhook_lib,
)

kiero_dep = declare_dependency(
  link_with : kiero_lib,
  include_directories : kiero_inc,
)

minhook_dep = declare_dependency(link_with : minhook_lib, include_directories : minhook_inc)
