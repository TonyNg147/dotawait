---
id: T-0002
title: Support noexcept and trailing return type on async functions
status: ready
area: transpiler
type: limitation
priority: P1
depends_on: [T-0006]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0006 (declarator parser; this ticket replaces its "need T-0002" tail error branch and updates that test). Implement over the token stream (T-0010), not `SourceCursor`. Must keep T-0012's line-preserving rule if it has landed.

## User story

As a C++ developer, I want to write `async auto f(int x) -> int { ... }` and
`async int f() noexcept { ... }` so that async functions accept the same
signature forms as ordinary functions.

## Problem

The name heuristic in `parse_file` requires `{` immediately after the
balanced parameter list, so both forms fail:

```
async int f(int x) noexcept { return x; }
-> error: could not find a function name, parameter list, and body after 'async'
async auto f(int x) -> int { return x; }
-> error: could not find a function name, parameter list, and body after 'async'
```

Listed in README "Known MVP limitations" (third bullet).

## Scope

- In: optional specifier tail between `)` and `{`: `noexcept`,
  `noexcept(<balanced expr>)`, `-> <type>` (type may contain `<...>`,
  `::`, `(...)`), and member qualifiers `const`, `&`, `&&`, `override`,
  `final` (cheap to allow while touching this code, and needed for async
  member functions defined inline).
- Out: `requires` clauses (separate ticket if wanted). Attributes after
  the parameter list.
- Touches: `ast.hpp`, `async_parser.cpp`, `lowering.cpp` (`lower_function`), tests, README.

## Blueprint

1. `ast.hpp`: add `std::string_view specifiers;` to `AsyncFunction`. It holds
   the verbatim trimmed text between `)` and `{`, excluding a trailing return
   type, plus `std::string_view trailing_return;` (the type after `->`, or empty).
2. `async_parser.cpp`: after the balanced `(...)`, scan a specifier tail of
   identifiers, `&`, `&&`, `->`, and balanced `(...)`/`<...>` groups until `{`.
   Stop with a `ParseError` on `;` or EOF so a declaration is still rejected
   loudly. Keep the existing "fail rather than mis-lower" rule.
3. Semantics of `-> T`: the leading return type must then be `auto`.
   Lower to `::asyncrt::Task<T> name(params) specifiers {`, putting `T` (not
   `auto`) inside `Task<>`. A leading type other than `auto` together with
   `->` is a `ParseError`.
4. `noexcept`: pass it through unchanged. Add a comment that it covers
   creating the coroutine frame and `Task`, not the body. Body exceptions are
   still captured into the promise by `unhandled_exception`. Confirm this
   against `task.hpp` before writing the comment.
5. Update README limitations and the comment in `async_parser.hpp`.

## Acceptance criteria

- [ ] `parser_tests`: `noexcept`, `noexcept(true)`, `-> int`,
      `-> std::vector<std::pair<int,int>>`, `const noexcept`, and error cases
      (`async int f() -> int {`, `async int f();`).
- [ ] `lowering_tests`: lowered signature text for each accepted form.
- [ ] Golden fixture `signature_tail`.
- [ ] Runtime check: a `noexcept` async fn whose body throws still delivers
      the exception to the awaiter (added to an example or `task_tests`).
- [ ] README + header comments updated; `verify.sh all` passes.

## Log

- 2026-10-17 architect: reproduced and blueprinted; ready.
- 2026-10-17 architect: triaged under epic T-0009. Still valid and ready, now ordered after T-0006: the specifier tail plugs into T-0006's declarator parser and works on tokens. Blueprint steps 1-5 still apply, with `parse_one` now token-based; `async int f() -> int {` stays an error (now via the T-0006 return-type check).
