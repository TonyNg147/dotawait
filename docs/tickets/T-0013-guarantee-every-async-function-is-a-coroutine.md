---
id: T-0013
title: Guarantee every async function is a coroutine
status: ready
area: transpiler
type: bug
priority: P1
depends_on: [T-0011]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0011 (token-based lowering); compatible with T-0012's line-preserving rule.

## User story

As a C++ developer, I want `async void bump(int& x) { x++; }` to behave
like any other async function, lazy and awaitable, so that I don't have to
know that C++ only makes a function a coroutine when it contains
`co_await`/`co_return`.

## Problem

The user's contract is "if a function is declared `async`, transform it to
be coroutine-compatible". Today a body with no `return` and no `.await`
is lowered to a plain function returning `Task<void>` that falls off its
end. That is undefined behaviour (review finding H1).

```cpp
async void f(int& x) { x++; }
int main(){ int x=0; asyncrt::block_on(f(x)); return x==1?0:1; }
```
asynctp output: `::asyncrt::Task<void> f(int& x) { x++; }`.

GCC 15 compiles it with only
`warning: no return statement in function returning non-void [-Wreturn-type]`,
and the program dies with `Illegal instruction` (exit 132).

It also breaks the "Task is lazy" invariant: even if it did not crash,
`x++` would run eagerly at the call.

## Scope

- In: after lowering, decide whether the function body (excluding nested ordinary lambdas) contains `co_await`, `co_return` or `co_yield`. If it does not:
  - `void` return type: insert ` co_return;` right before the closing `}`, on the same line (line-preserving).
  - Non-`void` return type: `ParseError` at the function name: `"async function '<name>' returns '<T>' but never returns a value or awaits; add a return statement"`.
- Out: declarations without a body (T-0006: nothing to do). Detecting `co_await` hidden in macros: a macro expansion we cannot see is the user's responsibility. The non-void case then fails loudly in GCC anyway.
- Touches: `transpiler/src/lowering.cpp` (`lower_function`), `lowering.hpp` comment, `lowering_tests`, golden fixture, a runnable example.

## Blueprint

1. Track whether a body is a coroutine in the T-0011 token walk:
   - set `has_coroutine_keyword = true` when, at `lambda_depth == 0`, an edit produces `co_return` or `co_await`;
   - also set it when an `Identifier` token is literally `co_await`, `co_return` or `co_yield` (the user wrote it by hand).
2. In `lower_function`, if it is still false:
   - Return type text (trimmed) equal to `void`: add an insertion edit of `" co_return;"` at `body_close_offset` (before the `}`).
     The source map records it as a zero-length-origin replacement mapped to the `}`.
   - Otherwise: return the `ParseError` above, with line/col of the name token.
3. Document in `lowering.hpp`: "every lowered async function is a coroutine. A `void` body without suspension points gets an implicit `co_return;`, so the call stays lazy."

### Invariants

- **Lazy Task:** restored for this case (the body runs only when awaited/driven), proven by a test that observes laziness.
- **Fail loudly:** the non-void case becomes a `ParseError` instead of UB.
- **Text-island / line-preserving:** the insertion adds no newline.
- **Other invariants:** unaffected.

## Acceptance criteria

- [ ] `lowering_tests`:
  - `async void f(int& x) { x++; }` lowers to the body `" x++;  co_return;"`. Pin the exact spacing chosen; `" x++; co_return;"` is acceptable if the implementation trims, but the test fixes one form.
  - `async void f() { auto l = [] { return; }; }` gets the insertion (the lambda's `return` does not count).
  - `async void f() { co_await g(); }` gets no insertion (hand-written keyword).
  - `async int f() { int x = 1; }` returns a `ParseError` at the col of `f`, with message containing `never returns a value`.
  - `async void f() { return; }` is unchanged apart from the `co_return` rewrite (no double insertion).
- [ ] Golden fixture `void_without_await`.
- [ ] Runnable example `examples/lazy_void.async.cpp`, registered in `examples/CMakeLists.txt` and the Makefile `EXAMPLES`. It does:
  ```cpp
  async void bump(int& x) { x++; }
  ```
  then `int x = 0; auto t = bump(x);` checks `x == 0` (lazy), `asyncrt::block_on(std::move(t));` checks `x == 1`, and returns 0 only if both hold.
  It must also pass under `verify.sh asan` (no UB).
- [ ] `lowering.hpp` comment updated. README: add to the "Using it" section one sentence that async functions are always lazy, even without `.await`. No limitation bullet existed.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Log

- 2026-10-17 architect: created under epic T-0009 (review finding H1); reproduced SIGILL (exit 132) for a no-await `async void`; ready.
