# cs2-payload — the Windows DLL.
#
# This is a SEPARATE Meson project (its own root) so it can be configured with
# a Windows cross file while the rest of the repo builds natively. Configure:
#
#   meson setup build-payload payload --cross-file toolchain/windows-msvc.ini
#   meson compile -C build-payload
#
# See docs/architecture.md and toolchain/README.md.

project('cs2-payload', 'c', 'cpp',
  version : '0.1.0',
  meson_version : '>=1.3',
  default_options : [
    'warning_level=3',
    'buildtype=debugoptimized',
  ])

# C++23 dialect (see the root meson.build): the spellings avoid Meson's
# "use the built-in cpp_std option" warning (see the root file for details).
cpp = meson.get_compiler('cpp')
if cpp.get_id() == 'msvc' or cpp.get_id() == 'clang-cl'
  add_project_arguments('-std:c++latest', language : 'cpp')
else
  add_project_arguments('--std=c++23', language : 'cpp')
endif

# --- Build provenance -----------------------------------------------------
# Distinguish a *native* Windows build (host == build == windows) from a cross
# build produced on Linux with msvc-wine. The define is consumed by
# src/payload_main.cpp (boot banner + marker file) and features/src/overlay.cpp.
# The native Windows build has now been validated in-game (see
# docs/handoff.md); the historic "untested" marker was removed 2026-10.
native_windows = host_machine.system() == 'windows' and build_machine.system() == 'windows'
if native_windows
  add_project_arguments('-DCS2_NATIVE_WINDOWS_BUILD=1', language : ['c', 'cpp'])
  message('cs2-internal: native Windows build')
elif host_machine.system() == 'windows'
  add_project_arguments('-DCS2_CROSS_WINDOWS_BUILD=1', language : ['c', 'cpp'])
  message('cs2-internal: cross Windows build (build machine: ' + build_machine.system() + ')')
endif

# --- The protection pass ----------------------------------------------------
# The LLVM pass plugin that selects the functions to protect. It is a
# build-time tool, not a project artifact, so it is built by CMake (see
# tools/protect/README.md); this project only loads it.
#
# The flag is `/clang:-fpass-plugin=<dll>`: clang-cl ignores the plain
# `-fpass-plugin=` spelling ("unknown argument ignored in clang-cl"), and the
# `/clang:` prefix routes the option to the clang driver.
#
# The arguments have to be declared before the first target, so this block
# sits above `subdir('loader')`.
fs = import('fs')
protect_enabled = get_option('protect')
protect_plugin = get_option('protect_plugin')
protect_build_depends = []
protect_link_depends = []

if protect_enabled
  # The plugin is loaded with `/clang:-fpass-plugin=`, which only the clang
  # driver understands: the *compiler* is what matters here, not the machine
  # the build runs on (the repo cross-builds the payload from Linux with
  # clang-cl, see toolchain/windows-msvc.ini).
  if cpp.get_id() != 'clang-cl' and cpp.get_id() != 'clang'
    error('the protection pass needs the clang-cl compiler: the plugin is loaded with ' +
          '/clang:-fpass-plugin=..., which only the clang driver understands (this build ' +
          'uses ' + cpp.get_id() + ')')
  endif
  if protect_plugin == ''
    error('-Dprotect=true needs -Dprotect_plugin=<path to Cs2Protect.dll>')
  endif
  if not fs.is_absolute(protect_plugin)
    error('the plugin path must be absolute: ' + protect_plugin)
  endif
  if ',' in protect_plugin
    error('the plugin path must not contain a comma (it is one compiler argument): ' + protect_plugin)
  endif
  if ' ' in protect_plugin
    error('the plugin path must not contain a space (it is one compiler argument): ' + protect_plugin)
  endif

  protect_tool = meson.current_source_dir() / '..' / 'tools' / 'protect'
  protect_verify = find_program(protect_tool / 'verify_plugin.py')
  protect_check = find_program(protect_tool / 'check_policy.py')
  protect_payload_check = find_program(protect_tool / 'check_payload.py')
  protect_plugin_file = files(protect_plugin)
  protect_plugin_digest = fs.hash(protect_plugin_file, 'sha256')

  # Ninja does not track a DLL that is only mentioned inside a compiler flag,
  # so a plugin that changed after the configure step would leave stale
  # objects behind -- a wrong-results failure mode with no error. The
  # verification target turns that into a build failure, and the digest in the
  # command line makes the objects rebuild once the configure step was
  # repeated.
  protect_stamp = custom_target('protect-plugin',
    input : protect_plugin_file,
    output : 'Cs2Protect.stamp',
    command : [protect_verify,
               '--plugin', '@INPUT@',
               '--expected-sha256', protect_plugin_digest,
               '--output', '@OUTPUT@'],
    depend_files : protect_plugin_file,
    build_by_default : true,
  )
  protect_build_depends = [protect_stamp]
  protect_link_depends = [protect_plugin_file]

  # The policy file is read by the plugin at compile time, from the
  # environment. Its digest goes into the command line, so that a policy
  # change (after a reconfigure) rebuilds the objects: the plugin has to run
  # again to write the coverage of the new policy.
  #
  # A protected build without a policy protects nothing, and that is
  # indistinguishable from an unprotected build -- so the configure step
  # fails instead. (The plugin itself stays a no-op without a policy: it is
  # the wiring that knows that `protect` is on.)
  protect_policy_args = []
  policy_probe = run_command(
    find_program('python'), '-c',
    'import os, sys; sys.stdout.write(os.environ.get("CS2_PROTECT_CONFIG", ""))',
    check : false)
  if policy_probe.returncode() != 0
    error('-Dprotect=true needs CS2_PROTECT_CONFIG: the configure step could not read the environment')
  endif
  protect_policy = policy_probe.stdout().strip()
  if protect_policy == ''
    error('-Dprotect=true needs a policy file: set CS2_PROTECT_CONFIG=<path to a policy ' +
          'JSON file> (see tools/protect/policy.example.json). A protected build without ' +
          'a policy protects nothing, and that is indistinguishable from an unprotected one.')
  endif
  if not fs.exists(protect_policy)
    error('the policy file does not exist: ' + protect_policy)
  endif

  # The seed is **mandatory**. The protection's constants, its keys and its
  # layout come from it, and the fallback (a constant of the payload: an
  # FNV-1a over the module id) is what makes two builds without a seed byte
  # for byte the same file. A protected build without a seed is therefore
  # the same failure as a protected build without a policy -- a constant
  # image that looks protected -- and the configure step fails for it the
  # same way.
  #
  # The plugin refuses to run without a seed as well
  # (tools/protect/src/config.cpp): the wiring catches it here, where the
  # message can name both sources.
  #
  # The rule is the **plugin's**, and the probe that applies it is the
  # policy checker's (`--seed`): there is one reader of the policy, and the
  # configure step fails with the plugin's own message -- a malformed
  # policy is the plugin's error to report, and it is not hidden behind a
  # seed error.
  protect_seed_probe = run_command(protect_check, '--seed', check : false)
  if protect_seed_probe.returncode() == 1
    error('-Dprotect=true needs a build seed: set CS2_PROTECT_SEED=<64-bit hexadecimal> or ' +
          'put a "seed" in the policy file')
  elif protect_seed_probe.returncode() != 0
    error('-Dprotect=true: the seed or the policy is invalid: ' +
          protect_seed_probe.stderr().strip())
  endif
  protect_policy_digest = fs.hash(files(protect_policy), 'sha256')
  protect_policy_args = [
    '-DCS2_PROTECT_POLICY_SHA256=' + protect_policy_digest.substring(0, 16),
  ]
  protect_link_depends = protect_link_depends + [files(protect_policy)]

  # The image the transform produced has to be checked, and the check has to
  # be part of the build: a build that produces an image whose dispatches
  # are folded away, or whose mixer constants the backend mis-materialised,
  # has to fail -- not to look protected and be wrong.
  #
  # The check disassembles the image, so it needs the disassembler of the
  # LLVM SDK the plugin was built with: another LLVM may print the
  # instructions differently enough for the check to miss a dispatch.
  protect_objdump_probe = run_command(
    find_program('python'), protect_tool / 'find_objdump.py', check : false)
  if protect_objdump_probe.returncode() != 0
    error('the protected build needs the llvm-objdump of the LLVM SDK:\n' +
          protect_objdump_probe.stderr())
  endif
  protect_objdump = protect_objdump_probe.stdout().strip()

  # The tree that is being built. The coverage directory is keyed by the
  # policy, so two worktrees that share a policy share the directory: the
  # coverage of the other tree is not evidence about this build, and the
  # check has to be told which tree it is looking at.
  protect_source_root = meson.current_source_dir() / '..'

  # The unprotected image, when there is one: the checks that compare the two
  # images (the exports, the imports, and the "the unprotected image has no
  # dispatch") need it. Without it they are skipped, and the rest of the
  # checks still run.
  protect_reference = get_option('protect_reference')
  protect_reference_args = []
  if protect_reference != ''
    if not fs.exists(protect_reference)
      error('the unprotected reference image does not exist: ' + protect_reference)
    endif
    protect_reference_args = ['--unprotected', protect_reference]
  endif

  add_project_arguments(
    '/clang:-fpass-plugin=' + protect_plugin.replace('\\', '/'),
    '-DCS2_PROTECT_PLUGIN_SHA256=' + protect_plugin_digest.substring(0, 16),
    language : 'cpp',
  )
  add_project_arguments(protect_policy_args, language : 'cpp')
endif

subdir('loader')

# The shared RAII handle (cs2/core/handle.hpp). It must come before the
# modules that own an OS handle: they take its include directory (`core_inc`)
# from here.
subdir('core')

# --- Generated SDK headers ------------------------------------------------
# The payload consumes the generated SDK (payload/sdk). Generate it here too so
# a standalone payload build works; scripts/gen-sdk.py uses the calibration
# dumps and builds the host dumper on demand.
gen_sdk = find_program(meson.current_source_dir() / '..' / 'scripts' / 'gen-sdk.py')
sdk_dir = meson.current_source_dir() / 'sdk'
dump_dir = meson.current_source_dir() / '..' / 'calibration'

sdk_headers = custom_target('sdk-headers',
  output : 'sdk-headers.stamp',
  command : [gen_sdk, dump_dir, sdk_dir, '@OUTPUT@'],
  build_always_stale : true,
  console : true,
)

sdk_dep = declare_dependency(
  include_directories : include_directories('sdk'),
  sources : sdk_headers,
)

# --- The settings name table -------------------------------------------------
# The control channel's `settings` command addresses a setting by its flat wire
# field name, and the byte offset of a field is a property of the packed
# layout. The table is generated (`scripts/gen-settings-table.py`) and checked
# in; **both** builds verify it, so a table that no longer matches `wire.hpp`
# fails the build instead of reaching the binary.
#
# The payload had no such target: a payload-only build compiled against the
# checked-in table, so a `wire.hpp` change without a regenerated table wrote a
# neighbouring field's bytes (silent corruption, no error, no log line).
gen_settings_table = find_program(meson.current_source_dir() / '..' / 'scripts' / 'gen-settings-table.py')
settings_table = custom_target('settings-table',
  output : 'settings-table.stamp',
  command : [gen_settings_table,
             '--check',
             meson.current_source_dir() / '..' / 'overlay' / 'include' / 'cs2' / 'overlay' / 'wire.hpp',
             meson.current_source_dir() / '..' / 'overlay' / 'include' / 'cs2' / 'overlay' / 'settings_table.hpp',
             '@OUTPUT@'],
  build_always_stale : true,
  console : true,
)
settings_table_dep = declare_dependency(
  sources : settings_table,
)

if host_machine.system() != 'windows'
  message(
    'cs2-payload targets Windows. This configure produced no targets. ' +
    'Use --cross-file toolchain/windows-msvc.ini to build the DLL.')
else
  subdir('third_party/imgui')
  # kiero + MinHook were retired with ADR-0015 (the inline Present hook stalled
  # the game under DXVK). The swapchain is now interposed via its method table.
  # The vendored copy under third_party/kiero is kept for reference only.
  subdir('hooks')
  subdir('game')
  subdir('aim')
  subdir('diag')
  subdir('tls')
  subdir('render')
  subdir('features')
  subdir('control')

  # --- The protection pass --------------------------------------------------
  # The plugin was loaded above (the flag is a project argument, so every
  # translation unit of the payload gets it).
  payload_lib = shared_library('payload',
    'src/payload_main.cpp',
    name_prefix : '',   # payload.dll, not libpayload.dll
    install : false,
    include_directories : loader_inc,
    link_with : loader_lib,
    link_depends : protect_link_depends + protect_build_depends,
    dependencies : [sdk_dep, hooks_dep, game_dep, aim_dep, diag_dep, render_dep, features_dep,
                    control_dep, settings_table_dep])

  if protect_enabled
    # A pass sees one translation unit at a time, so it cannot know whether a
    # policy entry matched *somewhere* in the build. The plugin records what
    # each module decided; this check reads the records and fails the build
    # when an entry matched nothing.
    #
    # The policy is **not** passed on the command line: the checker reads it
    # from $CS2_PROTECT_CONFIG, exactly like the plugin does. Meson spells a
    # path with forward slashes, so the coverage directory is keyed by the
    # **normalised** path -- the separators are folded to one spelling on
    # both sides, because the checker and the plugin have to compute the
    # same directory, and only the environment gives them the same string.
    custom_target('protect-policy-check',
      output : 'protect-policy-check.stamp',
      command : [protect_check, '--output', '@OUTPUT@',
                 '--source-root', protect_source_root],
      depends : [payload_lib],
      build_always_stale : true,
      build_by_default : true,
      console : true,
    )

    # The check of the image itself: the constants of the mixer have to be in
    # the data of the image, the number of the encrypted dispatches has to be
    # the number of the calls the pass reports having rewritten, and every
    # dispatch has to decode to a callee of the image. A build that fails it
    # fails: the image it produced is not the one the pass meant to write.
    #
    # The coverage directory is not passed: the checker takes it from
    # $CS2_PROTECT_COVERAGE, else it computes the same default the plugin
    # does (the digest of the policy path).
    custom_target('protect-payload-check',
      output : 'protect-payload-check.stamp',
      command : [protect_payload_check,
                 '--protected', payload_lib.full_path(),
                 '--objdump', protect_objdump,
                 '--source-root', protect_source_root] + protect_reference_args,
      depends : [payload_lib],
      build_always_stale : true,
      build_by_default : true,
      console : true,
    )
  endif
endif
