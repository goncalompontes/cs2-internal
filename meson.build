project('cs2-internal', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'warning_level=3',
    'buildtype=debugoptimized',
  ])

# C++23 dialect. Meson's built-in cpp_std option does not list 'c++23' for the
# MSVC-style drivers (clang-cl / cl.exe), so the flag is selected explicitly.
# The spellings below avoid Meson's "use the built-in cpp_std option" warning:
#   - GNU-like drivers accept the long form `--std=c++23` (GCC and Clang);
#   - clang-cl / cl.exe accept the dash form `-std:c++latest` (C++23 and later).
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'msvc' or cpp.get_id() == 'clang-cl'
  add_project_arguments('-std:c++latest', language : 'cpp')
else
  add_project_arguments('--std=c++23', language : 'cpp')
endif

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

# The third-person camera model (the origin/distance/angle composition) —
# header-only and platform-independent, so the camera maths has a host
# regression test next to the matrix one.
game_camera_test = executable('game_camera_test', 'payload/game/tests/test_camera.cpp',
  include_directories : include_directories('payload/game/include'),
)
test('game-camera', game_camera_test)

# Ragebot aim geometry (the angular chart, hulls, optima) — platform-independent,
# built natively so the aim model has a host regression test.
aim_geometry_test = executable('aim_geometry_test',
  files(
    'payload/aim/tests/test_aim_geometry.cpp',
    'payload/aim/src/geometry.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-geometry', aim_geometry_test)

aim_bvh_test = executable('aim_bvh_test',
  files(
    'payload/aim/tests/test_aim_bvh.cpp',
    'payload/aim/src/bvh.cpp',
    'payload/aim/src/geometry.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-bvh', aim_bvh_test)

aim_field_test = executable('aim_field_test',
  files(
    'payload/aim/tests/test_aim_field.cpp',
    'payload/aim/src/geometry.cpp',
    'payload/aim/src/field.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-field', aim_field_test)

aim_spread_test = executable('aim_spread_test',
  files(
    'payload/aim/tests/test_aim_spread.cpp',
    'payload/aim/src/spread.cpp',
    'payload/aim/src/geometry.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-spread', aim_spread_test)

aim_solver_test = executable('aim_solver_test',
  files(
    'payload/aim/tests/test_aim_solver.cpp',
    'payload/aim/src/geometry.cpp',
    'payload/aim/src/field.cpp',
    'payload/aim/src/hitbox.cpp',
    'payload/aim/src/solver.cpp',
    'payload/aim/src/damage.cpp',
    'payload/aim/src/occlusion.cpp',
    'payload/aim/src/penetration.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-solver', aim_solver_test)

aim_records_test = executable('aim_records_test',
  files(
    'payload/aim/tests/test_aim_records.cpp',
    'payload/aim/src/records.cpp',
    'payload/aim/src/hitbox.cpp',
    'payload/aim/src/geometry.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-records', aim_records_test)

aim_heatmap_test = executable('aim_heatmap_test',
  files(
    'payload/aim/tests/test_aim_heatmap.cpp',
    'payload/aim/src/geometry.cpp',
    'payload/aim/src/heatmap.cpp',
    'payload/aim/src/occlusion.cpp',
    'payload/aim/src/damage.cpp',
    'payload/aim/src/penetration.cpp',
    'payload/aim/src/records.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-heatmap', aim_heatmap_test)

aim_occlusion_test = executable('aim_occlusion_test',
  files(
    'payload/aim/tests/test_aim_occlusion.cpp',
    'payload/aim/src/geometry.cpp',
    'payload/aim/src/field.cpp',
    'payload/aim/src/heatmap.cpp',
    'payload/aim/src/occlusion.cpp',
    'payload/aim/src/damage.cpp',
    'payload/aim/src/penetration.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-occlusion', aim_occlusion_test)

aim_penetration_test = executable('aim_penetration_test',
  files(
    'payload/aim/tests/test_aim_penetration.cpp',
    'payload/aim/src/geometry.cpp',
    'payload/aim/src/field.cpp',
    'payload/aim/src/heatmap.cpp',
    'payload/aim/src/occlusion.cpp',
    'payload/aim/src/damage.cpp',
    'payload/aim/src/penetration.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-penetration', aim_penetration_test)

# Control-channel core (ADR-0020): the platform-independent parts of
# payload/control (JSON value, `wait_for` predicate, snapshot serialization are
# built here; the pipe/console/thread parts are Windows-only and live in the
# payload subproject).
control_core_test = executable('control_core_test',
  files(
    'payload/control/tests/test_control_core.cpp',
    'payload/control/src/json.cpp',
    'payload/control/src/predicate.cpp',
    'payload/control/src/snapshot_json.cpp',
    'payload/control/src/event_ring.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
    include_directories('payload/game/include'),
  ],
)
test('control-core', control_core_test)

# The auto-strafe's decision maths (the W/A/S/D travel heading, the side's
# dead zone and hysteresis, and the `AirAccelerate` optimum) — a pure,
# dependency-free header, built natively so the strafe's maths has a host
# regression test (the module itself is header-only, so no source is listed).
control_strafe_test = executable('control_strafe_test',
  files(
    'payload/control/tests/test_control_strafe.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
  ],
)
test('control-strafe', control_strafe_test)

# The feature settings foundation: the wire <-> config conversion and the
# config store's round trip. The settings block is a POD and the conversion is
# pure, so the feature sources are built natively here (the payload itself
# needs the game).
features_config_test = executable('features_config_test',
  files(
    'payload/features/tests/test_features_config.cpp',
    'payload/features/src/settings.cpp',
    'payload/features/src/config_store.cpp',
    'payload/game/src/log.cpp',
  ),
  include_directories : [
    include_directories('payload/features/include'),
    include_directories('overlay/include'),
    include_directories('payload/game/include'),
    include_directories('payload/render/include'),
  ],
)
test('features-config', features_config_test)

# --- Generated C++ SDK headers -------------------------------------------
# Render payload/sdk/*.hpp from the calibration dumps in calibration/. The
# generator is the Rust dumper, built on demand by scripts/gen-sdk.py. With
# -Dlive_calibration=true the script refreshes the dumps from a running target
# first (see scripts/calibrate-live.sh).
if get_option('sdk_gen')
  gen_sdk = find_program('scripts/gen-sdk.py')
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
