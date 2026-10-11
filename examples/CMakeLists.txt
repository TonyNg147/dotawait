add_async_executable(hello_example hello.async.cpp)
add_async_executable(yielding_example yielding.async.cpp)

if(BUILD_TESTING)
  add_test(NAME hello_example COMMAND hello_example)
  add_test(NAME yielding_example COMMAND yielding_example)
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
