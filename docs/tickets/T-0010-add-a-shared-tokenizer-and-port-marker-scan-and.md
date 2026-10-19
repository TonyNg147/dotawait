---
id: T-0010
title: Add a shared tokenizer and port marker scan and signature parser to it
status: done
area: transpiler
type: bug
priority: P1
depends_on: []
created: 2026-10-17
---

Epic: T-0009. Depends on: none (T-0001 should land first if it is still open, so the port inherits its behaviour; not a hard dependency).

## User story

As a C++ developer whose every `.cpp`/`.hpp` now passes through asynctp
(see the T-0009 contract), I want asynctp to read any file GCC accepts
without dropping or corrupting anything, so that a digit separator, an
apostrophe in `#if 0` prose, or a `#define` never silently turns off
lowering or breaks a build.

## Problem

Three hand-rolled scanners each re-implement "skip literals and comments":
`find_async_markers` (`transpiler/src/lexer.cpp`), `parse_one`
(`transpiler/src/async_parser.cpp`), and `lower_body` plus its char-level
backward helpers (`transpiler/src/lowering.cpp`). All three sit on
`SourceCursor` (`scanner.hpp`/`scanner.cpp`). `SourceCursor` treats every
`'` as the start of a char literal and knows nothing about preprocessor
directives. `transpiler/include/asynctp/token.hpp` declares a `Token` type
that nothing uses.

Observed with `build/transpiler/asynctp <in> -o <out> --no-line-directives`.

1. **A single digit separator silently disables lowering for the rest of the file.**
   `find_async_markers` stops on the first scan error (`if (cur.has_error()) break;`)
   and returns the markers found so far, so the run exits 0 with unlowered output.

   Input:
   ```cpp
   constexpr int kMax = 1'000;
   async int f() { return kMax; }
   ```
   Output (rc=0, nothing lowered, no runtime include):
   ```cpp
   constexpr int kMax = 1'000;
   async int f() { return kMax; }
   ```

2. **The same happens after an apostrophe in a directive or in `#if 0` text.**
   GCC accepts both and only warns `missing terminating ' character`.

   Input:
   ```cpp
   #if 0
   this isn't compiled
   #endif
   async int f() { return 1; }
   ```
   Output: identical to the input, rc=0. Same with `#define MSG don't` on line 1.

3. **A digit separator inside an async body is a hard error.**
   `async int f() { int n = 1'000; return n; }` gives
   `digitsep_async.cpp:1:43: error: unterminated string or char literal (unescaped newline)`.

Before T-0009 this hit only `.async.cpp` files. Under the launcher
(T-0014) every TU and every project header is scanned, so (1) and (2)
become silent wrong builds in ordinary code.

## Scope

- In:
  - A tokenizer that produces a flat token vector for a whole buffer.
  - Port `find_async_markers` and `parse_one`/`parse_file` onto it.
  - Delete `token.hpp`'s unused type (replaced by the new one).
  - Keep the marker rule and the name heuristic **behaviour-identical**
    except for the lexing fixes above.
- Out:
  - Classifying which `async` tokens are markers. That is T-0004, which depends on this ticket.
  - Signature grammar: declarations, qualified names, bounded search (T-0006); `noexcept`/`->` (T-0002).
  - `lower_body`, which keeps using `SourceCursor` until T-0011. The two co-exist for one ticket only.
- Touches:
  - `transpiler/include/asynctp/tokenizer.hpp` (new)
  - `transpiler/src/tokenizer.cpp` (new)
  - `transpiler/include/asynctp/token.hpp` (deleted or replaced)
  - `lexer.hpp/.cpp`, `async_parser.hpp/.cpp`
  - `transpiler/CMakeLists.txt` and `Makefile` (new source and new test suite)
  - `transpiler/tests/tokenizer_tests.cpp` (new), `lexer_tests.cpp`, `parser_tests.cpp`

## Blueprint

1. **Token model** (`tokenizer.hpp`, namespace `asynctp`):
   ```cpp
   enum class TokKind { Identifier, Number, String, Char, Punct, Directive, Comment, Invalid };
   struct Tok { TokKind kind; std::size_t begin; std::size_t end; }; // [begin,end) into the source
   struct TokenizedFile { std::string_view source; std::vector<Tok> toks;
                          std::vector<std::size_t> line_starts; };     // for offset -> line/col
   std::variant<TokenizedFile, ParseError> tokenize(std::string_view source);
   SourceLocation location_of(const TokenizedFile&, std::size_t offset); // 1-based, binary search on line_starts
   std::string_view text(const TokenizedFile&, const Tok&);
   ```
   - Whitespace is not a token; gaps between tokens are whitespace.
   - Keep `ParseError` in `async_parser.hpp` and include it. Moving it to a
     small `diagnostics.hpp` is fine if it avoids a cycle.
2. **Lexing rules** (one pass, offsets only, no copies):
   - **Identifier:** `[A-Za-z_$]` or any byte >= 0x80, then the same set plus digits.
     Non-ASCII bytes are consumed so UTF-8 identifiers and stray bytes never split into garbage.
   - **Number:** a pp-number per [lex.ppnumber]. It starts with a digit, or `.` followed by a digit, and continues over:
     - digits and identifier characters;
     - `'` followed by a digit or identifier character (digit separator);
     - `e+`, `e-`, `E+`, `E-`, `p+`, `p-`, `P+`, `P-`;
     - `.`

     So `1'000'000`, `0x1'Fu`, `1.5e+3f` and `.5` are each one token.
   - **String / Char:** an optional encoding prefix (`u8`, `u`, `U`, `L`), then `"` or `'`, with escapes, then an optional ud-suffix (identifier chars).
     - Raw strings: a prefix ending in `R` (`R`, `u8R`, `uR`, `UR`, `LR`) immediately followed by `"` is lexed as `R"delim(...)delim"`.
     - The prefix is detected while scanning the identifier, not by looking backward (the current `skip_string_or_char_literal` looks backward).
   - **Unterminated ordinary string/char** (newline or EOF before the closing quote): emit an `Invalid` token from the quote to end of line. No error. This mirrors GCC, which warns and continues.
   - **Directive:** `#` as the first non-whitespace character of a logical line starts a `Directive` token. It runs to the end of the logical line:
     - backslash-newline continues it;
     - a `/* ... */` comment that starts inside it extends it across lines;
     - a `//` comment ends at end of line;
     - quotes inside a directive are lexed leniently: an unterminated one never errors.
   - **Comment:** `//` to end of line (respecting backslash-newline continuation), and `/* */`.
   - **Punct:** single characters, except these multi-char tokens: `::` `->` `...` `.*` `->*`.
     Do **not** merge `>>`, `<<`, `<=`, `>=`: T-0003 needs `>` as its own token to close template argument lists. Other multi-char operators are irrelevant to us and stay split.
   - **Hard errors** (`ParseError` with line/col of the opening delimiter), because GCC rejects these too:
     - unterminated `/*` comment: `"unterminated block comment"`
     - unterminated raw string: `"unterminated raw string literal"`
3. **Port `find_async_markers`** (`lexer.cpp`):
   - New signature: `std::vector<std::size_t> find_async_markers(const TokenizedFile&)`.
     It returns the begin offsets of `Identifier` tokens whose text is `async` and that are not inside a `Directive`.
   - The marker rule itself stays as today: every such identifier counts. T-0004 replaces it.
   - Remove the silent `break` on error. Errors can now only come from `tokenize`, which the caller propagates.
4. **Port `parse_one` / `parse_file`** (`async_parser.cpp`):
   - `parse_file` calls `tokenize` once, propagates its error, gets markers, and runs the same heuristic over token indices:
     the first `Identifier` followed by a balanced `(`…`)` group, then `{`, then a balanced `{`…`}` group.
   - Balanced matching counts only `Punct` tokens. Literals, comments and directives are atoms.
   - The first `Invalid` token that `parse_one` meets while scanning forward from a marker raises
     `ParseError{offset, line, col, "unterminated string or character literal"}` at the token's begin.
     This covers the return type, the parameter list and the body balancing.
     - It must win over the generic "could not find a function name…" error. An `Invalid` token swallows the rest of its line, `}` included, so balancing would otherwise run to EOF and report the wrong place.
     - This keeps fail-loudly for async islands, while passthrough text with a GCC-tolerated lone quote is unaffected.
   - Keep the `AsyncFunction` fields and their exact offset semantics (`body_open_offset` is just after `{`, `body_close_offset` is the `}`). The emitter and lowering must not notice the port.
   - Keep the existing error message text for "could not find a function name, parameter list, and body after 'async'".
5. **Tokenize once.** `parse_file` may keep the `TokenizedFile` in `ParsedFile` (new field, `std::shared_ptr` or by value) so T-0011 can reuse it. Optional here, required by T-0011.
6. **Build parity:**
   - Add `src/tokenizer.cpp` to the `asynctp` and `asynctp_core` source lists in `transpiler/CMakeLists.txt`, to `TRANSPILER_SRCS` in the `Makefile`, and to the test `foreach` list.
   - Add `tokenizer_tests` to the CMake `foreach` and as a Makefile target, `all` prerequisite and `test` line.

### Invariants

- **Fail loudly:** strengthened. The silent `break` that truncated the marker list is removed, and lone quotes inside async islands are a `ParseError` with a position.
- **Text-island rewriter:** unchanged. Passthrough bytes are never touched; the tokenizer only reads.
- **Zero-dependency:** standard library only.
- **Makefile/CMake parity:** one new source and one new suite go into both (step 6).
- **Transpiler/runtime independence, header-only runtime, lazy Task:** untouched.

## Acceptance criteria

- [ ] New suite `tokenizer_tests` (CMake + Makefile), one `test_` function per case, asserting kinds and `[begin,end)`:
  - `1'000'000`, `0x1'Fu`, `1.5e+3f`, `.5` each tokenize as one `Number`.
  - `u8"a"`, `L'x'`, `"s"_sv` and `R"x(a)"b)x"` each tokenize as one `String`/`Char`.
  - `#define A don't\nint x;` gives one `Directive`, then `int`, `x`, `;`.
  - `#if 0\nthis isn't\n#endif` gives `Directive`, `this`, `Invalid` (`'t` to end of line), `Directive`, with no error.
  - A multi-line `#define F(x) \\\n  x` is one `Directive`.
  - `a::b->c...` gives `a`, `::`, `b`, `->`, `c`, `...`; `x>>y` gives `x`, `>`, `>`, `y`.
  - Unterminated `/*` and an unterminated raw string each return a `ParseError` with the opening line/col.
  - Non-ASCII `int café = 1;` gives `café` as one `Identifier`.
- [ ] `parser_tests`:
  - `constexpr int kMax = 1'000;\nasync int f() { return kMax; }` parses one function named `f`. Today: zero functions.
  - `#if 0\nthis isn't compiled\n#endif\nasync int f() { return 1; }` parses one function. Today: zero.
  - `#define MSG don't\nasync int f() { return 1; }` parses one function.
  - `async int f() { auto s = 'x; return 1; }` returns a `ParseError` at line 1, col 26 (the stray quote), message containing "unterminated".
  - Every existing `parser_tests` case still passes unchanged.
- [ ] `lexer_tests`: every existing case passes, adapted to the new `find_async_markers(const TokenizedFile&)` signature. Add one: `#define X async\nasync int f() {}` reports exactly one marker (the second).
- [ ] Golden fixture `digit_separators`:
  - Input: `constexpr int kMax = 1'000;` followed by `async int f() { return kMax; }`.
  - Expected: the runtime include line, the unchanged first line, then `::asyncrt::Task<int> f() { co_return kMax; }`.
  - Register it in `golden_tests.cpp` `main()`.
- [ ] `grep -rn "find_async_markers(std::string_view" transpiler/` returns nothing.
- [ ] Header comments:
  - `lexer.hpp`'s comment no longer claims literal/comment skipping is done by `SourceCursor`.
  - `tokenizer.hpp` documents the `Invalid`-token policy and why it exists (GCC tolerates lone quotes; asynctp only errors inside async islands).
- [ ] README "Known MVP limitations": no change. These bugs were never listed; the list is rewritten in T-0017.
- [ ] Makefile and CMake both build `tokenizer.cpp` and run `tokenizer_tests`.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

- Review findings closed here: H5 (digit separators), N5 (preprocessor lines, at file level), and the lexing half of M2 (shared scanner). The body half of M2 is closed by T-0011.
- Not a full C++ lexer: no trigraphs, no phase-2 splicing outside directives and `//` comments, no digraph handling (`<:` stays `<`, `:`). Digraphs before `.await` are vanishingly rare. Document this in `tokenizer.hpp`.
- `$` in identifiers is a GCC/Clang extension that appears in some codebases; accepting it costs nothing.

## Log

- 2026-10-17 architect: created under epic T-0009; reproduced the silent marker truncation (digit separator, `#if 0` prose, `#define` apostrophe) and the in-body digit-separator error; ready.
- 2026-10-18 developer: added `tokenizer.{hpp,cpp}` (flat `Tok` stream, Invalid-token policy, hard errors for unterminated `/*` and raw strings) and `diagnostics.hpp` (`ParseError`/`SourceLocation`, to avoid an include cycle once `ParsedFile` holds the `TokenizedFile`); ported `find_async_markers(const TokenizedFile&)` and `parse_one`/`parse_file` to token indices; deleted `token.hpp`; new `tokenizer_tests` suite (CMake + Makefile), new lexer/parser cases, golden `digit_separators`. In-body digit separators still fail in `lower_body` (T-0011). `verify.sh all` green; in-review.
- 2026-10-19 tester: FAIL. All 9 criteria have tests and pass, but there is a regression from HEAD: `skip_directive` reads quotes leniently up to the physical newline, so a raw string that spans lines inside a `#define` (GCC keeps the whole literal in the directive; checked with `g++ -E`) ends the Directive early. The literal's contents then lex as code. (a) `async int g() {...}` inside it gets lowered, which rewrites the string's bytes and breaks the text-island rule. (b) Inside an async body, a `}` in such a literal closes the body early. HEAD's `SourceCursor` skipped these raw strings. Repro tests added (red): tokenizer `test_directive_with_multiline_raw_string_is_one_directive`, parser `test_multiline_raw_string_in_define_inside_body` and `test_multiline_raw_string_in_define_hides_async`, and golden `raw_string_in_define`. Also added passing tests: golden `literals_around_signature` and tokenizer `test_directive_with_crlf_splice`. Back to in-progress.
- 2026-10-19 developer: `skip_directive` now skips a raw string (any R-prefix) with `skip_raw_string`, so a multi-line raw string stays inside its Directive; an unterminated one is a hard error at the prefix (`error_message_` replaces the hard-coded "block comment" message). Tester's 4 red tests now pass; added tokenizer `test_unterminated_raw_string_in_directive_is_error_at_opening` and `test_directive_with_raw_prefix_identifier_not_raw`; `tokenizer.hpp` Directive/hard-error docs updated. `verify.sh all` green; in-review.
- 2026-10-19 tester: PASS. 9/9 criteria met and tested; `verify.sh all` OK. Raw-string-in-directive fix holds: multi-line raw strings in `#define`, `u8R`/`LR` prefixes, custom delimiters, and an unterminated raw string in `#error`/`#warning` (live or under `#if 0`) all match `g++ -E` on whether it errors and at which line:col. Lone quotes in directives swallowing `/*`, `{`/`}` in directives inside bodies, comments before `#`, UTF-8 byte columns, and comments between the name and `(` also behave correctly, and the lowered output compiles. Added passing regression tests: tokenizer `test_lone_quote_in_directive_swallows_block_comment_opener`, `test_comment_before_hash_keeps_directive`, `test_unterminated_raw_string_in_error_directive_matches_gcc_position`; parser `test_brace_in_directive_inside_body_is_opaque`, `test_lone_quote_in_if0_inside_body_is_error_at_quote`. Out-of-scope findings handed to the architect: UTF-8 BOM before a line-1 directive, `co_return {a, b}`, a lone quote in `#if 0` text inside an async body, and `#` after a mid-line multi-line comment.
