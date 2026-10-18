---
id: T-0011
title: Port lower_body to the token stream and delete SourceCursor
status: ready
area: transpiler
type: chore
priority: P1
depends_on: [T-0010]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0010.

## User story

As a C++ developer, I want lowering errors to point at the real line in my
file, and digit separators and `#if` blocks inside async bodies to just
work, so that the transpiler never makes me debug the transpiler.

## Problem

After T-0010 the file-level stages use the token stream, but `lower_body`
(`transpiler/src/lowering.cpp`) still re-scans the body **substring**
with its own `SourceCursor`. Its helpers `looks_like_lambda_start` and
`find_expression_start_backward` walk raw characters backward. Observed
consequences:

1. **Body-relative error positions (review finding H2).** Input:
   ```cpp
   // line 1
   // line 2

   async int f() {
     int a = 0;
     auto l = [a] ;
     return a;
   }
   ```
   asynctp reports `h2b.cpp:3:12: error: unterminated lambda expression (no '{' found after capture list)`.
   The `[` is at **6:12**. `SourceCursor` starts counting at the body's first byte.
2. **Digit separator in a body is a hard error:**
   `async int f() { int n = 1'000; return n; }` gives
   `1:43: error: unterminated string or char literal (unescaped newline)`.
3. **The backward `.await` scan does not understand literals:**
   `async int f() { auto s = g(")").await; return 0; }` gives
   `::asyncrt::Task<int> f() { auto s = g(")")co_await (); co_return 0; }` (silent; this was T-0003's original repro).
4. Two scanners co-exist, the tokenizer and `SourceCursor` (finding M2).

## Scope

- In:
  - Re-implement `lower_body` over the file's token vector, with file-absolute offsets.
  - Port the lambda-start check and the `.await` operand scan to tokens with **the same rules as today**. Tokens make literals and directives atomic automatically.
  - Delete `scanner.hpp` / `scanner.cpp`.
- Out:
  - New `.await` operand grammar (chains `a().await.b().await`, template arguments, keyword operands). That is T-0003.
  - The lambda keyword table. That is T-0005.
  - Source map / `#line`. That is T-0012.
- Touches:
  - `lowering.hpp/.cpp`, `emitter.cpp` (call site)
  - `async_parser.hpp` (`ParsedFile` keeps its `TokenizedFile`)
  - deletion of `scanner.hpp/.cpp`, `transpiler/CMakeLists.txt`, `Makefile`
  - `lowering_tests.cpp`, `lexer_tests.cpp`, `tokenizer_tests.cpp`

## Blueprint

1. **Keep the tokens.** `ParsedFile` gains `TokenizedFile tokens;`
   (filled by `parse_file`, T-0010 step 5). Each `AsyncFunction` gains
   `std::size_t body_tok_begin, body_tok_end;`: indices of the first token
   after `{` and of the matching `}` token.
2. **New lowering API:**
   ```cpp
   std::variant<std::string, ParseError> lower_body(const ParsedFile&, const AsyncFunction&);
   std::variant<std::string, ParseError> lower_function(const ParsedFile&, const AsyncFunction&);
   ```
   - Output is assembled exactly as today: verbatim source slices between rewritten tokens, so whitespace and comments survive byte-for-byte.
   - Every `ParseError` uses `location_of(tokens, offset)` with the file offset, which fixes H2 by construction.
3. **Port the rules one-for-one**, walking `toks[body_tok_begin, body_tok_end)`:
   - **Frames:** `{` pushes a non-lambda frame and `}` pops. A lambda's body `{` pushes a lambda frame and increments `lambda_depth`.
   - **`return` → `co_return`:** apply when `lambda_depth == 0` and the token is `Identifier` `return`.
     - If T-0001 has landed, there is no statement-start condition.
     - If it has not, port `at_statement_start` faithfully: it is true after `;`, `{` and `}` tokens.
   - **Lambda start at a `[` Punct.** Look at the previous non-`Comment` token:
     - none, or a `Directive`: lambda;
     - `)` or `]`: subscript;
     - other `Punct`: lambda;
     - `Identifier` in {`return`, `throw`, `co_return`}: lambda, otherwise subscript;
     - **`String`, `Char` or `Number`: subscript** (new, but it is what token semantics imply; it fixes `")"[0]`, which today fails with "unterminated lambda expression").
     - Skip the lambda header by balancing `(`/`[` Punct tokens up to the body `{`. Keep today's error text for a missing `{`, reported at the `[`.
   - **`.await` (`.` then `Identifier` `await`, with the next token not `(`):**
     - Operand start: walk tokens backward from the `.`. Consume `Identifier`, `.`, `::`, `->` and balanced `)…(` / `]…[` groups, matching Punct only.
     - Stop at any other token, at a `Comment` or `Directive` (this replaces today's `comment_floor`), or at the last rewritten token (today's `last_copied`).
     - If the operand would be empty, return `ParseError{offset_of_dot, …, "'.await' needs an expression before it"}` instead of emitting `co_await ()`.
   - **Directive tokens inside a body** (`#if`, `#else`, `#endif`, `#define`) are copied verbatim and never rewritten inside.
     A `return` on a code line between `#if` and `#else` is still rewritten, as before.
4. **Delete** `transpiler/include/asynctp/scanner.hpp` and `transpiler/src/scanner.cpp`:
   - remove them from both source lists in `transpiler/CMakeLists.txt` and from `TRANSPILER_SRCS` in the `Makefile`;
   - remove `SourceLocation`/`ScanError` users (T-0010 introduced `location_of`).
5. **Tests that used `SourceCursor` directly** (`lexer_tests.cpp`, the first ~10 cases): move each behaviour they check (comment skipping, escaped quotes, raw strings, identifier scanning, unterminated literal) to an equivalent `tokenizer_tests` case, then delete the `SourceCursor` cases. No behaviour may lose coverage: list the mapping in the PR description.
6. **Update `lower_single_body`** in `lowering_tests.cpp` to call the new API. The expected strings of every existing test stay **unchanged**: that is the regression proof for the port.

### Invariants

- **Fail loudly:** improved. Error positions become file-absolute; the empty-operand case becomes a `ParseError`; literal-swallowing becomes impossible.
- **Text-island rewriter:** output is still built from verbatim slices. Only `return`, and `.await` plus its operand, are rewritten.
- **Makefile/CMake parity:** two files are removed from both builds. No new suite.
- **Other invariants:** not affected.

## Acceptance criteria

- [ ] All existing `lowering_tests` cases pass with their current expected strings, through the updated helper.
- [ ] New `lowering_tests`:
  - `async int f() { int n = 1'000; return n; }` gives `" int n = 1'000; co_return n; "`.
  - `async int f() { auto s = g(")").await; return 0; }` gives `" auto s = co_await (g(\")\")); co_return 0; "`.
  - `async int f() { auto c = ")"[0]; return c; }` gives `" auto c = \")\"[0]; co_return c; "`.
  - Body with directives: `async int f() {\n#if 1\n  return 1;\n#else\n  return 2;\n#endif\n}` gives both `return`s rewritten and the directive lines byte-identical.
  - `async int f() {\n#define R return\n  return 0;\n}` gives the `#define` line unchanged and the second line `  co_return 0;`.
  - `async int f() { return .await; }` gives a `ParseError` whose message contains `needs an expression`.
- [ ] `parser_tests` or `lowering_tests` (H2): the exact `h2b` input from Problem returns a `ParseError` with `line == 6 && col == 12`.
- [ ] `grep -rn "SourceCursor\|scanner.hpp" transpiler/` returns nothing.
- [ ] `tokenizer_tests` contains the migrated `SourceCursor` behaviours (step 5).
- [ ] Golden fixtures `simple_fn` and `nested_lambda` are unchanged (no regeneration).
- [ ] `lowering.hpp` comment: the "does not skip over string literals" limitation is removed (now false). The keyword-limited lambda bullet stays until T-0005.
- [ ] README: remove the "`.await`'s backward expression scan doesn't skip over string literals" bullet, or merge it into T-0003's narrower wording if T-0003 lands first.
- [ ] Makefile and CMake both stop building `scanner.cpp`.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

Closes review findings H2 and the body half of M2. Partly closes T-0003 (string literals). T-0003 keeps the operand grammar.

## Log

- 2026-10-17 architect: created under epic T-0009; reproduced H2 (3:12 vs 6:12), the in-body digit-separator error, and the `g(")").await` corruption; ready.
