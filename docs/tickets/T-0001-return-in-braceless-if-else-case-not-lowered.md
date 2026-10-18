---
id: T-0001
title: return in braceless if/else/loop/case is not lowered to co_return
status: ready
area: transpiler
type: bug
priority: P1
depends_on: []
created: 2026-10-17
---

Epic: T-0009 (related, independent). Depends on: none. Land before T-0010/T-0011 so the token port inherits this behaviour and its tests.

## User story

As a C++ developer writing `.async.cpp`, I want `if (x) return a;` to work
exactly like it does in ordinary C++ so that I don't have to brace every
early return in an async function.

## Problem

`asynctp` rewrites `return` to `co_return` only when the keyword is the
first token of a statement. In `lower_body`
(`transpiler/src/lowering.cpp`, function `lower_body`, currently line 232):

```cpp
if (lambda_depth == 0 && at_statement_start && id == "return") {
```

`at_statement_start` is set to `true` only after `;`, `{`, `}`, and a
lambda body's `{`. Every other token clears it, including `)` (so
`if (c)`, `while (c)`, `for (;;)`), any identifier (`else`, `do`, `case`,
`default`, a label name), and `:` (`case 1:`, `default:`, `label:`). A
`return` after any of those is left as a plain `return`.

Repro (input `br.async.cpp`):

```cpp
#include <asyncrt/asyncrt.hpp>
async int pick(int k) {
  switch (k) {
    case 1: return 10;
    default: return 0;
  }
}
async int g(bool c) { if (c) return 1; else return 2; }
async void h(int n) { while (n) return; for (;;) return; do return; while (n); }
int main() { return asyncrt::block_on(g(true)) == 1 ? 0 : 1; }
```

Actual output of `build/transpiler/asynctp br.async.cpp -o /dev/stdout --no-line-directives`
(no `return` was rewritten):

```cpp
#include <asyncrt/asyncrt.hpp>
#include <asyncrt/asyncrt.hpp>
::asyncrt::Task<int> pick(int k) {
  switch (k) {
    case 1: return 10;
    default: return 0;
  }
}
::asyncrt::Task<int> g(bool c) { if (c) return 1; else return 2; }
::asyncrt::Task<void> h(int n) { while (n) return; for (;;) return; do return; while (n); }
int main() { return asyncrt::block_on(g(true)) == 1 ? 0 : 1; }
```

(The first `#include` line is the emitter's normal prelude, not part of this bug.)

When a function mixes the two forms, e.g.
`async int g(bool c) { if (c) return 1; return 2; }`, the output is
`::asyncrt::Task<int> g(bool c) { if (c) return 1; co_return 2; }` and
GCC 15 reports:

```
min.async.cpp:2:48: error: could not convert '1' from 'int' to 'asyncrt::Task<int>'
min.async.cpp:2:22: error: a 'return' statement is not allowed in coroutine; did you mean 'co_return'?
```

The user wrote valid code in the `.async.cpp` and gets an error about a
`return` they never meant to be different from the others.

Affected forms: `if (...) return`, `else return`, `while (...) return`,
`for (...) return`, `do return ...; while (...)`, `case K: return`,
`default: return`, `label: return`.

## Scope

- In: every `return` keyword at `lambda_depth == 0` in an async function
  body is rewritten to `co_return`, wherever it appears.
- Out (unchanged behavior):
  - `return` inside a nested ordinary lambda that is detected as a lambda
    stays `return`, braced or not.
  - Lambdas that `looks_like_lambda_start` fails to detect (e.g. a lambda
    right after `)` or after a keyword other than `return`/`throw`/`co_return`)
    are a separate known limitation, tracked in T-0005. Their bodies are
    already mis-lowered today when a `return` starts a statement; this
    ticket does not make that worse in kind and does not fix it.
  - No change to `.await` lowering, the parser, the emitter, or the runtime.
- Touches: `transpiler/src/lowering.cpp`, `transpiler/include/asynctp/lowering.hpp`
  (comment only), `transpiler/tests/lowering_tests.cpp`,
  `transpiler/tests/golden_tests.cpp`, new files under
  `transpiler/tests/golden/`, new `examples/braceless_return.async.cpp`,
  `examples/CMakeLists.txt`, `Makefile`.

## Blueprint

1. In `lower_body` (`transpiler/src/lowering.cpp`), change the rewrite
   condition in the identifier branch from
   `lambda_depth == 0 && at_statement_start && id == "return"` to
   `lambda_depth == 0 && id == "return"`.
   This is safe because `return` is a reserved keyword and
   `SourceCursor::scan_identifier_if_present` consumes a whole identifier,
   so `co_return`, `returned`, `my_return` never compare equal to
   `"return"`. String/char literals and comments are already skipped
   before the identifier branch is reached.
2. Remove `at_statement_start`. It is read only by that one condition
   (verified: `grep -n at_statement_start transpiler/` shows one read at
   line 232 plus the declaration at line 110 and 12 assignments). Delete
   the declaration and every assignment; leave the surrounding control flow
   (`advance_one`, `continue`, frame push/pop) unchanged. The build must
   stay warning-free under `-Wall -Wextra`.
3. Update the `lower_body` comment in `transpiler/include/asynctp/lowering.hpp`
   so it says `return` is rewritten in any statement position, including
   braceless `if`/`else`/loop bodies and after `case`/`default`/labels.
   Don't change the "Known MVP limitations" list in that comment.
4. Add the tests, golden fixture, and runnable example listed under
   Acceptance criteria. The example goes into both `examples/CMakeLists.txt`
   (`add_async_executable` + `add_test`) and the Makefile's `EXAMPLES`
   variable.

Prototype check (done during review on a scratch copy, not committed):
with steps 1 and 2 applied, the repro above lowers every `return` to
`co_return`, the result compiles with `-Wall -Wextra` and exits 0, and
every existing `lowering_tests` case still passes.

### Invariants

- Fail loudly: the change removes a heuristic instead of adding one. No
  new silent-misfire path: the only `return` tokens that reach the
  comparison are real keywords outside detected lambdas.
- Text-island rewriting: only the `return` keyword inside async bodies
  changes; bytes outside async spans are untouched.
- Transpiler/runtime independence, zero-dependency, header-only runtime,
  lazy `Task`/symmetric transfer: not affected (transpiler-only change, no
  new includes). The new example lives in `examples/`, which is allowed to
  use both.
- Makefile/CMake parity: the new example must be registered in both
  (step 4). No new transpiler source files or test suites.

## Acceptance criteria

- [ ] `lowering_tests` (`transpiler/tests/lowering_tests.cpp`): new test
      functions, each registered in `main()`, using `lower_single_body`
      and `CHECK_EQ` on the exact lowered body:
  - `async int f(bool c) { if (c) return 1; return 2; }` ->
    `" if (c) co_return 1; co_return 2; "`
  - `async int f(bool c) { if (c) return 1; else return 2; }` ->
    `" if (c) co_return 1; else co_return 2; "`
  - `async void f(int n) { while (n) return; }` -> `" while (n) co_return; "`
  - `async int f() { for (;;) return 1; }` -> `" for (;;) co_return 1; "`
  - `async void f(int n) { do return; while (n); }` ->
    `" do co_return; while (n); "`
  - `async int f(int k) { switch (k) { case 1: return 1; default: return 0; } }` ->
    `" switch (k) { case 1: co_return 1; default: co_return 0; } "`
  - `async int f() { again: return 1; }` -> `" again: co_return 1; "`
  - `async int f(bool c) { if (c) return g().await; return 0; }` ->
    `" if (c) co_return co_await (g()); co_return 0; "` (`.await` still
    lowered after a braceless `return`)
  - Nested lambda untouched:
    `async int f(bool c) { auto l = [](bool c) { if (c) return 1; return 2; }; if (c) return l(c); else return 0; }` ->
    `" auto l = [](bool c) { if (c) return 1; return 2; }; if (c) co_return l(c); else co_return 0; "`
  - Each of these fails on the current code (the braceless `return` stays `return`).
- [ ] Golden fixture `transpiler/tests/golden/braceless_return.async.cpp` +
      `braceless_return.expected.cpp`, registered with
      `run_case(asynctp_path, golden_dir, "braceless_return");` in
      `golden_tests.cpp` `main()`. Input:

      ```cpp
      async int classify(int k) {
          switch (k) {
              case 0: return 100;
              case 1:
                  return fetch(k).await;
              default: return -1;
          }
      }

      async int sign(int x) {
          if (x > 0) return 1;
          else if (x < 0) return -1;
          else return 0;
      }
      ```

      The expected file is the `--no-line-directives` output: the emitter
      prelude line `#include <asyncrt/asyncrt.hpp>`, then the same text with
      `async int` -> `::asyncrt::Task<int>`, every `return` ->
      `co_return`, and `fetch(k).await` -> `co_await (fetch(k))`. Generate
      it with the fixed `asynctp` and check it by eye against that rule
      before committing it.
- [ ] Runnable example `examples/braceless_return.async.cpp`, built with
      `add_async_executable(braceless_return_example braceless_return.async.cpp)`
      and `add_test(NAME braceless_return_example COMMAND braceless_return_example)`
      in `examples/CMakeLists.txt`, and `braceless_return` added to
      `EXAMPLES` in the `Makefile`. It must use, with no braces around the
      return: `if`/`else return`, `case`/`default: return`, a loop body
      `return`, and at least one `return x.await;`. `main()` drives the
      tasks with `asyncrt::block_on`, checks every result against an
      expected value, and returns 0 only if all match. This proves the
      lowered C++ compiles and runs, not just that the text looks right.
- [ ] `grep -n at_statement_start transpiler/` returns nothing.
- [ ] `lowering.hpp` comment on `lower_body` updated per Blueprint step 3.
      No README change: this bug was never listed under "Known MVP
      limitations".
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes (includes
      Makefile parity, consumer build, and ASan/UBSan).

## Notes

Found while seeding the backlog by running `asynctp` on small inputs. It
isn't in the README's known limitations because nobody knew about it.

Related: T-0005 (lambda-start detection misses keywords and literals).
Fixing T-0005 is not required for this ticket and this ticket does not
block it.

## Log

- 2026-10-17 architect: reproduced and blueprinted; ready.
- 2026-10-17 architect: review before Jira export. Re-ran repro against current code (line 232 still accurate); confirmed `at_statement_start` has no other reader and made its removal definitive; prototyped fix on a scratch copy (repro compiles and runs, existing lowering_tests pass); added real asynctp/GCC output, invariant section, exact expected strings for lowering_tests, do/while and label cases, golden fixture input, concrete example registration in CMake + Makefile; scoped out T-0005 lambda misdetection. Status stays ready.
- 2026-10-17 architect: triaged under epic T-0009 (new suffix-free/LSP contract). Scope unaffected; stays ready and should ship first. The `.async.cpp` names of its example/golden fixture are renamed later by T-0017, so no change is needed here or in Jira SCRUM-8.
- 2026-10-17 architect: D7 accepted: SCRUM-8 stays as-is and this ships first; the human links SCRUM-8 to the epic in Jira.
