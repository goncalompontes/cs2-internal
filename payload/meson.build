# cs2-payload — the Windows DLL.
#
# This is a SEPARATE Meson project (its own root) so it can be configured with
# a Windows cross file while the rest of the repo builds natively. Configure:
#
#   meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
#   meson compile -C build-payload
#
# See docs/architecture.md and toolchain/README.md.

project('cs2-payload', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'cpp_std=c++23',
    'warning_level=3',
    'buildtype=debugoptimized',
  ])

subdir('loader')

if host_machine.system() != 'windows'
  message(
    'cs2-payload targets Windows. This configure produced no targets. ' +
    'Use --cross-file toolchain/windows-msvc.ini to build the DLL.')
else
  shared_library('payload',
    'src/payload_main.cpp',
    name_prefix : '',   # payload.dll, not libpayload.dll
    install : false,
    include_directories : loader_inc,
    link_with : loader_lib)
endif
