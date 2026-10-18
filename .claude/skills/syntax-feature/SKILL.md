---
name: syntax-feature
description: End-to-end playbook for changing what .async.cpp syntax the asynctp transpiler accepts or how it lowers it (new signature forms, new keywords/markers, fixing a lexer/parser/lowering heuristic, lifting a "Known MVP limitation"). Use when implementing or designing any transpiler grammar or lowering change.
---

# Changing the async syntax

The pipeline, and where each kind of change belongs:

| Stage | Files | Responsibility |
|---|---|---|
| Scanner | `scanner.{hpp,cpp}` | `SourceCursor`: skip whitespace/comments/string+char literals, balanced matching, line/col |
| Lexer | `lexer.{hpp,cpp}`, `token.hpp` | `find_async_markers`: where `async` marker keywords are |
| Parser | `async_parser.{hpp,cpp}`, `ast.hpp` | `parse_file`: carve each marked function into `AsyncFunction` (verbatim `string_view` slices) |
| Lowering | `lowering.{hpp,cpp}` | `lower_body` (`.await` -> `co_await (...)`, `return` -> `co_return`), `lower_function` (signature -> `::asyncrt::Task<T>`) |
| Emitter | `emitter.{hpp,cpp}` | Stitch passthrough text + lowered functions, runtime include, `#line` directives |
| CLI | `main.cpp` | Flags only. Keep logic out of it. |

## Steps

1. **Reproduce** with the real binary on a minimal input and keep it. It
   becomes the first test.
2. **Pick the lowest stage that can own the change.** Literal/comment
   awareness belongs in the scanner, not in ad-hoc character checks in
   lowering. Marker classification belongs in the lexer. Signature shape
   belongs in the parser.
3. **Tests first, at that stage's level:** `lexer_tests` / `parser_tests` /
   `lowering_tests` (string-in, string-out). Then add a golden fixture for
   the CLI-visible result (`golden-test` skill).
4. **Error paths.** Any input the new heuristic can't handle must produce a
   `ParseError` with an accurate `line:col` and a message that tells the
   user what to write instead. Add a test asserting the error and its
   position. Silent wrong output is never acceptable.
5. **Passthrough.** Text outside async function spans must come out
   byte-for-byte unchanged, including comments and whitespace. `#line`
   directives must still map to the original lines (spot-check with
   `asynctp` without `--no-line-directives`).
6. **Compile the output.** Run lowered text through
   `g++ -std=c++20 -Iruntime/include` and execute it when behavior matters.
   For anything reachable from user code, add a case to an example or
   test that runs.
7. **Document.** Update the doc comment on the function that owns the
   heuristic (that is where limitations are recorded) and README "Known MVP
   limitations" (remove, narrow, or add the bullet). If the feature is
   user-facing, also update README's usage section.
8. **Verify:** `verify.sh full`, and `verify.sh all` before done.

## Pitfalls specific to this codebase

- `AsyncFunction` fields are views into the source buffer. Never store views
  into a temporary `std::string`.
- `lower_body` tracks lambda frames (`frame_is_lambda`, `lambda_depth`):
  rewrites only happen at `lambda_depth == 0`. Any new `{` handling must push
  and pop that stack symmetrically.
- The `.await` operand is found by a backward scan bounded by
  `max(last_copied, comment_floor)`. Moving work to the forward pass is
  usually safer than making the backward scan smarter.
- `obj.await()` (with parens) is a real method call and must stay untouched.
