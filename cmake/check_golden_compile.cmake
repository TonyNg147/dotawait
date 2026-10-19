# Compiles every golden *.expected.cpp with -fsyntax-only against the real
# runtime headers, so a fixture proves the lowering works, not only that the
# text is stable (golden_tests compares bytes and nothing else).
#
# Run as a script; it is handed paths and knows nothing about either
# component, so transpiler/ and runtime/ stay independent:
#   cmake -DCXX=<compiler> -DRUNTIME_INCLUDE=<dir> -DGOLDEN_DIR=<dir>
#         -P check_golden_compile.cmake
#
# Fails loudly: an empty fixture list (e.g. a renamed directory) is an error,
# and every failing fixture is reported with its compiler stderr. No
# -Wall/-Werror: fixtures may legitimately have unused parameters.

foreach(var CXX RUNTIME_INCLUDE GOLDEN_DIR)
  if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
    message(FATAL_ERROR "check_golden_compile: -D${var}=... is required")
  endif()
endforeach()

file(GLOB fixtures "${GOLDEN_DIR}/*.expected.cpp")
list(SORT fixtures)
list(LENGTH fixtures fixture_count)
if(fixture_count EQUAL 0)
  message(FATAL_ERROR "check_golden_compile: no golden fixtures found in ${GOLDEN_DIR}")
endif()

set(failures "")
foreach(fixture IN LISTS fixtures)
  execute_process(
    COMMAND "${CXX}" -std=c++20 -fsyntax-only "-I${RUNTIME_INCLUDE}" "${fixture}"
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE output)
  if(NOT result EQUAL 0)
    string(APPEND failures "\n--- ${fixture} (exit ${result}) ---\n${output}")
  endif()
endforeach()

if(NOT failures STREQUAL "")
  message(FATAL_ERROR "golden_compile: fixtures failed to compile:${failures}")
endif()
message(STATUS "golden_compile: ${fixture_count} fixtures OK")
