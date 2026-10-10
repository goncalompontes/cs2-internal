project('cs2-internal', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'cpp_std=c++23',
    'warning_level=3',
    'buildtype=debugoptimized',
  ])

# Host-side (Linux) tools, tests and helpers live here.
# The payload is a separate Meson project under payload/ and is
# cross-compiled for Windows (see toolchain/).
subdir('tools')

# Quick self-check so `meson test` is meaningful from day one.
test('host-probe', host_probe)
