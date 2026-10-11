# Header-only: Task<T>/Executor/yield_now have no .cpp of their own. This
# target exists only to carry the include path and C++20 requirement to
# anything that links against it.
add_library(asyncrt INTERFACE)
add_library(asyncrt::asyncrt ALIAS asyncrt)
target_include_directories(asyncrt INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_features(asyncrt INTERFACE cxx_std_20)

if(BUILD_TESTING)
  foreach(test_name task_tests executor_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE asyncrt)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()
endif()
