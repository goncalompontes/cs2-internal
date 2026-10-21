cmake_minimum_required(VERSION 3.20)
project(Cs2Protect CXX)

# The plugin needs LLVM's own CMake package: it includes LLVM headers and
# links the LLVM component libraries. Meson has no usable LLVM dependency
# (`dependency('llvm')` needs CMake on PATH anyway and buys nothing), which is
# why this one tool of the repository is a CMake project. It is a *build-time
# tool*, not a project artifact, so ADR-0003 (Meson + Ninja) governs the
# project and the payload, not this glue.
find_package(LLVM REQUIRED CONFIG)
message(STATUS "LLVM ${LLVM_PACKAGE_VERSION} from ${LLVM_DIR}")

list(APPEND CMAKE_MODULE_PATH "${LLVM_CMAKE_DIR}")
include(AddLLVM)

# `add_llvm_pass_plugin` creates a MODULE library, and CMake's MODULE type is
# not supported by the MSVC toolchain on Windows ("Loadable modules not
# supported on this platform"). A Windows pass plugin therefore has to be a
# SHARED library (a .dll) that exports `llvmGetPassPluginInfo`.
add_library(Cs2Protect SHARED
  src/plugin.cpp
  src/config.cpp
  src/selection.cpp
  src/report.cpp
  src/strings.cpp
  src/transform.cpp
)

# --- The DIA SDK ------------------------------------------------------------
# The official release's exported CMake targets hardcode the *build* machine's
# DIA SDK path (".../Visual Studio/2022/Enterprise/DIA SDK/..."), which does
# not exist here. CMake resolves an imported target's link interface when it
# generates the link line, so the property itself has to be rewritten --
# filtering the list we hand to target_link_libraries is not enough.
get_filename_component(REAL_DIA_DIR "" ABSOLUTE)
if(DEFINED ENV{VSINSTALLDIR} AND NOT "$ENV{VSINSTALLDIR}" STREQUAL "")
  get_filename_component(REAL_DIA_DIR "$ENV{VSINSTALLDIR}/DIA SDK/lib/amd64" ABSOLUTE)
else()
  file(GLOB _dia_candidates
    "C:/Program Files/Microsoft Visual Studio/*/DIA SDK/lib/amd64"
    "C:/Program Files (x86)/Microsoft Visual Studio/*/DIA SDK/lib/amd64")
  foreach(_candidate IN LISTS _dia_candidates)
  if(EXISTS "${_candidate}/diaguids.lib")
    set(REAL_DIA_DIR "${_candidate}")
    break()
  endif()
  endforeach()
endif()
if(NOT EXISTS "${REAL_DIA_DIR}/diaguids.lib")
  message(WARNING "the DIA SDK was not found (looked in '${REAL_DIA_DIR}'); "
                  "the link may fail with 'diaguids.lib missing'")
endif()

get_property(_llvm_imported DIRECTORY PROPERTY IMPORTED_TARGETS)
list(APPEND _llvm_imported LLVMDebugInfoPDB)
foreach(_llvm_tgt IN LISTS _llvm_imported)
  if(TARGET ${_llvm_tgt})
    get_target_property(_libs ${_llvm_tgt} INTERFACE_LINK_LIBRARIES)
    if(_libs AND NOT _libs MATCHES "-NOTFOUND$")
      set(_new_libs "")
      foreach(_l IN LISTS _libs)
        if(_l MATCHES "DIA SDK")
          if(EXISTS "${REAL_DIA_DIR}/diaguids.lib")
            list(APPEND _new_libs "${REAL_DIA_DIR}/diaguids.lib")
          endif()
        else()
          list(APPEND _new_libs "${_l}")
        endif()
      endforeach()
      set_target_properties(${_llvm_tgt} PROPERTIES
        INTERFACE_LINK_LIBRARIES "${_new_libs}")
    endif()
  endif()
endforeach()
get_target_property(_check LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
message(STATUS "LLVMDebugInfoPDB interface after patch: ${_check}")

# --- The LLVM libraries -----------------------------------------------------
llvm_map_components_to_libnames(LLVM_LIBS
  core support passes analysis transformutils irreader asmparser)
set(LLVM_LIBS_FIXED "")
foreach(_l IN LISTS LLVM_LIBS)
  if(_l MATCHES "DIA SDK")
    if(EXISTS "${REAL_DIA_DIR}/diaguids.lib")
      list(APPEND LLVM_LIBS_FIXED "${REAL_DIA_DIR}/diaguids.lib")
    endif()
  else()
    list(APPEND LLVM_LIBS_FIXED "${_l}")
  endif()
endforeach()
target_link_libraries(Cs2Protect PRIVATE ${LLVM_LIBS_FIXED})

set_target_properties(Cs2Protect PROPERTIES PREFIX "" OUTPUT_NAME "Cs2Protect")

# --- The compile flags ------------------------------------------------------
# The release LLVM is built without RTTI and with the static CRT, and the
# compiler that loads the plugin (clang-cl) is built the same way: a plugin
# with a different CRT would allocate and free across two heaps. clang-cl
# spells the flags `/GR-` and `/MT`; the `-fno-rtti` and
# `-fms-runtime-lib=static` spellings are rejected by clang-cl ("unknown
# argument ignored in clang-cl").
set(CS2_PROTECT_CXX_STANDARD cxx_std_23)
set(CS2_PROTECT_COMPILE_OPTIONS "")
set(CS2_PROTECT_DEFINITIONS "")
if(MSVC)
  set(CS2_PROTECT_COMPILE_OPTIONS /W4 /GR- /EHs-c- /MT)
else()
  set(CS2_PROTECT_COMPILE_OPTIONS -Wall -Wextra -fno-rtti)
endif()

target_compile_features(Cs2Protect PRIVATE ${CS2_PROTECT_CXX_STANDARD})
target_include_directories(Cs2Protect SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_compile_definitions(Cs2Protect PRIVATE ${LLVM_DEFINITIONS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND "${LLVM_DEFINITIONS}")
target_compile_options(Cs2Protect PRIVATE ${LLVM_DEFINITIONS_LIST})
target_compile_options(Cs2Protect PRIVATE ${CS2_PROTECT_COMPILE_OPTIONS})

# --- The tests --------------------------------------------------------------
# The plugin is a build-time tool, so its tests live with it and run under
# CTest. They cover the policy reader (a malformed policy must be a reported
# error, never a crash), the qualified name and the selection.
enable_testing()
add_executable(cs2-protect-tests
  tests/protect_tests.cpp
  src/config.cpp
  src/selection.cpp
  src/report.cpp
  src/strings.cpp
  src/transform.cpp
)
target_compile_features(cs2-protect-tests PRIVATE ${CS2_PROTECT_CXX_STANDARD})
target_include_directories(cs2-protect-tests SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
target_include_directories(cs2-protect-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(cs2-protect-tests PRIVATE ${LLVM_DEFINITIONS})
target_compile_options(cs2-protect-tests PRIVATE ${LLVM_DEFINITIONS_LIST})
target_compile_options(cs2-protect-tests PRIVATE ${CS2_PROTECT_COMPILE_OPTIONS})
target_link_libraries(cs2-protect-tests PRIVATE ${LLVM_LIBS_FIXED})
add_test(NAME protect-tests COMMAND cs2-protect-tests)

# --- The IR-level test and the differential test -----------------------------
# The pass rewrites calls, so a mistake shows up as a *wrong call target*,
# not as a crash. The two tests below are the ones that catch it:
#
#   * protect-ir-test runs `opt` over a checked-in fixture and asserts on the
#     shape of the rewritten IR (the direct call is gone, an indirect call is
#     there, the function type is unchanged, the descriptor is encrypted, and
#     the calls that must be skipped are untouched);
#   * protect-diff-test builds the same fixture twice -- once with the pass
#     and once without -- runs both and compares their output and their exit
#     code.
#
# The tests need the SDK's `opt` and `clang-cl`, so they use the paths the
# LLVM package hands down.
find_program(CS2_PROTECT_PYTHON NAMES python python3 REQUIRED)

add_test(NAME protect-ir-test
  COMMAND ${CS2_PROTECT_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ir_test.py
          --opt ${LLVM_TOOLS_BINARY_DIR}/opt.exe
          --plugin $<TARGET_FILE:Cs2Protect>
          --fixture ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/calls.ll
          --policy ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/calls.policy.json
          --work ${CMAKE_CURRENT_BINARY_DIR}/ir-test)

add_test(NAME protect-diff-test
  COMMAND ${CS2_PROTECT_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/diff_test.py
          --clang-cl ${LLVM_TOOLS_BINARY_DIR}/clang-cl.exe
          --objdump ${LLVM_TOOLS_BINARY_DIR}/llvm-objdump.exe
          --plugin $<TARGET_FILE:Cs2Protect>
          --fixture ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/diff_fixture.cpp
          --policy ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/diff.policy.json
          --work ${CMAKE_CURRENT_BINARY_DIR}/diff-test)

# The readers of the coverage: a malformed file has to be reported and
# ignored (never a traceback), and the coverage of another tree has to be
# skipped (it would count every rewrite twice).
add_test(NAME protect-coverage-test
  COMMAND ${CS2_PROTECT_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/tests/coverage_test.py)
