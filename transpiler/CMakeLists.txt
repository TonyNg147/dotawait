add_executable(asynctp
  src/scanner.cpp
  src/lexer.cpp
  src/async_parser.cpp
  src/lowering.cpp
  src/emitter.cpp
  src/main.cpp
)
target_include_directories(asynctp PRIVATE include)
target_compile_features(asynctp PRIVATE cxx_std_20)

if(BUILD_TESTING)
  # Tests use the small hand-rolled harness in tests/test_support.hpp rather
  # than pulling in doctest/Catch2 via FetchContent -- it already covers
  # what these suites need (run everything, report every failing CHECK),
  # so there's no reason to add the dependency just for its own sake. The
  # project's zero-dependency rule applies strictly to transpiler/ and
  # runtime/ production code; test tooling was explicitly allowed to use a
  # framework if one earned its keep, but this one doesn't need it.
  add_library(asynctp_core STATIC
    src/scanner.cpp
    src/lexer.cpp
    src/async_parser.cpp
    src/lowering.cpp
    src/emitter.cpp
    # LSP support for the future asynctp-lsp proxy (T-0021); only the tests
    # use it until that executable exists.
    src/lsp/json.cpp
    src/lsp/framing.cpp
  )
  target_include_directories(asynctp_core PUBLIC include)
  target_compile_features(asynctp_core PUBLIC cxx_std_20)

  foreach(test_name lexer_tests parser_tests lowering_tests json_tests framing_tests)
    add_executable(${test_name} tests/${test_name}.cpp)
    target_link_libraries(${test_name} PRIVATE asynctp_core)
    target_compile_features(${test_name} PRIVATE cxx_std_20)
    add_test(NAME ${test_name} COMMAND ${test_name})
  endforeach()

  add_executable(golden_tests tests/golden_tests.cpp)
  target_include_directories(golden_tests PRIVATE include)
  target_compile_features(golden_tests PRIVATE cxx_std_20)
  add_test(NAME golden_tests
    COMMAND golden_tests $<TARGET_FILE:asynctp> "${CMAKE_CURRENT_SOURCE_DIR}/tests/golden"
  )
endif()
