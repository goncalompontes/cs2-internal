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

# The shared RAII handle (cs2/core/handle.hpp). The ownership rules are
# platform-independent, so they have a host regression test, which the module
# wires up itself (payload/core/meson.build).
subdir('payload/core')

# The control and the features headers reach the shared RAII handle:
# `cs2/control/channel.hpp` -- and `cs2/control/service.hpp`, which includes
# the channel -- and `cs2/features/model_publisher.hpp` include
# `cs2/core/handle.hpp`. A target that includes one of those headers
# therefore needs the core include directory as well, so the pairs are
# published here as dependencies instead of being re-listed in every test: a
# new host test that includes a control header takes `control_host_dep` and
# compiles.
control_host_dep = declare_dependency(
  include_directories : [include_directories('payload/control/include'), core_inc],
)
features_host_dep = declare_dependency(
  include_directories : [include_directories('payload/features/include'), core_inc],
)

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

# The Monte-Carlo hit-chance solver (docs/orchestration/spec-t34-montecarlo.md): the
# engine's own spread kernel, the nearest-hit ray test and the lexicographic
# selection rule. Platform-independent, so it builds here as well as into the
# payload.
aim_montecarlo_test = executable('aim_montecarlo_test',
  files(
    'payload/aim/tests/test_aim_montecarlo.cpp',
    'payload/aim/src/montecarlo.cpp',
    'payload/aim/src/spread.cpp',
    'payload/aim/src/geometry.cpp',
    'payload/aim/src/hitbox.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('aim-montecarlo', aim_montecarlo_test)

# The Monte-Carlo solver's **benchmark** (not a test: a benchmark must not slow the
# suite). It replays the committed saved cell set (`artifacts/live-heatmap.json`) and
# publishes the solver's p50/p95/p99 next to the existing objective's cost. The data
# path is relative to the repository root, so the benchmark is run from there.
aim_bench = executable('aim_bench',
  files(
    'payload/aim/bench/bench_montecarlo.cpp',
    'payload/aim/src/montecarlo.cpp',
    'payload/aim/src/spread.cpp',
    'payload/aim/src/geometry.cpp',
    'payload/aim/src/field.cpp',
    'payload/control/src/json.cpp',
  ),
  include_directories : [
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
    include_directories('payload/control/include'),
  ],
)

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
    # The Monte-Carlo objective (docs/ragebot.md §13): the solver dispatches to
    # the core, so the test links it and its spread model.
    'payload/aim/src/montecarlo.cpp',
    'payload/aim/src/spread.cpp',
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
#
# The table is **checked in** and the build **verifies** it: the target below
# runs the generator in `--check` mode, so a table that no longer matches
# `wire.hpp` fails the build instead of reaching a binary. (It used to rewrite
# the file on every build, which made the `settings-table` test vacuous: the
# test ran after the target had already regenerated the file, so it could never
# fail -- and a **payload-only** build, which had no such target at all,
# compiled against the stale table and wrote a neighbouring field's bytes.)
gen_settings_table = find_program('scripts/gen-settings-table.py')
settings_table = custom_target('settings-table',
  output : 'settings-table.stamp',
  command : [gen_settings_table,
             '--check',
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
# from a fresh generation. The build does not rewrite the file, so the file the
# test compares against is the checked-in one.
#
# The generator's own unit test (`scenario-settings-table`) checks the *same*
# thing without the build: it drives the generator against a **copy** in a
# temporary directory, so it fails even in a run that regenerates the file
# first — the bug the check below could not catch on its own.
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
    include_directories('payload/game/include'),
    include_directories('overlay/include'),
  ],
  dependencies : [control_host_dep, settings_table_dep],
)
test('control-core', control_core_test)

# The live rage objective's **pure rules** (the objective's names and values,
# the seed policy's names, the weapon's spread pattern) — a pure header, so the
# test compiles it alone. The mappings used to live inline in `service.cpp`,
# where no host test can reach them: the Python scenario suite drives a fake
# pipe and never runs the service, so a test written there would be testing the
# fake pipe. The header is the same pattern as the chams' target rules
# (`cs2/features/hook_target.hpp`).
control_rage_objective_test = executable('control_rage_objective_test',
  files(
    'payload/control/tests/test_control_rage_objective.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
)
test('control-rage-objective', control_rage_objective_test,
  # The last check is a **source** check: the service needs the game, so the
  # wiring (the call sites use the rules, they do not re-implement them) is
  # checked by reading the service's own source.
  args : [meson.current_source_dir()],
)

# The ragebot's settings **ownership rule** (T40): the pure comparison that
# decides whether the operator changed a rage field, the wire's rage group as a
# snapshot, and the field names the status reports. The rule is pure, so the
# test compiles the header alone; it reads `wire.hpp` to prove the comparison
# covers every rage field the wire owns, and it reads the payload's entry point
# and the service to prove the wiring (the sink consults the rule before it
# applies, and the service applies only the fields the write carries).
control_rage_settings_test = executable('control_rage_settings_test',
  files(
    'payload/control/tests/test_control_rage_settings.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
    include_directories('overlay/include'),
  ],
)
test('control-rage-settings', control_rage_settings_test,
  args : [meson.current_source_dir()],
)

# The per-weapon rage profile's **line codec** (`cs2/control/rage_config.hpp`):
# the text format of `cs2-rage.cfg` is a property of the file, not of the file
# system, so the codec is pure and the host test drives it — the round trip
# (save -> load) with the Monte-Carlo objective, and the refusal of an unknown
# objective byte. The file's own I/O is Windows-only and unchanged.
control_rage_config_test = executable('control_rage_config_test',
  files(
    'payload/control/tests/test_control_rage_config.cpp',
    'payload/control/src/rage_config.cpp',
    'payload/game/src/log.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
    include_directories('payload/aim/include'),
    include_directories('payload/game/include'),
  ],
  dependencies : control_host_dep,
)
test('control-rage-config', control_rage_config_test)

# The auto-strafe's decision maths (the W/A/S/D travel heading, the side's
# dead zone and hysteresis, and the `AirAccelerate` optimum) — a pure,
# dependency-free header, built natively so the strafe's maths has a host
# regression test (the module itself is header-only, so no source is listed).
control_strafe_test = executable('control_strafe_test',
  files(
    'payload/control/tests/test_control_strafe.cpp',
  ),
  dependencies : control_host_dep,
)
test('control-strafe', control_strafe_test)

# The anti-aim's **decision maths** (the fake yaw, the pitch clamp, the
# per-command jitter and the **movement re-base**) — a pure, dependency-free
# header, built natively so the re-base has a host regression test. The test
# takes the repository root: its last check is a **source** check that the
# payload never writes the camera's live view angles (`CCSGOInput + 0x688`) —
# the anti-aim writes the command's own angles, so the camera cannot follow.
control_anti_aim_test = executable('control_anti_aim_test',
  files(
    'payload/control/tests/test_control_anti_aim.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
  ],
)
test('control-anti-aim', control_anti_aim_test,
  args : [meson.current_source_dir()],
)

# The input backend's **button gate** and the ragebot's auto-stop counter-wish
# (both pure functions in `cs2/control/strafe_geometry.hpp`). The gate is what
# decides whether a feature's button reaches the command: the triggerbot used
# to go through the control channel's backend-gated mask, so in the default
# `console` backend it decided to fire and the gun stayed silent.
control_input_test = executable('control_input_test',
  files(
    'payload/control/tests/test_control_input.cpp',
  ),
  dependencies : control_host_dep,
)
test('control-input', control_input_test)

# The prediction's **copy** of the engine's movement services
# (`cs2/control/prediction_copy.hpp`). The payload is manually mapped, so the
# game's threads have no TLS block for the image: a `thread_local` copy
# resolved against a block that was never installed, the fault-safe reader
# answered "false", and the prediction was silently off
# (`docs/re/prediction.md` §"the TLS defect"). The copy is the caller's
# buffer now. The test drives it against a fake engine object and reads the
# repository's own sources: the module must not carry a `thread_local`.
control_prediction_test = executable('control_prediction_test',
  files(
    'payload/control/tests/test_control_prediction.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
  ],
)
test('control-prediction', control_prediction_test,
  args : [meson.current_source_dir()],
)

# The **local animfix**'s decision logic (which graph-input slots the animfix
# owns and the validity rule that stops a stale capture from being written
# over a fresh prediction) - a pure header, so the test compiles it alone.
# `docs/re/animfix.md` §9.
control_animfix_test = executable('control_animfix_test',
  files(
    'payload/control/tests/test_control_animfix.cpp',
  ),
  include_directories : [
    include_directories('payload/control/include'),
  ],
  # The animfix's test runs the repository's own inline relocator over the
  # seams' prologues: they must be hookable (and the parameter setter's must
  # not be), or the feature would be silently inert.
  dependencies : hooks_dep,
)
test('control-animfix', control_animfix_test,
  # The seam test reads the dumper's own source, so the pinned prologues and
  # the signatures the payload resolves cannot drift apart.
  args : [meson.current_source_dir()],
)

# The settings **sink** (the payload's entry point) must hand the features the
# clamped configuration: it used to read the wire fields directly, so a control
# client's `settings set` could put an out-of-range value into the engine's
# camera-distance field. The sink needs the game, so the check is a source
# check.
settings_sink_test = executable('settings_sink_test',
  files(
    'payload/tests/test_settings_sink.cpp',
  ),
)
test('settings-sink', settings_sink_test,
  args : [meson.current_source_dir()],
)

# The triggerbot's decision maths (the angle gate, the hitchance gate, the head
# preference, the reaction delay and the "already firing" latch) — a pure
# header, so the test compiles the header alone.
control_trigger_test = executable('control_trigger_test',
  files(
    'payload/control/tests/test_control_trigger.cpp',
  ),
  dependencies : control_host_dep,
)
test('control-trigger', control_trigger_test)

# The ESP's out-of-module providers (the sound events and the per-player
# visibility cache). Their host-testable parts are header-only, so the test
# compiles the event ring next to them.
control_sound_test = executable('control_sound_test',
  files(
    'payload/control/tests/test_control_sound.cpp',
    'payload/control/src/event_ring.cpp',
  ),
  include_directories : [
    include_directories('payload/game/include'),
    include_directories('payload/aim/include'),
    # The control service now reaches the shared settings block (the generated
    # name table lives next to it), so the host build needs that include path.
    include_directories('overlay/include'),
  ],
  # `cs2/control/service.hpp` reaches `cs2/control/input.hpp`, which holds the
  # local animfix's inline hooks - so the host build needs the hook engine's
  # include path (and its library, for the destructor).
  dependencies : [control_host_dep, hooks_dep],
)
test('control-sound', control_sound_test)

# The payload's fault guard (`cs2::diag::call_guarded`).
#
# The guard is the payload's only safety net for an engine call that faults,
# and what it does depends on the **compiler**: a `noexcept` vectored handler
# is not `nounwind` -- clang-cl gives its frame a C++ exception-handler unwind
# record whose cleanup action is `std::terminate`, and the `longjmp` out of
# that frame runs the action, so the process dies with 0xC0000409. That is
# clang-cl's behaviour -- clang-cl is the compiler the payload ships with --
# and not MSVC's: under `cl.exe` the same source recovers.
#
# **How the test is run.** The test is registered in **two** builds, and both
# of them have to be run:
#
#   meson test -C build          # this copy (the host build)
#   meson test -C build-payload  # the payload's own clang-cl build
#
# The host copy's behavioural half is only meaningful in a **clang-cl host
# build**: with `cl.exe` the guard recovers either way, so the half cannot see
# the defect. This repository's host build is clang-cl (CI sets
# `CC=clang-cl`/`CXX=clang-cl`), which is why the host copy is worth
# registering -- and why the defect survived: **there was no test at all**,
# not because the compiler was wrong.
#
# The payload's own copy (`payload/diag/meson.build`) is the one that ships,
# and it is registered in the payload's build whether or not that build is a
# cross build -- the test runs wherever the build machine can run the
# binaries.
diag_guard_test = executable('diag_guard_test',
  files(
    'payload/diag/tests/test_diag_guard.cpp',
    'payload/diag/src/crash_handler.cpp',
    'payload/diag/src/text.cpp',
    'payload/game/src/log.cpp',
    'payload/game/src/module.cpp',
    'payload/game/src/memory.cpp',
  ),
  include_directories : [
    include_directories('payload/diag/include'),
    include_directories('payload/game/include'),
    core_inc,
  ],
)
test('diag-guard', diag_guard_test,
  # The source check reads the payload's own sources.
  args : [meson.current_source_dir() / 'payload'],
)

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
    include_directories('overlay/include'),
    include_directories('payload/game/include'),
    include_directories('payload/render/include'),
  ],
  dependencies : features_host_dep,
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

# The third-person camera's re-entrancy guard (`cs2/features/recursion_guard.hpp`).
# The guard is what keeps the camera hook from killing the game: the detour
# runs on the game's own camera thread, in a manually mapped image where a
# `thread_local` resolves against a TLS block that was never installed — the
# read faults inside the payload (see `docs/re/thirdperson.md`). The guard is
# a pure header, so the host test drives it directly; the test also reads the
# repository's own sources and fails when a `thread_local` comes back.
features_thirdperson_guard_test = executable('features_thirdperson_guard_test',
  files(
    'payload/features/tests/test_thirdperson_guard.cpp',
  ),
  include_directories : [
    include_directories('payload/features/include'),
  ],
)
test('features-thirdperson-guard', features_thirdperson_guard_test,
  args : [meson.current_source_dir()],
)

# The chams' draw-hook **target guard**: the rule that decides whether a
# function entry may be patched. It is a pure function (the bytes are an
# argument), so the host build can prove it: the right prologue is accepted, a
# changed, a truncated or an unreadable one is refused, and a missing offset
# is refused. The previous target came from an RTTI *lambda* type and took the
# game down with an access violation, so the rule needs a test of its own.
features_chams_target_test = executable('features_chams_target_test',
  files(
    'payload/features/tests/test_chams_target.cpp',
  ),
  include_directories : [
    include_directories('payload/features/include'),
  ],
)
test('features-chams-target', features_chams_target_test)

# The chams' draw detour's **re-entrancy guard** and its **status line**. The
# scene system renders its draw list in jobs on the tier0 thread pool, so two
# threads can be inside the detour at once: a process-wide depth counter read
# the other job's draw as a recursion and **dropped** it (a hole in the model,
# in silence). The guard is per owner (the thread id) and is shared with the
# third-person camera's hook, so the tree holds exactly one implementation of
# it. Both headers are pure (no engine, no Windows), so the host test drives
# the whole decision; the test also reads the repository's own sources and
# fails when the detour stops holding the shared guard, when a second
# implementation appears, when a `thread_local` comes back, or when the
# status line starts allocating on the game's render thread.
#
# The status line's **formatting** is a pure function in the chams' header,
# so the test drives it with the counters at their 64-bit maximum and checks
# that the line still fits the overlay's 192-byte HUD buffer.
features_chams_draw_hook_test = executable('features_chams_draw_hook_test',
  files(
    'payload/features/tests/test_chams_draw_hook.cpp',
  ),
  include_directories : [
    include_directories('payload/features/include'),
    include_directories('payload/game/include'),
    include_directories('payload/render/include'),
    include_directories('payload/hooks/include'),
  ],
)
test('features-chams-draw-hook', features_chams_draw_hook_test,
  args : [meson.current_source_dir()],
)

# The desc cache's **concurrency probe** (T33). The scene system renders the
# draw list in jobs on the tier0 thread pool, so two threads are inside the
# draw detour at once, and the desc cache has to be safe for two writers: the
# old protocol (three atomics, the desc published last) could leave a reader
# with desc A and entity B, and the chams were decided on the wrong player.
# The probe drives the **shipped** cache (`cs2::features::DescCache`) with
# four threads and two descs that collide in one slot, and it must report
# zero mismatches.
features_chams_desc_cache_test = executable('features_chams_desc_cache_test',
  files(
    'payload/features/tests/test_chams_desc_cache.cpp',
  ),
  include_directories : [
    include_directories('payload/features/include'),
    include_directories('payload/game/include'),
    include_directories('payload/render/include'),
    include_directories('payload/hooks/include'),
  ],
)
test('features-chams-desc-cache', features_chams_desc_cache_test)

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
    include_directories('payload/game/include'),
    include_directories('payload/render/include'),
    include_directories('payload/third_party/imgui'),
  ],
  dependencies : features_host_dep,
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

# --- The Python scenario suite ---------------------------------------------
# The scenario library (`scripts/scenario`) is the Python half of the control
# harness (ADR-0020) and it has its own test suite. The suite is pure logic --
# no game, no payload, no network -- so it belongs to the same `meson test`
# run as the C++ host tests, one test per suite (the shape the C++ side uses:
# each suite is registered separately, so a failure names the area).
#
# A machine without Python (or without the optional MCP SDK) must not break
# the build:
#   * the interpreter is optional -- without it the suites are not registered
#     at all, and the configure says so;
#   * a suite that cannot import what it needs exits with the automake "skip"
#     code (77), which Meson reports as SKIP rather than FAIL.
python = find_program('python', 'python3', required : false)
if python.found()
  scenario_runner = meson.current_source_dir() / 'scripts' / 'scenario' / 'tests' / 'run_suite.py'
  # [the test's name, the suite's module] -- one test per suite.
  scenario_suites = [
    ['protocol', 'test_protocol'],
    ['screens', 'test_screens'],
    ['state-machine', 'test_state_machine'],
    ['state', 'test_state'],
    ['control', 'test_control'],
    ['awaiting', 'test_awaiting'],
    ['bot-match', 'test_bot_match'],
    ['session', 'test_session'],
    ['settings', 'test_settings'],
    ['heatmap', 'test_heatmap'],
    ['anti-aim', 'test_anti_aim'],
    ['animfix', 'test_animfix'],
    ['registry', 'test_registry'],
    ['mcp', 'test_mcp'],
    ['settings-table', 'test_settings_table'],
    ['sdk-generator', 'test_sdk_generator'],
    ['clang-tidy-fix', 'test_clang_tidy_fix'],
    ['resolve-offsets-offline', 'test_resolve_offsets_offline'],
  ]
  foreach suite : scenario_suites
    test('scenario-' + suite[0], python,
      args : [scenario_runner, suite[1]],
      workdir : meson.current_source_dir(),
      timeout : 300,
    )
  endforeach

  # The type gate: mypy over the scenario package (`scripts/typecheck.py`).
  # It exits non-zero on a type error, and with the "skip" code when the
  # checker is not installed -- so it is a gate on a machine that has the
  # dev dependencies and a no-op on a machine that has not.
  test('scenario-typecheck', python,
    args : [meson.current_source_dir() / 'scripts' / 'typecheck.py'],
    workdir : meson.current_source_dir(),
    timeout : 900,
  )
else
  message('python not found: the scenario test suites are skipped')
endif

# Quick self-check so `meson test` is meaningful from day one.
test('host-probe', host_probe)
