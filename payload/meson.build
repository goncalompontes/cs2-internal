# cs2-payload — the Windows DLL.
#
# This is a SEPARATE Meson project (its own root) so it can be configured with
# a Windows cross file while the rest of the repo builds natively. Configure:
#
#   meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
#   meson compile -C build-payload
#
# See docs/architecture.md and toolchain/README.md.

project('cs2-payload', 'c', 'cpp',
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

# --- Build provenance -----------------------------------------------------
# Distinguish a *native* Windows build (host == build == windows) from a cross
# build produced on Linux with msvc-wine. The define is consumed by
# src/payload_main.cpp (boot banner + marker file) and features/src/overlay.cpp.
# The native Windows build has now been validated in-game (see
# docs/handoff.md); the historic "untested" marker was removed 2026-10.
native_windows = host_machine.system() == 'windows' and build_machine.system() == 'windows'
if native_windows
  add_project_arguments('-DCS2_NATIVE_WINDOWS_BUILD=1', language : ['c', 'cpp'])
  message('cs2-internal: native Windows build')
elif host_machine.system() == 'windows'
  add_project_arguments('-DCS2_CROSS_WINDOWS_BUILD=1', language : ['c', 'cpp'])
  message('cs2-internal: cross Windows build (build machine: ' + build_machine.system() + ')')
endif

subdir('loader')

# --- Generated SDK headers ------------------------------------------------
# The payload consumes the generated SDK (payload/sdk). Generate it here too so
# a standalone payload build works; scripts/gen-sdk.py uses the calibration
# dumps and builds the host dumper on demand.
gen_sdk = find_program(meson.current_source_dir() / '..' / 'scripts' / 'gen-sdk.py')
sdk_dir = meson.current_source_dir() / 'sdk'
dump_dir = meson.current_source_dir() / '..' / 'calibration'

sdk_headers = custom_target('sdk-headers',
  output : 'sdk-headers.stamp',
  command : [gen_sdk, dump_dir, sdk_dir, '@OUTPUT@'],
  build_always_stale : true,
  console : true,
)

sdk_dep = declare_dependency(
  include_directories : include_directories('sdk'),
  sources : sdk_headers,
)

if host_machine.system() != 'windows'
  message(
    'cs2-payload targets Windows. This configure produced no targets. ' +
    'Use --cross-file toolchain/windows-msvc.ini to build the DLL.')
else
  subdir('third_party/imgui')
  # Native Windows uses DXGI function hooks; retain the existing Proton path.
  subdir('third_party/kiero')
  subdir('hooks')
  subdir('game')
  subdir('aim')
  subdir('diag')
  subdir('tls')
  subdir('render')
  subdir('features')
  subdir('control')

  shared_library('payload',
    'src/payload_main.cpp',
    name_prefix : '',   # payload.dll, not libpayload.dll
    install : false,
    include_directories : loader_inc,
    link_with : loader_lib,
    dependencies : [sdk_dep, hooks_dep, game_dep, aim_dep, diag_dep, render_dep, features_dep,
                    control_dep])
endif
