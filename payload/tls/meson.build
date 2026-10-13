# Per-thread TLS bootstrap for the manually-mapped payload. Windows-only logic,
# built into the payload and stubbed for host builds.
tls_inc = include_directories('include')

tls_lib = static_library('cs2_tls',
  files('src/bootstrap.cpp'),
  include_directories : tls_inc,
)

tls_dep = declare_dependency(
  link_with : tls_lib,
  include_directories : tls_inc,
)
