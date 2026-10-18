---
id: T-0004
title: Recognise async only at declaration starts; leave std::async and other identifiers alone
status: ready
area: transpiler
type: bug
priority: P1
depends_on: [T-0010]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0010 (token stream).

## User story

As a C++ developer whose every source file now passes through asynctp, I
want `std::async(std::launch::async, f)`, `obj.async`, and a variable named
`async` to compile exactly as they do without asynctp, so that adopting
dotawait never breaks code that has nothing to do with it.

## Problem

`find_async_markers` treats every identifier spelled `async` as the
keyword. Under the old contract this hit only `.async.cpp` files. Under the
launcher contract (T-0009 / T-0014) it hits every TU. `<future>`'s
`std::async` and `std::launch::async` are standard C++, so this is now a
build break in ordinary code: priority raised from P2 to P1.

Observed with `build/transpiler/asynctp <in> -o <out> --no-line-directives`:

```cpp
#include <future>
int g() { auto f = std::async(std::launch::async, []{ return 1; }); return f.get(); }
```
```
stdasync.cpp:2:25: error: could not find a function name, parameter list, and body after 'async'
```

During the launcher prototype, a plain `plain.cpp` calling
`std::async(std::launch::deferred, ...)` made the whole CMake build fail
with `plain.cpp:2:27: error: could not find a function name ...`.

Silent wrong output (rc=0) when a later function happens to fit the name
heuristic:

```cpp
int async = 3; int g() { return async; }
```
```cpp
int ::asyncrt::Task<= 3; int> g() { co_return async; }
```

## Scope

- In:
  - A classification rule deciding, per `async` identifier token, whether it is a **marker**, a **plain identifier** (passed through), or **ambiguous** (a `ParseError` explaining what to do).
  - Port the rule into `find_async_markers`.
- Out:
  - `#define async ...` macro tricks. Directive contents are never classified, per T-0010.
  - Async lambdas. The future `async [` form belongs to T-0008, which must extend this rule.
  - A `requires`-clause between `template<...>` and `async`: reported as ambiguous (loud), not supported.
- Touches:
  - `transpiler/src/lexer.cpp` and `transpiler/include/asynctp/lexer.hpp`
  - `transpiler/src/async_parser.cpp` (it now receives classified markers and propagates ambiguity errors)
  - `lexer_tests`, `parser_tests`, golden fixture, README.

## Blueprint

All "previous"/"next" below mean the nearest token skipping `Comment`
tokens. A `Directive` token counts as a statement boundary.

1. `find_async_markers` returns
   `std::variant<std::vector<std::size_t>, ParseError>` (offsets of markers only).
2. For each `Identifier` token with text `async`:
   1. **Previous token is `.`, `->`, `::`, `.*` or `->*`:** plain identifier.
      This covers `std::async`, `std::launch::async`, `obj.async`, `p->async()`.
   2. **Next token is one of `(` `)` `=` `;` `,` `.` `->` `[` `]` `}` `?` `:` `+` `-` `*` `/` `%` `&` `|` `^` `<` `>` `!` `~`, or EOF:**
      plain identifier. Identifier uses: `async(x)`, `async = 3`, `return async;`, `f(async)`.
      Exception: `[` at a declaration start is reserved for T-0008; until then it is a plain identifier too.
   3. **Previous token is a declaration boundary and the next token can start a type** (`Identifier` or `::`): **marker**. Declaration boundaries are:
      - start of file, `;`, `{`, `}`, a `Directive`;
      - `:` immediately preceded by `public`, `private` or `protected`;
      - the `>` that closes a `template < ... >` header (balance `<`/`>` backward from it to a `<` preceded by `template`);
      - the `]` closing an attribute `[[ ... ]]`;
      - one of the decl-specifier identifiers `static`, `inline`, `virtual`, `friend`, `extern`, themselves preceded (recursively) by a declaration boundary.
   4. **Otherwise, if the next token can start a type:** **ambiguous**. Return
      `ParseError{offset, line, col, "'async' must start a declaration (put it before the return type, after any template<...>, static, inline, virtual, friend, extern or [[attributes]]); it cannot follow '<prev token text>'"}`.
      Example: `template<class T> requires C<T> async T f()`.
   5. **Otherwise:** plain identifier (for example `int async = 3;`, where the previous token is `int` and the next is `=`).
3. Precedence: apply rule 1 first, then 2, then 3, then 4, then 5. Write a
   table-driven unit test for every row.
4. `parse_file` propagates the `ParseError` from step 2.4. The existing
   "could not find a function name…" error still fires when a marker's
   signature does not parse. T-0006 later sharpens that message.
5. Update the `lexer.hpp` comment (it currently documents the limitation
   that every `async` is a marker) to describe the rule and its one
   remaining restriction: a **type** literally named `async` at a
   declaration start is read as the marker.

### Invariants

- **Fail loudly:**
  - Before, a misclassification produced silent garbage. Now every token is a marker (parse or `ParseError`), a provably non-declaration use (passthrough), or an explicit `ParseError` (rule 4).
  - No path silently drops a real marker. Rule 4 catches markers in unexpected positions instead of passing them to GCC as `'async' does not name a type`.
- **Text-island rewriter:** plain-identifier `async` is now passthrough byte-for-byte, which is a strict improvement.
- **Other invariants:** not affected. Transpiler-only change; no new files, so no Makefile/CMake change.

## Acceptance criteria

- [ ] `lexer_tests`, table-driven, one row per case, asserting marker / plain / error:
  - `std::async(f)`, `std::launch::async`, `obj.async`, `p->async()`, `x.*async` give plain.
  - `int async = 3;`, `async = 4;`, `f(async);`, `return async;`, `async(1);` give plain.
  - `async int f() {}` at file start, after `;`, after `}`, after a `Directive`, inside `namespace n {`, after `public:`, after `template <typename T>`, after `template <class T, int N = (1>2)>`, after `[[nodiscard]]`, after `static`, after `inline`, after `virtual`, after `friend` all give marker.
  - `template<class T> requires C<T> async T f() {}` gives a `ParseError` whose message contains `must start a declaration` and the col of `async`.
  - `int x = 1; constexpr async int f() {}` gives a `ParseError` (`constexpr` is not in the boundary list).
- [ ] `parser_tests`:
  - `#include <future>\nint g() { auto f = std::async(std::launch::async, []{ return 1; }); return f.get(); }` parses with zero functions and no error.
  - `int async = 3; int g() { return async; }` parses with zero functions.
- [ ] Golden fixture `std_async_passthrough`:
  - Input: the `<future>` example above, followed by `async int h() { return g(); }`.
  - Expected: the runtime include, the `<future>` lines byte-for-byte, then `::asyncrt::Task<int> h() { co_return g(); }`.
- [ ] Compile check (in the golden test or an example): the expected output of `std_async_passthrough` compiles with `-std=c++20 -Wall -Wextra` against `runtime/include`.
  This can be done in `examples/` as a runnable example `std_async_mix` that calls both and returns 0. If so, add it to both `examples/CMakeLists.txt` and the Makefile `EXAMPLES`.
- [ ] README "Known MVP limitations": replace the first bullet with "a *type* named `async` cannot appear at the start of a declaration (it is read as the keyword); identifiers named `async` elsewhere, including `std::async`, are left alone". Update the `lexer.hpp` comment to match.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

- Superseded scope: this ticket originally (2026-10-17) proposed a looser rule. The rule above is stricter because, under T-0009, false positives break ordinary code.
- T-0008 (async lambdas) must extend rule 2's `[` exception. Recorded in T-0008's Log.

## Log

- 2026-10-17 architect: reproduced; proposed.
- 2026-10-17 architect: re-scoped under epic T-0009. Every TU now goes through asynctp, so `std::async` in a plain file broke a prototype launcher build. Raised to P1, specified a full classification table with an ambiguity `ParseError`, made it depend on T-0010, and moved it to ready.
