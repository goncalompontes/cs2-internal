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

# Platform-independent manual-map core (built natively here for its tests, and
# cross-compiled into the payload).
subdir('payload/loader')

# Hook engine (built natively here for its tests).
subdir('payload/hooks')

# World-to-screen projection math (header-only, platform-independent), built
# natively so the matrix convention has a host regression test.
game_matrix_test = executable('game_matrix_test', 'payload/game/tests/test_matrix.cpp',
  include_directories : include_directories('payload/game/include'),
)
test('game-matrix', game_matrix_test)

# --- Generated C++ SDK headers -------------------------------------------
# Render payload/sdk/*.hpp from the calibration dumps in calibration/. The
# generator is the Rust dumper, built on demand by scripts/gen-sdk.sh. With
# -Dlive_calibration=true the script refreshes the dumps from a running target
# first (see scripts/calibrate-live.sh).
if get_option('sdk_gen')
  gen_sdk = find_program('scripts/gen-sdk.sh')
  dump_dir = meson.current_source_dir() / 'calibration'
  sdk_dir = meson.current_source_dir() / 'payload' / 'sdk'

  gen_cmd = [gen_sdk, dump_dir, sdk_dir, '@OUTPUT@']
  if get_option('live_calibration')
    gen_cmd = [gen_sdk, '--live', dump_dir, sdk_dir, '@OUTPUT@']
  endif

  sdk_headers = custom_target('sdk-headers',
    output : 'sdk-headers.stamp',
    command : gen_cmd,
    build_always_stale : true,
    console : true,
    build_by_default : true,
  )

  # A dependency that carries the generated include dir and the generation
  # order, so consumers (tests, and the payload) build against fresh headers.
  sdk_dep = declare_dependency(
    include_directories : include_directories('payload' / 'sdk'),
    sources : sdk_headers,
  )
  sdk_consumer = executable('sdk-consumer', 'tools/sdk_consumer.cpp',
    dependencies : sdk_dep,
  )
  test('sdk-consumer', sdk_consumer)
endif

# Quick self-check so `meson test` is meaningful from day one.
test('host-probe', host_probe)
