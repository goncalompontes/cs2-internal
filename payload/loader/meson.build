# Platform-independent manual-map core. Included by both the host project
# (native, runs tests) and the payload project (cross, builds the DLL).

loader_inc = include_directories('include')

loader_lib = static_library('cs2_loader',
  files('src/pe.cpp', 'src/mapping.cpp'),
  include_directories : loader_inc,
)

loader_dep = declare_dependency(
  link_with : loader_lib,
  include_directories : loader_inc,
)

if not meson.is_cross_build()
  loader_fixture_args = '-DCS2_FIXTURE_DIR="' + meson.current_source_dir() + '/tests/fixtures"'

  loader_test_pe = executable('loader_test_pe', 'tests/test_pe.cpp',
    dependencies : loader_dep,
    cpp_args : loader_fixture_args,
  )
  test('loader-pe', loader_test_pe)

  loader_test_mapping = executable('loader_test_mapping', 'tests/test_mapping.cpp',
    dependencies : loader_dep,
    cpp_args : loader_fixture_args,
  )
  test('loader-mapping', loader_test_mapping)
endif
