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
    'cpp_std=c++23',
    'warning_level=3',
    'buildtype=debugoptimized',
  ])

# --- Build provenance -----------------------------------------------------
# Distinguish a *native* Windows build (host == build == windows) from a cross
# build produced on Linux with msvc-wine. Native Windows builds are UNTESTED:
# the payload has not been validated on a real Windows host yet — see
# docs/handoff.md. The define is consumed by src/payload_main.cpp (boot banner +
# marker file) and features/src/overlay.cpp (HUD title).
native_windows = host_machine.system() == 'windows' and build_machine.system() == 'windows'
if native_windows
  add_project_arguments('-DCS2_NATIVE_WINDOWS_BUILD=1', language : ['c', 'cpp'])
  message('*** cs2-internal: NATIVE WINDOWS build — UNTESTED ON WINDOWS ***')
elif host_machine.system() == 'windows'
  add_project_arguments('-DCS2_CROSS_WINDOWS_BUILD=1', language : ['c', 'cpp'])
  message('cs2-internal: cross Windows build (build machine: ' + build_machine.system() + ')')
endif

subdir('loader')

# --- Generated SDK headers ------------------------------------------------
# The payload consumes the generated SDK (payload/sdk). Generate it here too so
# a standalone payload build works; scripts/gen-sdk.sh uses the calibration
# dumps and builds the host dumper on demand.
gen_sdk = find_program(meson.current_source_dir() / '..' / 'scripts' / 'gen-sdk.sh')
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
  # kiero + MinHook were retired with ADR-0015 (the inline Present hook stalled
  # the game under DXVK). The swapchain is now interposed via its method table.
  # The vendored copy under third_party/kiero is kept for reference only.
  subdir('hooks')
  subdir('game')
  subdir('diag')
  subdir('tls')
  subdir('render')
  subdir('features')

  shared_library('payload',
    'src/payload_main.cpp',
    name_prefix : '',   # payload.dll, not libpayload.dll
    install : false,
    include_directories : loader_inc,
    link_with : loader_lib,
    dependencies : [sdk_dep, hooks_dep, game_dep, diag_dep, render_dep, features_dep])
endif
