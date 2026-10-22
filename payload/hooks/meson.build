# Hook engine (vtable + memory protection). Platform-independent; built natively
# for host tests and cross-compiled into the payload.
hooks_inc = include_directories('include')

hooks_lib = static_library('cs2_hooks',
  files('src/memory.cpp', 'src/vtable.cpp', 'src/inline.cpp'),
  include_directories : hooks_inc,
)

hooks_dep = declare_dependency(
  link_with : hooks_lib,
  include_directories : hooks_inc,
)

if not meson.is_cross_build()
  hooks_test = executable('hooks_test', 'tests/test_vtable.cpp',
    dependencies : hooks_dep,
  )
  test('hooks-vtable', hooks_test)

  inline_test = executable('inline_test', 'tests/test_inline.cpp',
    dependencies : hooks_dep,
  )
  test('hooks-inline', inline_test)
endif
