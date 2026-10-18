---
id: T-0015
title: Switch CMake and Makefile integration to the launcher
status: ready
area: cmake
type: feature
priority: P1
depends_on: [T-0014]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0014.

## User story

As a C++ developer, I want `target_enable_async(app)`, or one
`set(ASYNCTP_AUTO ON)` like `CMAKE_AUTOMOC`, to make every `.cpp`/`.hpp`
of my target accept `async`/`.await`, with my original files in
`compile_commands.json`, so that build and editor integration need no
special file names.

## Problem

`cmake/AsyncTranspile.cmake` decides by file suffix (`\.async\.cpp$`) and
replaces each source with a generated copy via `add_custom_command`. The
repo `Makefile` does the same with a pattern rule. Observed consequences:

- `compile_commands.json` lists `build/examples/asynctp_generated/hello_example/hello.cpp` instead of `examples/hello.async.cpp`.
- Headers cannot be lowered.

T-0014 provides `asynctp-cc`; nothing uses it yet.

## Evidence (architect experiments, CMake 4.3.4, Unix Makefiles)

- `set_target_properties(app PROPERTIES CXX_COMPILER_LAUNCHER "$<TARGET_FILE:launcher>;--")` plus `add_dependencies(app launcher)` works:
  - the launcher target is built first, then used for `app`;
  - `compile_commands.json` holds the plain `/usr/bin/c++ … -c m.cpp` for `app`.
- A `cmake_language(DEFER CALL fn)` registered before the targets exist can read `BUILDSYSTEM_TARGETS` and set `CXX_COMPILER_LAUNCHER` on every target. It chained correctly in front of a pre-existing launcher (`l.sh;--;env`).
- **Pitfall observed:** when there is no previous launcher, appending an empty element (`…;--;`) makes CMake pass an empty argv entry, and the compile fails with `Error 126`. Never append an empty list element.

## Scope

- In:
  - `target_enable_async(<target>)`;
  - the `ASYNCTP_AUTO` global switch;
  - re-implementing `add_async_executable`, `add_async_library` and `target_async_sources` on top of them;
  - deprecating `async_transpile_sources`;
  - a configure-time check for targets that consume async headers without being enabled;
  - repo `Makefile` and `examples/CMakeLists.txt` switched to the launcher;
  - consumer project builds through the launcher.
- Out:
  - Renaming files away from `.async.cpp` (T-0017). Existing `.async.cpp` names keep working because they end in `.cpp`.
  - Install/export packaging (review finding M7).
- Touches:
  - `cmake/AsyncTranspile.cmake`, `transpiler/CMakeLists.txt` (mark own targets), `examples/CMakeLists.txt`, `examples/consumer/CMakeLists.txt`
  - `Makefile`
  - a new ctest `compile_commands_check`
  - README CMake section

## Blueprint

1. **`target_enable_async(<target>)`** in `AsyncTranspile.cmake`:
   - Read the target's current `CXX_COMPILER_LAUNCHER`. If it is unset, fall back to `CMAKE_CXX_COMPILER_LAUNCHER`; treat `-NOTFOUND` and empty as no previous launcher.
   - Set:
     `CXX_COMPILER_LAUNCHER = $<TARGET_FILE:asynctp-cc>;--root;${CMAKE_SOURCE_DIR};[--root;<target SOURCE_DIR> if not under CMAKE_SOURCE_DIR];[--root;<each ASYNCTP_EXTRA_ROOTS>];--shadow;${CMAKE_BINARY_DIR}/asynctp-shadow;--[;<previous launcher list>]`.
     The previous launcher goes after `--`, so ccache sees the shadow copy. Append it only if non-empty (Error 126 pitfall).
   - `add_dependencies(<target> asynctp-cc)`.
   - `target_link_libraries(<target> PRIVATE asyncrt)` for executables/OBJECT libraries, `PUBLIC asyncrt` for STATIC/SHARED libraries. This supplies the include path for the injected `#include <asyncrt/asyncrt.hpp>`.
   - Set the custom target property `ASYNCTP_ENABLED ON`. For libraries, also set `INTERFACE_ASYNCTP_REQUIRED ON` (used in step 4).
   - Idempotent: calling it twice must not double the launcher (check `ASYNCTP_ENABLED`).
2. **`ASYNCTP_AUTO`:** when `AsyncTranspile.cmake` is included, register once
   `cmake_language(DEFER DIRECTORY ${CMAKE_SOURCE_DIR} CALL _asynctp_finalize)`.
   `_asynctp_finalize` walks the directory tree from `${CMAKE_SOURCE_DIR}`, recursing through the `SUBDIRECTORIES` property and reading each directory's `BUILDSYSTEM_TARGETS`. Then:
   - **If `ASYNCTP_AUTO` is ON:** call `target_enable_async` on every `EXECUTABLE`, `STATIC_LIBRARY`, `SHARED_LIBRARY`, `OBJECT_LIBRARY` and `MODULE_LIBRARY` target, **except** targets with property `ASYNCTP_SKIP` and dotawait's own targets. `transpiler/CMakeLists.txt` and `runtime/CMakeLists.txt` set `ASYNCTP_SKIP ON` on `asynctp`, `asynctp-cc`, `asynctp_core` and all test executables. Otherwise `asynctp-cc` would be compiled by itself (a cycle).
   - **Always (step 4):** run the consumer check.
3. **Re-implement the helpers**, keeping their signatures:
   - `target_async_sources(<t> [PRIVATE|PUBLIC|INTERFACE] srcs…)` = `target_sources(<t> <scope> srcs…)` (original paths, no generation) + `target_enable_async(<t>)`. The old per-source `target_include_directories(... src_dir)` hack is removed: it is no longer needed, because quoted includes resolve in the mirrored tree (T-0014).
   - `add_async_executable` and `add_async_library` keep their bodies on top of the above.
   - `async_transpile_sources(OUT …)`: keep the function, set `OUT` to the absolute input paths unchanged, and emit `message(DEPRECATION "async_transpile_sources is no longer needed: call target_enable_async(<target>) …")`. T-0017 removes it.
4. **Consumer check (fail loudly at configure time).** For every target **not** `ASYNCTP_ENABLED` whose direct `LINK_LIBRARIES` include a target with `INTERFACE_ASYNCTP_REQUIRED`:

   `message(FATAL_ERROR "target '<t>' links async library '<lib>' whose headers use async; call target_enable_async(<t>) or set ASYNCTP_AUTO ON")`

   Without this check, the user gets GCC's `'async' does not name a type` deep inside the library's header. Transitive links are not followed; document it.
5. **This repo:**
   - `examples/CMakeLists.txt` keeps `add_async_executable`, which now goes through the launcher.
   - `examples/consumer/CMakeLists.txt`:
     - keep `add_async_library(jobs …)` / `add_async_executable(consumer_app …)`;
     - add `set(CMAKE_EXPORT_COMPILE_COMMANDS ON)`.
6. **`Makefile`** (parity):
   - build `$(BUILD)/asynctp-cc`;
   - compile examples with `$(BUILD)/asynctp-cc --root $(CURDIR) --shadow $(BUILD)/asynctp-shadow -- $(CXX) $(CXXFLAGS) $(RUNTIME_INC) -Iexamples -c examples/<name>.async.cpp -o $(BUILD)/<name>.o -MD -MF $(BUILD)/<name>.d`, then link with plain `$(CXX)`;
   - `-include $(BUILD)/*.d` for header dependency tracking;
   - delete the `generated/%.cpp` pattern rule and the `.PRECIOUS` line.

   Update the header comment and the "Adding an example" comment.
7. **New ctest `compile_commands_check`.** Under `if(BUILD_TESTING)` in `examples/CMakeLists.txt`, add a test that runs `${CMAKE_COMMAND} -P ${PROJECT_SOURCE_DIR}/cmake/check_compile_commands.cmake` with the consumer build dir. The script lives under `cmake/` (build glue; allowed to know both sides). It reads `compile_commands.json` from the consumer build (configured by `consumer_example`) and asserts:
   - every entry's `file` is an original source (under `examples/consumer/src`);
   - no `command` contains `asynctp-cc`.

   Give it `DEPENDS consumer_example` via `set_tests_properties(... DEPENDS consumer_example)`.

### Invariants

- **Transpiler/runtime independence:** the only join points stay `cmake/AsyncTranspile.cmake` (links `asyncrt`, points the launcher at `asynctp-cc`) and `examples/`. `asynctp-cc` receives no runtime path.
- **Makefile/CMake parity:** both build examples through `asynctp-cc` (steps 5 and 6).
- **Fail loudly:** the configure-time consumer check (step 4); no empty launcher argv (pitfall).
- **Zero-dependency / header-only runtime:** unaffected.

## Acceptance criteria

- [ ] `verify.sh full` passes, with examples and consumer built through `asynctp-cc`:
  - `ASYNCTP_CC_VERBOSE=1 cmake --build build -j1 2>&1 | grep 'asynctp-cc: lowered'` shows each example.
  - The same build of `asynctp`/`asynctp-cc` themselves shows no launcher.
- [ ] `compile_commands_check` ctest passes (original files, no launcher in commands).
- [ ] Configure-time check, using a new fixture project `examples/consumer_check_fail/`:
  - The fixture `add_subdirectory()`s this repo like `examples/consumer`, then does `add_async_library(lib STATIC lib.cpp)` and a plain `add_executable(app main.cpp)` with `target_link_libraries(app PRIVATE lib)`.
  - Register it as
    `add_test(NAME async_consumer_check COMMAND ${CMAKE_COMMAND} -S ${CMAKE_CURRENT_SOURCE_DIR}/consumer_check_fail -B ${CMAKE_CURRENT_BINARY_DIR}/consumer_check_fail)`
    with `set_tests_properties(async_consumer_check PROPERTIES PASS_REGULAR_EXPRESSION "links async library")`.
    `PASS_REGULAR_EXPRESSION` makes the configure failure count as a pass only when the message is present.
- [ ] `ASYNCTP_AUTO`: a second fixture `examples/auto_mode/` (its own tiny project: `set(ASYNCTP_AUTO ON)`, a plain `add_executable` with an `async` function in `main.cpp`) builds and runs via `ctest --build-and-test`. Register it in `examples/CMakeLists.txt`.
- [ ] ccache-style chaining: unit-level CMake script test, or the `auto_mode` fixture with `CMAKE_CXX_COMPILER_LAUNCHER=env`:
  - the resulting launcher list ends with `--;env`;
  - with no prior launcher it ends with `--` and has no empty element.
- [ ] `async_transpile_sources` still works for an existing caller and prints a deprecation message.
- [ ] Editing `examples/consumer/src/jobs/jobs.hpp` (touch) triggers a recompile of `main.async.cpp` in the consumer build (manual check recorded in the PR; the depfile rewrite is covered by T-0014's tests).
- [ ] If `ninja` is installed in the verifying environment, repeat `compile_commands_check` with `-G Ninja` and record the result in the PR. Ninja was not available to the architect.
- [ ] README "CMake" section rewritten:
  - `target_enable_async`, `ASYNCTP_AUTO`, the helpers;
  - the consumer check;
  - ccache chaining;
  - the plain-Makefile recipe `CXX = asynctp-cc --root $(CURDIR) --shadow build/asynctp-shadow -- g++`;
  - recommend `CMAKE_EXPORT_COMPILE_COMMANDS ON` for clangd.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Log

- 2026-10-17 architect: created under epic T-0009; verified genex launcher plus add_dependencies, compile_commands contents, DEFER-based auto enable and launcher chaining, and the empty-element Error 126 pitfall; ready.
- 2026-10-17 architect: D2/D3 accepted as recommended (deprecate async_transpile_sources here, remove in T-0017; ship both target_enable_async and ASYNCTP_AUTO). No change to the blueprint.
