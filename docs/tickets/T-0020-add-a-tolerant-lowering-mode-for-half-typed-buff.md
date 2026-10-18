---
id: T-0020
title: Add a tolerant lowering mode for half-typed buffers
status: ready
area: transpiler
type: feature
priority: P1
depends_on: [T-0012]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0012 (source map). T-0003 and T-0006 are recommended first, so tolerant mode inherits the final grammar. Any of them landing later must extend the tolerant tests.

## User story

As a C++ developer typing in my editor, I want completion and hover to
keep working while my async function is half-written (an unclosed brace,
`fetch(`, a dangling `.await`), so that the LSP proxy does not go blind
every time the file is momentarily invalid.

## Problem

Every asynctp stage stops at the first error and produces no output
(`parse_file`: "Never returns a partially-valid ParsedFile on error";
`emit` returns the first `ParseError`). That is right for the build (the
fail-loudly invariant). The LSP proxy (T-0022), however, receives a new
buffer on every keystroke, and most of those buffers are invalid. Without
output there is no lowered text and no map, so clangd sees raw `async`
and reports `unknown type name 'async'` everywhere.

Observed: for the buffer `async int f() { int y = g(` (no trailing newline),
`asynctp half.cpp -o half.out` prints
`half.cpp:1:27: error: unexpected end of file while matching delimiters`,
exits 1, and writes no output file.

## Scope

- In: a library entry point with two modes. **Strict** is exactly today's behaviour, used by `asynctp`/`asynctp-cc`. **Tolerant** always returns text + map + a list of diagnostics.
- Out: any CLI or launcher behaviour change. Tolerant mode is **never** used by the build. A CLI flag `--tolerant` may exist for debugging and tests only; its output is not meant to be compiled.
- Touches:
  - `transpiler/include/asynctp/lower_file.hpp`, `transpiler/src/lower_file.cpp` (new)
  - small hooks in `async_parser.cpp`, `lowering.cpp`, `emitter.cpp`
  - `main.cpp` (`--tolerant` debug flag)
  - `transpiler/tests/tolerant_tests.cpp` (new); CMake + Makefile

## Blueprint

1. **API:**
   ```cpp
   enum class Mode { Strict, Tolerant };
   struct LowerResult { std::string text; SourceMap map; std::vector<ParseError> diagnostics; };
   // Strict: diagnostics.size() <= 1; if non-empty, text is empty (callers must fail).
   // Tolerant: text/map always valid (coverage invariant holds); diagnostics may be many.
   LowerResult lower_file(std::string_view source, std::string_view filename, Mode, bool runtime_include = true);
   ```
   `asynctp`/`asynctp-cc` switch to `lower_file(..., Mode::Strict)`. That is a pure refactor; golden output is unchanged.
2. **Tolerant recovery rules** (each records a `ParseError` diagnostic, then continues):

   | Situation | Recovery |
   |---|---|
   | Tokenizer: unterminated block comment / raw string | the token extends to EOF |
   | `Invalid` token inside an island | treated as an atom; body balancing continues |
   | Marker whose declarator cannot be found (bound hit) | replace the 5 bytes `async` with 5 spaces (a replacement segment), so clangd sees `int fo` and completes normally |
   | Declarator found, body `{` never closed | body extends to EOF; lower signature and body; append `}` at EOF (an insertion mapped to EOF) |
   | Unbalanced `(`/`[` inside a body at a `;`, `}` or EOF | the group is closed implicitly at that point for operand scanning only; no text is inserted |
   | `.await` with an empty operand, or a `binds tighter` keyword error (T-0003) | leave `.await` verbatim (clangd will flag it) |
   | Non-void body without return (T-0013) | skip the check (half-typed bodies are normal) |
   | Ambiguous `async` (T-0004 rule 4) | blank `async` as above |
3. **No interference.** Strict mode must not be weakened. Implement the recovery behind `Mode` checks, and keep strict-path tests unchanged.
4. **Map coverage invariant** holds in tolerant mode too (T-0012). `tolerant_tests` asserts it for every case.
5. Document both modes in `lower_file.hpp`, including "tolerant output is for editors, never for compilers".

### Invariants

- **Fail loudly (build):** preserved by construction. Only `Mode::Strict` is reachable from `asynctp` (without the debug flag) and from `asynctp-cc`. Add a launcher unit test that asserts the launcher calls strict mode, for example via a seam or by checking that a broken file still fails.
- **Text-island rewriter:** tolerant edits stay inside islands, except the blanking of an orphan `async` keyword, which is recorded in the map.
- **Makefile/CMake parity:** one new source and one new suite in both.

## Acceptance criteria

- [ ] `tolerant_tests`, each asserting text, map coverage, and diagnostic count/positions:
  - `async int f() { int y = g(` gives text containing `::asyncrt::Task<int> f() { int y = g(` and ending in `}`; one diagnostic at the `{`.
  - `async int f() { return x.await; }\nasync int g() { return y.` gives `f` fully lowered and `g` lowered with an appended `}`.
  - `async int fo` gives `      int fo`, i.e. `async` blanked to five spaces (column-preserving); one diagnostic.
  - `async int f() { auto v = x.await.; }` gives `(co_await (x)).` in the output (T-0003 parenthesisation), so member completion works in clangd.
  - `async int f() { co_yield x.await; }` gives `.await` left verbatim and one diagnostic.
  - A strict-valid file gives text identical to strict mode and zero diagnostics.
- [ ] `golden_tests` unchanged (strict refactor proof).
- [ ] `asynctp --tolerant in.cpp -o -` prints the tolerant text, and its diagnostics as `file:L:C: warning:` on stderr; exit code 0.
- [ ] `lower_file.hpp` comment documents both modes and the recovery table.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Log

- 2026-10-17 architect: created under epic T-0009; ready. The recovery table is the contract the LSP tickets build on.
