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
  shared_library('payload',
    'src/payload_main.cpp',
    name_prefix : '',   # payload.dll, not libpayload.dll
    install : false,
    include_directories : loader_inc,
    link_with : loader_lib,
    dependencies : sdk_dep)
endif
