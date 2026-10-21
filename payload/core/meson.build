# The shared RAII handle (cs2/core/handle.hpp).
#
# WHY A SEPARATE MODULE
#     The handle wrapper is used by every module that owns an OS handle (the
#     payload's control channel, its config store, its model publisher, the
#     diagnostics and the overlay's transport), and it has to be usable from
#     the overlay and the injector too -- separate Meson projects that do not
#     share a dependency graph with the payload. A header-only module of its
#     own is the one place all of them can include.
#
# WHAT IT IS NOT
#     It is not a Windows header: the wrapper itself is platform-independent
#     (so the ownership logic has a host test), and the Windows policy lives
#     in cs2/core/win_handle.hpp, which only the Windows sources include.
#
# The module has no sources: it is headers plus the include directory.

core_inc = include_directories('include')

core_dep = declare_dependency(
  include_directories : core_inc,
)
