# Control channel (ADR-0020): a Win32 named pipe carrying newline-delimited
# JSON, plus the in-process `exec` primitive (docs/re/engine2.md). Windows-only
# logic; json/predicate/snapshot_json are also built host-side for their tests
# (see the root meson.build).
#
# `include/cs2/control/strafe_geometry.hpp` is the auto-strafe's decision
# maths: a header-only, dependency-free module (no engine headers, no
# `<windows.h>`) that the host test `control-strafe` (root meson.build)
# compiles on its own.
#
# `include/cs2/control/anti_aim.hpp` is the anti-aim's decision maths — the
# fake yaw, the pitch clamp, the per-command jitter and the **movement
# re-base** (the projection of the player's intent from the camera frame into
# the anti-aim frame). It is header-only and dependency-free in the same way,
# and the host test `control-anti-aim` (root meson.build) compiles it on its
# own.
#
# The overlay's `include/` is on the path because `cs2/overlay/wire.hpp` is the
# **shared POD contract** between the payload and the Vulkan layer (the
# settings block the `settings` command reads and writes), not a layering
# violation.
control_inc = include_directories('include')
overlay_inc = include_directories('../../overlay/include')

control_lib = static_library('cs2_control',
  files(
    'src/json.cpp',
    'src/predicate.cpp',
    'src/snapshot_json.cpp',
    'src/event_ring.cpp',
    'src/events.cpp',
    'src/input.cpp',
    'src/console.cpp',
    'src/channel.cpp',
    'src/occlusion_query.cpp',
    'src/rage_config.cpp',
    'src/settings_access.cpp',
    'src/trigger.cpp',
    'src/service.cpp',
  ),
  include_directories : [control_inc, overlay_inc, core_inc],
  dependencies : [game_dep, aim_dep, diag_dep, tls_dep, sdk_dep, hooks_dep],
)

control_dep = declare_dependency(
  link_with : control_lib,
  include_directories : [control_inc, overlay_inc, core_inc],
  dependencies : [game_dep, aim_dep, diag_dep, tls_dep, sdk_dep, hooks_dep],
)
