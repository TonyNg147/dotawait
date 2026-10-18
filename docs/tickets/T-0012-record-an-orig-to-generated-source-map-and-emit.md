---
id: T-0012
title: Record an orig-to-generated source map and emit exact positions
status: ready
area: transpiler
type: bug
priority: P1
depends_on: [T-0011]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0011. If T-0003 has landed, build on its edit list; otherwise introduce the edit list here and T-0003 reuses it.

## User story

As a C++ developer, I want every compiler error, debugger line, and (later)
editor squiggle to point at the exact place in the file I wrote, so that
the transpiler is invisible when something goes wrong.

## Problem

The emitter (`transpiler/src/emitter.cpp`, `emit`) keeps no record of where
generated text came from. It sprinkles `#line N` after each lowered
function, which is not enough.

Observed:

1. **Line drift for a multi-line signature (review finding M3).** Input `m3b.cpp`:
   ```cpp
   #include <string>
   async
   std::string
   fetch()
   {
     return nope;
   }
   ```
   asynctp output (default, with `#line`):
   ```cpp
   #include <asyncrt/asyncrt.hpp>
   #line 1 "m3b.cpp"
   #include <string>
   ::asyncrt::Task<std::string> fetch() {
     co_return nope;
   }
   #line 7 "m3b.cpp"
   ```
   GCC 15 reports `m3b.cpp:3:13: error: 'nope' was not declared in this scope`. The real location is line **6**. `lower_function` joins the signature onto one line.
2. **Column drift.** For `async int g() { int z = f().await + nope; return z; }` on line 2, GCC reports `col.cpp:2:53`. `nope` is at column **37**.
   GCC prints the *original* line with a caret at column 53, i.e. past the end of the text. This cannot be fixed with `#line`; T-0016 fixes it using this map.
3. **UTF-8 BOM breaks the build.** asynctp emits `#line 1 "bom.cpp"` *before* the input's BOM. GCC then fails with `bom.cpp:1:1: error: '﻿int' does not name a type`. The same input compiles fine without asynctp.
4. **Unescaped filename.** For input `we\ird/x.cpp`, asynctp emits `#line 1 "we\ird/x.cpp"`, and GCC warns `unknown escape sequence: '\i'` (and records the wrong file name).
5. **No mapping exists**, so the LSP proxy (T-0022/T-0023) has nothing to translate positions with.

## Scope

- In:
  - A `SourceMap` built during emission.
  - A **line-preserving** lowering rule.
  - BOM handling and `#line` filename escaping.
  - A `--emit-map <file>` CLI option for tests and debugging.
  - A diagnostic-line test that runs the real compiler.
- Out:
  - Rewriting GCC's column numbers (T-0016, in the launcher).
  - LSP position encoding (UTF-16). The map stores byte offsets only; conversion is T-0022's job.
- Touches:
  - `transpiler/include/asynctp/source_map.hpp` (new), `transpiler/src/source_map.cpp` (new)
  - `emitter.hpp/.cpp`, `lowering.hpp/.cpp`, `main.cpp`
  - `transpiler/CMakeLists.txt`, `Makefile`
  - new suites `source_map_tests` and `diagnostic_line_tests`
  - golden fixture

## Blueprint

1. **`source_map.hpp`** (namespace `asynctp`):
   ```cpp
   struct MapSegment {
     std::size_t orig_begin, orig_len, gen_begin, gen_len;
     bool verbatim; // true: identical bytes, orig_len == gen_len, exact mapping
   };
   class SourceMap {
   public:
     void append(MapSegment s);          // segments must arrive in order on both sides
     enum class Bias { Start, End };
     std::size_t to_generated(std::size_t orig_offset, Bias = Bias::Start) const;
     std::size_t to_original(std::size_t gen_offset) const;
     const std::vector<MapSegment>& segments() const noexcept;
   };
   ```
   - **Coverage invariant:** segments are contiguous and cover `[0, orig.size())` and `[0, gen.size())` exactly.
   - **Inside a verbatim segment:** mapping is offset arithmetic.
   - **Inside a replacement**, `to_original` returns `orig_begin + min(gen_off - gen_begin, orig_len)`.
     For pure insertions (`orig_len == 0`, for example `co_await (`) it returns `orig_begin`.
   - `to_generated` returns `gen_begin` (Bias::Start) or `gen_begin + gen_len` (Bias::End).
   - Use binary search; files are small, so no fancier structure is needed.
2. **Edits become segments.** Lowering produces an ordered edit list over file offsets:
   `return` → `co_return`; `.await` operand wrapping; signature rewrite.
   `emit` walks the file once, producing a verbatim segment for each gap and a replacement segment for each edit.
   The prelude include and `#line` lines are insertions at their offsets.
3. **Line-preserving rule** (new contract for every lowering, documented in `lowering.hpp`):
   - The generated span of each async function contains exactly as many `\n` as the original span `[async_kw_offset, body_close_offset]`.
   - `lower_function` achieves this by emitting the signature on one line, then the `k` newlines the original signature contained (`async` up to `{`), then the `{`.
   - Body edits never add or remove newlines (operands are verbatim slices).
   - With this rule, the only `#line` the emitter needs is one `#line 1 "<file>"` right after the prelude. **Remove the per-function `#line` directives** in `emit`.
   - T-0002 and T-0006 must keep this rule; note it in both tickets' Logs.
4. **BOM:** if the source begins with `EF BB BF`, emit those 3 bytes first, then the prelude, then `#line 1`. The BOM is a verbatim segment at offset 0.
5. **Filename escaping:** in `#line`, escape `\` as `\\` and `"` as `\"`. Add a helper `escape_line_filename` in `emitter.cpp`.
6. **API:**
   - `emit(...)` returns `std::variant<EmitResult, ParseError>`, where `struct EmitResult { std::string text; SourceMap map; };`.
   - `main.cpp` adds `--emit-map <path>`. It writes one line per segment, `V|R orig_begin orig_len gen_begin gen_len`, after a header line `asynctp-map 1`.
   - Usage text is updated in `print_usage`.
7. **Build parity:** add `source_map.cpp` to both source lists and `TRANSPILER_SRCS`. Add the `source_map_tests` and `diagnostic_line_tests` suites to CMake and the Makefile.
   - `diagnostic_line_tests` takes `<asynctp> <c++ compiler> <runtime include dir> <fixture dir>`. CMake passes `$<TARGET_FILE:asynctp> ${CMAKE_CXX_COMPILER} ${PROJECT_SOURCE_DIR}/runtime/include`; the Makefile passes `$(ASYNCTP) $(CXX) runtime/include`.
   - Passing a runtime path as a test *argument* is allowed. Only `transpiler/` production code must not reference `runtime/`, and `examples/`-style integration glue already does this.
   - It runs asynctp, then the compiler with `-fsyntax-only`, captures stderr to a temp file via `std::system(... 2> file)`, and checks for `file:LINE:` substrings.

### Invariants

- **Transpiler/runtime independence:** `diagnostic_line_tests` gets the runtime include dir as a command-line argument from the build system. No transpiler source or header references `runtime/`. State this in the test's header comment.
- **Fail loudly:** a map that violates the coverage invariant is a bug. `SourceMap::append` asserts contiguity in debug builds (`assert`), and `source_map_tests` checks it.
- **Text-island rewriter:** unchanged. Passthrough is still byte-for-byte, and the map makes that checkable.
- **Makefile/CMake parity:** one new source and two new suites in both builds.

## Acceptance criteria

- [ ] `source_map_tests` (new), for the inputs `simple_fn`, `nested_lambda`, `m3b` (above) and a chained `.await` body:
  - segments are contiguous and cover both texts;
  - every verbatim byte round-trips: `to_original(to_generated(o)) == o`;
  - the generated offset of each `co_return` maps back to its `return`;
  - the generated `co_await (` maps to the operand's first byte;
  - the prelude maps to offset 0.
- [ ] `lowering_tests` / `source_map_tests`: for every existing lowering test input, `count('\n')` of the lowered function equals that of the original span.
- [ ] Golden fixture `multiline_signature` (input `m3b` above). The expected output has `::asyncrt::Task<std::string> fetch()` followed by three newlines, then `{`, and line count equal to the input + 1 (prelude).
- [ ] `diagnostic_line_tests` (new):
  - `m3b` reports `m3b.cpp:6:`;
  - a two-function file with an error in the second body reports the original line;
  - a BOM-prefixed async file compiles without error;
  - a file under a directory whose name contains a backslash reports that exact path with no "unknown escape" warning.
- [ ] `asynctp --emit-map` writes the documented format. One golden-style check is in `golden_tests.cpp` or `source_map_tests`.
- [ ] Existing golden fixtures are regenerated **only** where per-function `#line` removal changes them. They run with `--no-line-directives`, so expect no change; if one changes, explain it in the PR.
- [ ] `emitter.hpp` comment rewritten: one `#line`, the line-preserving rule, the BOM rule, and the map.
- [ ] Makefile and CMake both build `source_map.cpp` and run both new suites.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

- Closes review finding M3 and the BOM/escape findings from the earlier review sketch. H2 was closed by T-0011.
- A `#line` per lowered line (instead of line preservation) was considered and rejected. It bloats output and still cannot fix columns, while line preservation needs no directives at all.

## Log

- 2026-10-17 architect: created under epic T-0009; reproduced M3 (3 vs 6), column drift (53 vs 37), BOM failure, and backslash filename warning; ready.
