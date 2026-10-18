add_async_executable(hello_example hello.async.cpp)
add_async_executable(yielding_example yielding.async.cpp)

if(BUILD_TESTING)
  add_test(NAME hello_example COMMAND hello_example)
  add_test(NAME yielding_example COMMAND yielding_example)

  # golden_tests only diffs text; this proves each expected output also
  # compiles against the real runtime. It lives here because examples/ is
  # the layer allowed to join transpiler fixtures and runtime headers.
  add_test(NAME golden_compile
    COMMAND "${CMAKE_COMMAND}"
      "-DCXX=${CMAKE_CXX_COMPILER}"
      "-DRUNTIME_INCLUDE=${async_project_SOURCE_DIR}/runtime/include"
      "-DGOLDEN_DIR=${async_project_SOURCE_DIR}/transpiler/tests/golden"
      -P "${async_project_SOURCE_DIR}/cmake/check_golden_compile.cmake")

  # Guards the fail-loudly rule: a missing/renamed fixture directory must be
  # an error, never "0 fixtures OK". Passes only on the expected message.
  # CMake-only: the Makefile loop does not use the script.
  add_test(NAME golden_compile_empty_dir
    COMMAND "${CMAKE_COMMAND}"
      "-DCXX=${CMAKE_CXX_COMPILER}"
      "-DRUNTIME_INCLUDE=${async_project_SOURCE_DIR}/runtime/include"
      "-DGOLDEN_DIR=${CMAKE_CURRENT_BINARY_DIR}/golden_compile_no_such_dir"
      -P "${async_project_SOURCE_DIR}/cmake/check_golden_compile.cmake")
  set_tests_properties(golden_compile_empty_dir PROPERTIES
    PASS_REGULAR_EXPRESSION "no golden fixtures found")
endif()

# examples/consumer is a standalone project (it add_subdirectory()s this
# repo), so it can't be added to this build directly. Configure, build, and
# run it as a separate project to check that outside integration keeps working.
if(BUILD_TESTING)
  add_test(NAME consumer_example
    COMMAND "${CMAKE_CTEST_COMMAND}"
      --build-and-test "${CMAKE_CURRENT_SOURCE_DIR}/consumer" "${CMAKE_CURRENT_BINARY_DIR}/consumer"
      --build-generator "${CMAKE_GENERATOR}"
      --build-options "-DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}"
      --test-command consumer_app
  )
endif()
