add_executable(asynctp
  src/scanner.cpp
  src/tokenizer.cpp
  src/lexer.cpp
  src/async_parser.cpp
  src/lowering.cpp
  src/emitter.cpp
  src/main.cpp
)
target_include_directories(asynctp PRIVATE include)
target_compile_features(asynctp PRIVATE cxx_std_20)

# Shared by asynctp-lsp and the test suites, so it is built even with
# tests off.
add_library(asynctp_core STATIC
  src/scanner.cpp
  src/tokenizer.cpp
  src/lexer.cpp
  src/async_parser.cpp
  src/lowering.cpp
  src/emitter.cpp
  src/lsp/json.cpp
  src/lsp/framing.cpp
  src/lsp/proxy.cpp
  # The only POSIX code in transpiler/ (T-0009 decision D1).
  src/platform/process_posix.cpp
)
target_include_directories(asynctp_core PUBLIC include)
target_compile_features(asynctp_core PUBLIC cxx_std_20)

# The proxy runs one thread per direction; the platform thread library is
# allowed under D1 (it is not a third-party dependency).
find_package(Threads REQUIRED)
target_link_libraries(asynctp_core PUBLIC Threads::Threads)

# Editors launch this in place of clangd (README "Editor / LSP").
add_executable(asynctp-lsp src/lsp/lsp_main.cpp)
target_link_libraries(asynctp-lsp PRIVATE asynctp_core)
target_compile_definitions(asynctp-lsp PRIVATE ASYNCTP_VERSION="${PROJECT_VERSION}")

if(BUILD_TESTING)
  # Tests use the small hand-rolled harness in tests/test_support.hpp rather
  # than pulling in doctest/Catch2 via FetchContent -- it already covers
  # what these suites need (run everything, report every failing CHECK),
  # so there's no reason to add the dependency just for its own sake. The
  # project's zero-dependency rule applies strictly to transpiler/ and
  # runtime/ production code; test tooling was explicitly allowed to use a
  # framework if one earned its keep, but this one doesn't need it.
  foreach(test_name tokenizer_tests lexer_tests parser_tests lowering_tests json_tests framing_tests)
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

  # Stands in for clangd so the proxy tests need no LLVM install.
  add_executable(fake_lsp_server tests/fake_lsp_server.cpp)
  target_link_libraries(fake_lsp_server PRIVATE asynctp_core)

  add_executable(lsp_proxy_tests tests/lsp_proxy_tests.cpp)
  target_link_libraries(lsp_proxy_tests PRIVATE asynctp_core)
  add_test(NAME lsp_proxy_tests
    COMMAND lsp_proxy_tests $<TARGET_FILE:asynctp-lsp> $<TARGET_FILE:fake_lsp_server>
  )
  # A broken proxy tends to hang on a pipe rather than fail.
  set_tests_properties(lsp_proxy_tests PROPERTIES TIMEOUT 60)

  # Needs a real clangd; exits 77 (skipped) without one (decision D4).
  add_executable(lsp_clangd_smoke tests/lsp_clangd_smoke.cpp)
  target_link_libraries(lsp_clangd_smoke PRIVATE asynctp_core)
  add_test(NAME lsp_clangd_smoke COMMAND lsp_clangd_smoke $<TARGET_FILE:asynctp-lsp>)
  set_tests_properties(lsp_clangd_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
