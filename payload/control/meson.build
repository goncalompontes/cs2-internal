# Control channel (ADR-0020): a Win32 named pipe carrying newline-delimited
# JSON, plus the in-process `exec` primitive (docs/re/engine2.md). Windows-only
# logic; json/predicate/snapshot_json are also built host-side for their tests
# (see the root meson.build).
control_inc = include_directories('include')

control_lib = static_library('cs2_control',
  files(
    'src/json.cpp',
    'src/aim_options.cpp',
    'src/predicate.cpp',
    'src/snapshot_json.cpp',
    'src/event_ring.cpp',
    'src/events.cpp',
    'src/input.cpp',
    'src/autostop_input.cpp',
    'src/console.cpp',
    'src/channel.cpp',
    'src/service.cpp', 'src/engine_ballistics.cpp', 'src/native_command.cpp', 'src/shot_state.cpp', 'src/live_rage.cpp', 'src/nl_movement_input.cpp', 'src/movement_trace.cpp', 'src/move_clock_probe.cpp', 'src/command_choke.cpp', 'src/movement_physics.cpp',
  ),
  include_directories : control_inc,
  dependencies : [game_dep, aim_dep, diag_dep, tls_dep, sdk_dep, hooks_dep, minhook_dep],
)

control_dep = declare_dependency(
  link_with : control_lib,
  include_directories : control_inc,
  dependencies : [game_dep, aim_dep, diag_dep, tls_dep, sdk_dep, hooks_dep, minhook_dep],
)

if native_windows
  movement_physics_test = executable('movement_physics_test', 'tests/test_movement_physics.cpp', dependencies : control_dep)
  test('movement-physics', movement_physics_test)
  command_choke_test = executable('command_choke_test', 'tests/test_command_choke.cpp', dependencies : control_dep)
  test('command-choke', command_choke_test)
  shared_library('move_clock_live', 'tests/move_clock_live.cpp', name_prefix : '', dependencies : control_dep)
  executable('move_clock_binding_probe', 'tests/move_clock_binding_probe.cpp', dependencies : [control_dep, loader_dep])
  move_clock_test = executable('move_clock_probe_test', 'tests/test_move_clock_probe.cpp', dependencies : control_dep)
  test('move-clock-probe', move_clock_test)
  nl_movement_input_test = executable('nl_movement_input_test', 'tests/test_nl_movement_input.cpp', dependencies : control_dep)
  test('nl-movement-input', nl_movement_input_test)
  shot_state_test = executable('shot_state_test', 'tests/test_shot_state.cpp', dependencies : control_dep)
  test('shot-state', shot_state_test)
  live_rage_test = executable('live_rage_test', 'tests/test_live_rage.cpp', dependencies : control_dep)
  test('live-rage', live_rage_test)
  rage_input_test = executable('rage_input_test', 'tests/test_rage_input.cpp', dependencies : control_dep)
  test('rage-input', rage_input_test)
  engine_ballistics_test = executable('engine_ballistics_test', 'tests/test_engine_ballistics.cpp',
    dependencies : control_dep)
  test('engine-ballistics', engine_ballistics_test)
  aim_worker_test = executable('aim_worker_test', 'tests/test_aim_worker.cpp', dependencies : control_dep)
  test('aim-worker', aim_worker_test, timeout : 10)
  native_command_test = executable('native_command_test', 'tests/test_native_command.cpp', dependencies : control_dep)
  test('native-command', native_command_test)
  executable('native_binding_probe', 'tests/native_binding_probe.cpp', dependencies : [control_dep, loader_dep, features_dep])
endif
