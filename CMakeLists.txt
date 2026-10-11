cmake_minimum_required(VERSION 3.20)
project(async_project CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Tests and examples default to on only when this is the top-level project,
# so pulling it in with add_subdirectory()/FetchContent stays quiet.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(_async_is_top_level ON)
else()
  set(_async_is_top_level OFF)
endif()
option(ASYNC_PROJECT_BUILD_TESTS "Build async_project's test suites" ${_async_is_top_level})
option(ASYNC_PROJECT_BUILD_EXAMPLES "Build async_project's examples" ${_async_is_top_level})
set(BUILD_TESTING ${ASYNC_PROJECT_BUILD_TESTS})

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
# CMake functions are global, so this also makes add_async_executable() etc.
# available to a parent project after add_subdirectory(async_project).
include(AsyncTranspile)

if(ASYNC_PROJECT_BUILD_TESTS)
  enable_testing()
endif()

add_subdirectory(transpiler) # defines the `asynctp` CLI target, no dependency on runtime/
add_subdirectory(runtime)    # defines the header-only `asyncrt` target, no dependency on transpiler/
if(ASYNC_PROJECT_BUILD_EXAMPLES)
  add_subdirectory(examples) # depends on both: transpiles via asynctp, links against asyncrt
endif()
