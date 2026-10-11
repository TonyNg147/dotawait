# A standalone project showing how an outside codebase uses async_project.
# Build it on its own:
#
#   cmake -S examples/consumer -B build-consumer
#   cmake --build build-consumer && ./build-consumer/consumer_app
#
# Point ASYNC_PROJECT_DIR elsewhere (e.g. a git submodule) in a real project.

cmake_minimum_required(VERSION 3.20)
project(async_consumer CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(ASYNC_PROJECT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../.." CACHE PATH "Path to the async_project checkout")

# Brings in the `asynctp` transpiler, the `asyncrt` runtime, and the
# add_async_executable/add_async_library/target_async_sources helpers from
# cmake/AsyncTranspile.cmake. async_project's own tests/examples stay off.
add_subdirectory("${ASYNC_PROJECT_DIR}" async_project)

# A library written with async functions. Anything linking it gets asyncrt
# (and the include path below) transitively.
add_async_library(jobs STATIC src/jobs/jobs.async.cpp)
target_include_directories(jobs PUBLIC src)

# The app mixes an .async.cpp with an ordinary .cpp.
add_async_executable(consumer_app src/main.async.cpp src/report.cpp)
target_link_libraries(consumer_app PRIVATE jobs)
