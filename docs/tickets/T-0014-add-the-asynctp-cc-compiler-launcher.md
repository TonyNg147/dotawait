---
id: T-0014
title: Add the asynctp-cc compiler launcher
status: ready
area: transpiler
type: feature
priority: P1
depends_on: [T-0010, T-0004, T-0012, T-0006]
created: 2026-10-17
---

Epic: T-0009. Depends on:
- T-0010 (tokens);
- T-0004 (`std::async` safety: every TU now passes through);
- T-0012 (single `#line`, BOM, escaped filenames);
- T-0006 (header declarations).

## User story

As a C++ developer, I want to name my files `main.cpp` and `jobs.hpp`,
write `async`/`.await` in them, and have my build lower them in the build
tree, never touching my sources, so that dotawait works like Qt's AUTOMOC
instead of forcing a `.async.cpp` naming convention.

## Problem

Today, only files named `*.async.cpp` are transpiled
(`cmake/AsyncTranspile.cmake`, `async_transpile_sources`, regex
`\.async\.cpp$`; `Makefile` rule `$(BUILD)/generated/%.cpp: examples/%.async.cpp`).
Headers cannot contain `async` at all. The generated `.cpp` is what CMake
compiles, so `compile_commands.json` lists the generated file, not the
user's:

```
"file": ".../build/examples/asynctp_generated/hello_example/hello.cpp"
```

This was observed by configuring this repo with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON`.
clangd therefore has no entry for `examples/hello.async.cpp`.

## Evidence from the prototype (architect, 2026-10-17, CMake 4.3.4, GCC 15.2, Unix Makefiles)

A throw-away Python launcher set as `CXX_COMPILER_LAUNCHER` on a target
proved the scheme. It is *not* to be reused; implement in C++.

1. CMake invokes the launcher as `<launcher> <launcher args> /usr/bin/c++ -I… -std=gnu++20 -MD -MT <obj> -MF <obj>.d -o <obj> -c <abs src>`.
2. **`compile_commands.json` does NOT contain the launcher.** It holds the plain `/usr/bin/c++ … -c <original src>` command, which is what clangd needs.
3. GCC resolves `#include "x.hpp"` relative to the **physical** file's directory, not the `#line` name. Hence the shadow tree must mirror directory structure.
4. **Shadowing only the async headers fails:**
   `sh/src/jobs/jobs.hpp:2:10: fatal error: detail.hpp: No such file or directory`.
   The shadowed header's own sibling include breaks. Mirroring **every non-system header the TU reaches** (plain ones copied with a `#line 1` prefix) and inserting a shadow twin before each project `-I` fixes both quoted and angle includes.
5. The compiler's depfile lists **shadow** paths. Rewriting the shadow prefix back to the original path made edits to an original header trigger a rebuild. A no-op rebuild compiled nothing.
6. A plain TU calling `std::async` broke the build (`plain.cpp:2:27: error: could not find a function name...`). Hence the T-0004 dependency.
7. A `-M` dependency scan costs ~56 ms per TU here, vs ~430 ms to compile the same TU (`<iostream> <vector> <future> <map> <string>`), about 13% overhead.
8. A UTF-8 BOM after a prepended `#line` breaks GCC (`'﻿int' does not name a type`): the BOM must stay first.
9. **Ninja is not installed on this machine**, so the compile_commands claim is verified for Unix Makefiles only (see acceptance criteria).

## Scope

- In:
  - A new executable `asynctp-cc` in `transpiler/`, linking the asynctp core library in-process:
    - argument parsing and invocation classification;
    - dependency scan, shadow mirroring with in-process lowering, compile-command rewrite;
    - depfile rewrite, bypass post-check, passthrough;
    - integration tests with the real compiler.
- Out:
  - CMake/Makefile wiring of this repo and of users' projects (T-0015).
  - Column remapping of diagnostics (T-0016).
  - MSVC; Windows paths; PCH; C++20 modules; unity builds. Each is **detected and rejected loudly** when lowering is needed.
- Touches:
  - `transpiler/include/asynctp/launcher.hpp` (new), `transpiler/src/launcher.cpp` (new), `transpiler/src/launcher_main.cpp` (new)
  - `transpiler/CMakeLists.txt`, `Makefile`
  - `transpiler/tests/launcher_tests.cpp` (new), `transpiler/tests/launcher_integration_tests.cpp` (new)

## Blueprint

### CLI

```
asynctp-cc [--root <dir>]... --shadow <dir> [--verbose] -- <compiler> <compiler args...>
```

- `--root` is repeatable and defaults to the current directory. Only files under a root are lowered.
- `--shadow` is required: the build-tree directory that holds lowered/mirrored copies. Exit code 2 with usage if it is missing.
- `--verbose`, or env `ASYNCTP_CC_VERBOSE=1`, prints one line per invocation to stderr: `asynctp-cc: passthrough <src>` or `asynctp-cc: lowered <src> (<n> files mirrored)`, followed by the final command.
- Everything after `--` is the real command, forwarded byte-for-byte when passing through.

### Pure, unit-testable functions (`launcher.hpp`, namespace `asynctp::cc`)

```cpp
struct LauncherOptions { std::vector<std::filesystem::path> roots; std::filesystem::path shadow; bool verbose; };
std::variant<std::pair<LauncherOptions, std::vector<std::string>>, std::string> parse_launcher_args(int, char**);
struct Invocation {                       // result of reading the compiler args
  std::vector<std::size_t> source_idx;    // indices of C++ source args
  std::optional<std::size_t> depfile_idx; // value index of -MF, if any
  bool compile_only;                      // has -c
  bool preprocess_only;                   // -E / -M / -MM without -c
  std::vector<std::string> unsupported;   // e.g. "-x c++-header", "-fmodules-ts", "msvc driver"
};
Invocation classify(const std::vector<std::string>& cmd);
std::filesystem::path shadow_path(const LauncherOptions&, const std::filesystem::path& abs_original);
std::vector<std::string> rewrite_compile_args(const std::vector<std::string>& cmd, const Invocation&,
                                              const LauncherOptions&, const std::filesystem::path& cwd);
std::string rewrite_depfile(std::string_view depfile_text, const LauncherOptions&);
std::string sh_quote(std::string_view arg);  // POSIX sh single-quote escaping
```

### Algorithm (`launcher_main.cpp`)

1. **Parse own args;** take `cmd` = everything after `--`.
2. **Classify.**
   - Source args are operands (not values of value-taking options) ending in `.cpp .cc .cxx .c++ .C`.
   - Value-taking options are kept in one table: `-o -MF -MT -MQ -I -isystem -iquote -idirafter -include -imacros -x -D -U -Xlinker -Xpreprocessor -Xassembler -L -l -T --param -aux-info -dumpbase -dumpdir -MJ`, each also accepted in its joined form.
   - **No source, or `preprocess_only`, or `-x` with a non-C++ language:** passthrough. This includes link steps when users set `CXX="asynctp-cc … -- g++"`.
3. **Dependency scan, per source.** Run the user's command with these changes:
   - remove `-c`, `-o X`, `-MD`, `-MMD`, `-MF X`, `-MT X`, `-MQ X` and the other sources;
   - add `-MM -MF <tmp>`.

   Parse the depfile (handle `\`-newline continuations and `\ ` escaped spaces); this is the list of *non-system* deps. If the scan fails, print its stderr and exit 1. That is the same error the user would get without asynctp.
4. **Decide.** Call `find_async_markers` (T-0004, classified) on the TU and on every dep under a root.
   - **No markers anywhere:** passthrough. The command runs unchanged; nothing is written to the shadow tree.
   - **Markers present but `unsupported` non-empty:** error `asynctp-cc: <src>: async code cannot be lowered with <flag> (unsupported: precompiled headers / modules / MSVC)`, exit 1.
5. **Mirror.** For the TU and each dep, the destination is `shadow_path(abs)` = `<shadow>/<abs path without leading '/'>`.
   - **Under a root and has markers:** `parse_file` + `emit` in-process with `filename` = the absolute original path.
     - On `ParseError`, print `<abs orig>:<line>:<col>: error: <msg>` (same format as `asynctp`) and exit 1.
   - **Otherwise:** a verbatim copy, prefixed by `#line 1 "<escaped abs path>"\n`. If the file starts with a BOM, emit the BOM first, then the `#line`.
   - **Write atomically:** write `<dst>.tmp-<random>`, where the suffix comes from `std::random_device` plus `steady_clock` (no `getpid` in ISO C++).
     - If `<dst>` exists with identical bytes, delete the temp file; this keeps mtimes stable for ccache and make.
     - Otherwise `std::filesystem::rename` over `<dst>` (atomic on POSIX, so parallel compiles writing the same header are safe).
6. **Rewrite the command** (`rewrite_compile_args`):
   - replace each source arg with its shadow path;
   - before every `-I`/`-iquote` dir (joined or separate form; relative dirs resolved against cwd), insert the same flag pointing at `shadow_path(dir)`. Never touch `-isystem`/`-idirafter`;
   - append `-iquote <original dir of the TU>` last, as a fallback so `__has_include("x.h")` sees headers the TU tests but does not include;
   - if no depfile was requested, add `-MD -MF <shadow>/.tmp/<random>.d` for the post-check.
7. **Run the compiler.** Use `run_command(argv)`, implemented with `std::system` over `sh_quote`d args, with stdout/stderr inherited.
   Exit status: 0 stays 0; anything else exits 1. Exact codes cannot be decoded portably without POSIX; see D1 in T-0009.
8. **Depfile.** If one exists (user's or ours), rewrite `<shadow>/` prefixes back to `/` (`rewrite_depfile`) in place. A user's `-MD` without `-MF` defaults to `<obj without extension>.d`; handle that too.
9. **Bypass post-check.** If any path in the rewritten depfile is an *original* (non-shadow) path of a file that has markers, print:

   `asynctp-cc: <abs header> was included by its original path (absolute #include, unmapped -I, or #include_next), so the compiler saw unlowered async code; include it through a quoted relative path or an -I directory`

   Exit 1 even if the compile "succeeded" (it cannot have, but be explicit). Delete our own temp depfile.
10. **The source tree is never written.** All writes go under `--shadow`.

### Build parity

- `transpiler/CMakeLists.txt`:
  - make `asynctp_core` unconditional (move it out of `if(BUILD_TESTING)`);
  - link `asynctp` and the new `add_executable(asynctp-cc src/launcher.cpp src/launcher_main.cpp)` against it;
  - add tests `launcher_tests` and `launcher_integration_tests`. The latter receives `$<TARGET_FILE:asynctp-cc> ${CMAKE_CXX_COMPILER} ${PROJECT_SOURCE_DIR}/runtime/include` as arguments.
- `Makefile`:
  - `$(BUILD)/asynctp-cc` from `transpiler/src/launcher.cpp transpiler/src/launcher_main.cpp $(TRANSPILER_SRCS)`;
  - both test binaries in `all` and `test`.

### Invariants

- **Transpiler/runtime independence:** `asynctp-cc` never knows where `asyncrt` lives. The injected `#include <asyncrt/asyncrt.hpp>` is text, and the include path comes from the user's flags (CMake: linking `asyncrt`, T-0015). The integration test gets the runtime dir as an argument from the build system.
- **Zero-dependency:** `std::filesystem`, `std::system`, `std::random_device` only. No POSIX headers in this ticket; T-0009 D1 decides whether a later ticket may switch `run_command` to `posix_spawn`.
- **Text-island rewriter / sources untouched:** mirrored plain files are byte-identical apart from a prepended `#line` (and a relocated BOM). The source tree is never written; the integration test asserts it.
- **Fail loudly:**
  - every unsupported mode is an explicit error *only when lowering is needed*;
  - plain TUs pass through untouched;
  - original-path bypass is detected (step 9).
- **Makefile/CMake parity:** the new binary and two suites are in both.

## Acceptance criteria

- [ ] `launcher_tests` (unit, no subprocess):
  - `parse_launcher_args`: missing `--shadow` is an error; `--root` is repeatable; everything after `--` is preserved.
  - `classify`: `g++ -c a.cpp -o a.o` gives source idx 1. `g++ a.o b.o -o app` gives no sources (link). `g++ -E a.cpp` is preprocess_only. `-x c++-header` and `-fmodules-ts` go to unsupported. `-I inc -o out.o -c x.cc` gives the source `x.cc`, and `inc` is not a source. `-MF dep.d` gives depfile idx.
  - `rewrite_compile_args`: shadow twins inserted before `-Isrc`, `-I src`, `-iquote q`; `-isystem s` untouched; source replaced; trailing `-iquote <tu dir>`; `-MD -MF` added only when absent.
  - `rewrite_depfile`: shadow prefixes are removed, including in continued lines and in paths with escaped spaces.
  - `sh_quote`: `a'b` becomes `'a'\''b'`.
- [ ] `launcher_integration_tests` (real compiler, temp dirs under `std::filesystem::temp_directory_path()`, one function per case):
  1. Project `src/main.cpp` (`#include "jobs/jobs.hpp"`, `async int both()` awaiting `download`) plus `src/jobs/jobs.hpp` (`#include "detail.hpp"` then `async int download(int n);`) plus `src/jobs/jobs.cpp` (definition) plus `src/jobs/detail.hpp`.
     Compile each `.cpp` via `asynctp-cc --root <proj> --shadow <tmp>/sh -- <cxx> -std=c++20 -I<proj>/src -I<runtime> -c … -MD -MF …`, link with the plain compiler, run: exit 0.
  2. Same project with `#include <jobs/jobs.hpp>` (angle include through `-I`): builds and runs.
  3. Depfile of case 1 contains `<proj>/src/jobs/jobs.hpp` and no path under `<tmp>/sh`.
  4. Plain TU with `std::async(std::launch::async, …)` and no markers: `--verbose` prints `passthrough`, nothing is created under the shadow dir, and the object links.
  5. TU with an async syntax error at line 3: exit 1, stderr contains `<abs src>:3:`.
  6. A header outside every `--root` containing `async int x();`: passthrough (not lowered), and GCC's own error is shown. Assert exit 1.
  7. TU including the async header by **absolute path**: exit 1, stderr contains `was included by its original path`.
  8. BOM-prefixed header with async content: compiles.
  9. The source tree is unchanged: the recursive listing plus file sizes and mtimes are identical before and after cases 1 to 8.
  10. Compile with `-x c++-header` on a header with markers: exit 1, stderr contains `unsupported`.
- [ ] Each test skips with a clear message, rather than failing, only if the compiler path argument is missing. In CI it is always passed.
- [ ] `asynctp-cc` builds warning-free under `-Wall -Wextra`; `verify.sh asan` runs both suites clean.
- [ ] Doc comment at the top of `launcher.hpp` documents the scheme: shadow layout, mirroring rule, `-I` twins, depfile rewrite, post-check. It also lists the known limitations: GCC/Clang only; POSIX paths; no PCH/modules/unity builds; `#include_next` inside mirrored headers; the ~one extra preprocessor pass per async-affected TU.
- [ ] Makefile and CMake both build `asynctp-cc` and run both suites.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

- **Why a launcher and not custom commands without the suffix.** The alternative is to keep `add_custom_command` but transpile *every* source of an enabled target, or scan sources for `async` at configure time.
  1. `compile_commands.json` then lists generated files, as observed today, so clangd loses the user's file. The launcher keeps the original command in the database (verified).
  2. A configure-time scan goes stale when a user adds the first `async` to a file (it needs a reconfigure). Transpiling everything unconditionally adds a custom command per source.
  3. Neither handles headers, because custom commands cannot know which headers a TU includes. The launcher learns it per compile from the compiler itself.
  4. The launcher also works for plain Makefiles (`CXX="asynctp-cc … -- g++"`).

  Costs: one extra `-M` pass per affected TU (~13% here), and launcher slot sharing with ccache (T-0015 chains them: `asynctp-cc … -- ccache g++`).
- **Why mirror every reached header** rather than only async ones: see evidence item 4. Only-async shadowing needs `-iquote` for every shadowed header's directory, which also changes lookup for unrelated same-named headers.

## Log

- 2026-10-17 architect: created under epic T-0009; prototyped the scheme with CMake 4.3.4 / GCC 15.2 (compile_commands excludes the launcher; physical-dir include resolution; mirror-all needed; depfile rewrite triggers correct rebuilds; std::async breakage); ready.
- 2026-10-17 architect: D1 resolved (POSIX allowed, isolated in transpiler/src/platform/). Scope unchanged: run_command stays std::system here. Switching it to the T-0021 platform module (exact exit codes) is an optional follow-up, not part of this ticket.
