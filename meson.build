project('cs2-internal', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'warning_level=3',
    'buildtype=debugoptimized',
    # The host build carries the tests, and `scripts/release.sh` configures it
    # with `--buildtype=release`. Meson's `b_ndebug` default is `if-release`,
    # so `NDEBUG` would be defined there and every `assert()` in the test
    # suite would be compiled out: a test that only bites in the debug build
    # is not a test. The assertions stay live in every configuration; the
    # tests that must survive `NDEBUG` do not rely on `assert` at all (they
    # count their failures explicitly).
    'b_ndebug=false',
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

# The third-person camera model (the origin/distance/angle composition) and
# the build guard on the camera's build-specific reads — the model is
# header-only and platform-independent, and the reads take the engine access
# as a parameter, so the camera maths and the guard have a host regression
# test next to the matrix one (the test drives the reads against a fake
# client, so the guard is proven without a game).
game_camera_test = executable('game_camera_test',
  files(
    'payload/game/tests/test_camera.cpp',
    'payload/game/src/camera.cpp',
  ),
  include_directories : [
    include_directories('payload/game/include'),
    include_directories('payload/game/src'),
  ],
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

# The settings name table (generated): the control channel's `settings` command
# addresses a feature setting by its flat wire field name, and the byte offset
# of a field is a property of the packed layout -- 87 hand-maintained entries
# would drift. `scripts/gen-settings-table.py` parses `wire.hpp` and writes the
# table into the source tree (the same pattern as `scripts/gen-sdk.py`); it
# fails loudly on a parse error rather than emitting an empty table.
gen_settings_table = find_program('scripts/gen-settings-table.py')
settings_table = custom_target('settings-table',
  output : 'settings-table.stamp',
  command : [gen_settings_table,
             meson.current_source_dir() / 'overlay' / 'include' / 'cs2' / 'overlay' / 'wire.hpp',
             meson.current_source_dir() / 'overlay' / 'include' / 'cs2' / 'overlay' / 'settings_table.hpp',
             '@OUTPUT@'],
  build_always_stale : true,
  console : true,
  build_by_default : true,
)
settings_table_dep = declare_dependency(
  sources : settings_table,
)

# The table must never be stale: the check fails when the file on disk differs
# from a fresh generation (the build above regenerates it, so this catches a
# hand-edited table).
test('settings-table', gen_settings_table, args : ['--check'])

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
    'payload/control/src/settings_access.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
    include_directories('payload/game/include'),
    include_directories('overlay/include'),
  ],
  dependencies : settings_table_dep,
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

# The ESP's out-of-module providers (the sound events and the per-player
# visibility cache). Their host-testable parts are header-only, so the test
# compiles the event ring next to them.
control_sound_test = executable('control_sound_test',
  files(
    'payload/control/tests/test_control_sound.cpp',
    'payload/control/src/event_ring.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
    include_directories('payload/game/include'),
    include_directories('payload/aim/include'),
    # The control service now reaches the shared settings block (the generated
    # name table lives next to it), so the host build needs that include path.
    include_directories('overlay/include'),
  ],
)
test('control-sound', control_sound_test)

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

# The ESP's pure screen-space geometry and the hitbox-derived skeleton chain.
# Both are dependency-free (no engine, no ImGui), so they are built natively
# here and regression-tested: the ESP's maths cannot be checked in a live
# game.
features_esp_geometry_test = executable('features_esp_geometry_test',
  files(
    'payload/features/tests/test_esp_geometry.cpp',
    'payload/features/src/esp_skeleton.cpp',
    'payload/features/src/esp_world_class.cpp',
  ),
  include_directories : [
    include_directories('payload/features/include'),
    include_directories('payload/features/src'),
    include_directories('payload/game/include'),
    include_directories('payload/render/include'),
  ],
)
test('features-esp-geometry', features_esp_geometry_test)

# The material system's parameter-name hash and the chams' "is this entity one
# of ours" decision. Both are pure (no engine, no Windows), so they are built
# natively here and regression-tested: the hash is keyed by the engine's own
# constants, and the decision decides whether a model is drawn with the flat
# material.
game_material_test = executable('game_material_test',
  files(
    'payload/game/tests/test_material.cpp',
    'payload/game/src/material.cpp',
    'payload/game/src/memory.cpp',
    'payload/game/src/module.cpp',
  ),
  include_directories : include_directories('payload/game/include'),
)
test('game-material', game_material_test)

# The shared menu header (`overlay/include/cs2/overlay/menu_pages.hpp`): the
# one definition of the menu's content for both renderers. Dear ImGui runs
# headless, so the **real** `draw_menu()` can be driven with a real ImGui
# context: the window's geometry, the keybind capture, the key names and the
# config-op protocol are all host-testable.
menu_geometry_test = executable('menu_geometry_test',
  files(
    'overlay/tests/test_menu_geometry.cpp',
    'payload/third_party/imgui/imgui.cpp',
    'payload/third_party/imgui/imgui_draw.cpp',
    'payload/third_party/imgui/imgui_tables.cpp',
    'payload/third_party/imgui/imgui_widgets.cpp',
  ),
  include_directories : [
    include_directories('overlay/include'),
    include_directories('overlay/tests'),
    include_directories('payload/third_party/imgui'),
  ],
)
test('menu-geometry', menu_geometry_test)

menu_pages_test = executable('menu_pages_test',
  files(
    'overlay/tests/test_menu_pages.cpp',
    'payload/features/src/settings.cpp',
    'payload/features/src/config_store.cpp',
    'payload/game/src/log.cpp',
    'payload/third_party/imgui/imgui.cpp',
    'payload/third_party/imgui/imgui_draw.cpp',
    'payload/third_party/imgui/imgui_tables.cpp',
    'payload/third_party/imgui/imgui_widgets.cpp',
  ),
  include_directories : [
    include_directories('overlay/include'),
    include_directories('overlay/tests'),
    include_directories('payload/features/include'),
    include_directories('payload/game/include'),
    include_directories('payload/render/include'),
    include_directories('payload/third_party/imgui'),
  ],
)
test('menu-pages', menu_pages_test)

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
