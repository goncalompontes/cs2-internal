# cs2-injector — manual-map injector (Windows x64).
#
# A separate Meson project cross-compiled for Windows with the same msvc-wine
# toolchain as the payload. It runs inside the target's Wine/Proton prefix and
# manual-maps the payload (dodging the target's LoadLibrary/signature checks).
#
#   BIN=toolchain/msvc/bin/x64 . toolchain/msvcenv-native.sh
#   meson setup build-injector injector --cross-file toolchain/windows-msvc.ini
#   meson compile -C build-injector

project('cs2-injector', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'warning_level=3',
    'buildtype=debugoptimized',
  ])

# C++23 dialect (see the root meson.build): the spellings avoid Meson's
# "use the built-in cpp_std option" warning (see the root file for details).
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'msvc' or cpp.get_id() == 'clang-cl'
  add_project_arguments('-std:c++latest', language : 'cpp')
else
  add_project_arguments('--std=c++23', language : 'cpp')
endif

# The platform-independent loader core is shared with the payload.
loader_inc = include_directories('../payload/loader/include')
loader_src = files('../payload/loader/src/pe.cpp', '../payload/loader/src/mapping.cpp')

if host_machine.system() != 'windows'
  message(
    'cs2-injector targets Windows. This configure produced no targets. ' +
    'Use --cross-file toolchain/windows-msvc.ini to build injector.exe.')
else
  executable('injector',
    'src/main.cpp',
    loader_src,
    include_directories : loader_inc,
    install : false)

  # Test aids: a benign victim process and a CRT-less trivial DLL to load into
  # it, so the manual mapper can be validated without the target.
  executable('victim', 'tests/victim.cpp', install : false)
  shared_library('trivial', 'tests/trivial_dll.cpp',
    name_prefix : '',
    install : false,
    override_options : ['b_vscrt=none'],
    link_args : ['/entry:DllMain'])
  shared_library('crt_dll', 'tests/crt_dll.cpp',
    name_prefix : '',
    install : false)
endif
