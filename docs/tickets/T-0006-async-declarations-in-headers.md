---
id: T-0006
title: Parse async declarations, qualified names, and bounded signatures
status: ready
area: transpiler
type: feature
priority: P1
depends_on: [T-0010, T-0004]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0010 (tokens), T-0004 (marker classification). Must follow T-0012's line-preserving rule if T-0012 has landed. T-0002 builds on this ticket's declarator parser.

## User story

As a C++ developer, I want to write `async int fetch(int id);` in a header,
`async int Client::fetch(int id) { ... }` in the `.cpp`, and
`struct Client { async int fetch(int id); };`, and get either correct
lowering or a precise error, so that async APIs can be split across headers
and classes like any other C++ API.

## Problem

Under the T-0009 contract, async declarations in headers are mandatory:
the launcher lowers project headers (T-0014), and the LSP proxy lowers
them for clangd (T-0024). The parser (`parse_one`, `transpiler/src/async_parser.cpp`)
only knows "first identifier followed by `(...)` followed by `{`". Its
search is unbounded (review finding H3).

Observed with `build/transpiler/asynctp <in> -o <out> --no-line-directives`:

| Input | Result |
|---|---|
| `async int fetch(int id);` | `decl.cpp:1:1: error: could not find a function name, parameter list, and body after 'async'` |
| `struct Foo { async int bar(); };` + newline + `async int Foo::bar() { return 1; }` | rc=0, **silent garbage**: `struct Foo { ::asyncrt::Task<int bar(); };` / `async int Foo::> bar() { co_return 1; }` |
| `async int v = compute();` + newline + `int main() { return 0; }` | rc=0, **silent garbage**: `::asyncrt::Task<int v = compute();` / `int> main() { co_return 0; }` |

`examples/consumer/src/jobs/jobs.hpp` spells `asyncrt::Task<int> download(...)`
by hand: exactly the boilerplate the product exists to remove.

## Scope

- In:
  - A bounded declarator parser over tokens that recognises:
    - a definition: `async T name(params) { body }`;
    - a declaration: `async T name(params);`, plus `= 0;` and `= delete;`;
    - qualified names: `ns::Foo::bar`, `Foo<T>::bar`.
  - Precise `ParseError`s for every non-function or unsupported form.
- Out:
  - The specifier tail between `)` and `{`/`;` (`noexcept`, `const`, `override`, `&`, `-> T`). That is T-0002. Until it lands, a non-empty tail is a `ParseError` naming T-0002's feature.
  - `requires` clauses; `operator` functions; the build/LSP plumbing that feeds headers in (T-0014, T-0024).
- Touches:
  - `ast.hpp`, `async_parser.hpp/.cpp`, `lowering.cpp` (`lower_function`), `emitter.cpp`
  - `parser_tests`, `lowering_tests`, golden fixtures, one runnable example
  - README, `async_parser.hpp` comment

## Blueprint

1. **`ast.hpp`:**
   - `AsyncFunction` gains `bool has_body;` and `std::string_view terminator;`, the verbatim `;`, `= 0;` or `= delete;` for declarations.
   - `name` may now be qualified (a view from the first qualifier to the final identifier).
   - Body fields are unset (0 / empty) when `!has_body`.
   - Rename `body_close_offset` to a general `end_offset` (the last byte of the island: `}` or `;`). Update the emitter and T-0011/T-0012 call sites.
2. **Declarator parser** (`parse_one`, over tokens from the marker). Walk forward with paren depth (`(`/`[`) and angle depth (`<`/`>`; only tracked at paren depth 0) to find the declarator-id.
   - **Declarator-id:** a run `[::] Identifier ( [<…>] :: Identifier )*` at depth 0, immediately followed by a `(` group.
   - **Candidates:** the *last* such run before the first depth-0 `{`, `;` or `=` is the name. This is how `decltype(x) f()` and `std::function<int(int)> f()` work: `x` and `int` are inside parens/angles and never count.
   - **Bound (H3):** the search stops at the first depth-0 `;`, `{`, `}` or `=` that is not immediately after the candidate's `)`. If no candidate has been found by then:
     `ParseError{at async, "expected a function declarator after 'async' (found '<tok>' at L:C)"}`.
     This replaces the unbounded scan that swallowed `main` in the example above.
   - **After the candidate's `)`, exactly one of:**
     - `{` → definition;
     - `;` → declaration;
     - `= 0 ;` or `= delete ;` → declaration;
     - anything else → `ParseError{at token, "unsupported tokens between ')' and the body; noexcept/const/override/'-> T' need T-0002"}`. T-0002 replaces this branch.
3. **Return-type checks** (all `ParseError` at the offending token):
   - Empty return type (`async Foo()`): `"async function needs a return type (constructors and destructors cannot be coroutines)"`.
   - `auto` or `decltype(auto)` as the whole return type without a trailing return: `"async functions need an explicit result type; write it, or use 'auto f() -> T' (T-0002)"`.
   - A decl-specifier inside the return type (`static`, `inline`, `virtual`, `friend`, `extern`, `explicit`): `"put '<kw>' before 'async'"`.
   - `constexpr` or `consteval` anywhere in the return type: `"a coroutine cannot be constexpr/consteval"` ([dcl.constexpr]).
   - An attribute `[[...]]` in the return type: `"put attributes before 'async'"`.
   - Name `main` (unqualified): `"main cannot be a coroutine"` ([basic.start.main]).
4. **Lowering** (`lower_function`):
   - Definition: unchanged, except it uses the qualified `name`.
   - Declaration: `::asyncrt::Task<RT> name(params)` + newlines (to preserve line count) + `terminator`. There is no body, and T-0013's coroutine check is skipped.
5. **Docs:** update the `async_parser.hpp` comment (algorithm, bound, error list) and README (Blueprint step 2 of T-0017 rewrites the limitation list; here, delete the "Async declarations in headers … aren't supported" bullet and narrow the function-name-heuristic bullet).

### Invariants

- **Fail loudly:** both silent-garbage cases above become `ParseError`s (bounded search) or correct output (qualified names).
- **Text-island rewriter:** islands now also end at `;`. Text outside islands is untouched.
- **Line-preserving (T-0012):** declarations keep newline counts.
- **Other invariants:** unaffected (transpiler-only).

## Acceptance criteria

- [ ] `parser_tests`, each with exact `name`, `return_type`, `params`, `has_body` and `terminator`:
  - `async int fetch(int id);`
  - `async std::map<int, std::string> load();`
  - `async int Foo::bar() { return 1; }` (name `Foo::bar`)
  - `async int ns::Foo<T>::bar(T t) { return 1; }`
  - `struct Foo { async int bar(); virtual async int baz() = 0; async int q() = delete; };` (3 declarations)
  - `async decltype(g()) f() { return g(); }` (name `f`)
  - `async std::function<int(int)> make();` (name `make`)
- [ ] `parser_tests`, error cases (assert line/col and a message substring):
  - `async int v = compute();\nint main() { return 0; }` gives `expected a function declarator` at 1:1.
  - `async Foo() {}` gives `needs a return type`.
  - `async auto f() { return 1; }` gives `explicit result type`.
  - `async static int f() {}` gives `put 'static' before 'async'`.
  - `async constexpr int f() {}` gives `cannot be constexpr`.
  - `async int main() { return 0; }` gives `main cannot be a coroutine`.
  - `async int f() noexcept {}` gives `need T-0002`, until T-0002 lands, at which point T-0002 changes this test.
- [ ] `lowering_tests`: `async int fetch(int id);` gives `::asyncrt::Task<int> fetch(int id);`. The in-class `= 0` case gives `virtual ::asyncrt::Task<int> baz() = 0;`.
- [ ] Golden fixture `declarations_and_qualified`: the struct with in-class declarations, a free declaration, and the out-of-line `Foo::bar` definition.
- [ ] Runnable example `examples/declarations.async.cpp` (renamed by T-0017), registered in `examples/CMakeLists.txt` and the Makefile `EXAMPLES`. It contains:
  - a forward declaration `async int twice(int);` used before its definition;
  - a class with an in-class `async` member declaration defined out-of-line;
  - a pure-virtual `async` member overridden in a derived class (`async int run() override` needs T-0002; until then the derived class defines it without `override`).

  `main` checks results via `block_on` and returns 0.
- [ ] README: delete the "Async declarations in headers (no body) aren't supported" bullet; reword the function-name heuristic bullet per the new bound. Update the `async_parser.hpp` comment.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

- **Superseded scope:** the original Option A (`.async.hpp` files) and Option B (document `Task<T>`) are both obsolete. The user's T-0009 contract makes header support mandatory and removes the suffix. The build side lives in T-0014/T-0015, the consumer example in T-0017, and the LSP side in T-0024.
- Virtual coroutines are fine: the `Task<T>` return type is what the override must match.

## Log

- 2026-10-17 architect: proposed; needs Option A/B decision.
- 2026-10-17 architect: re-scoped under epic T-0009. Header declarations are now mandatory; Options A/B are obsolete. Reproduced the declaration error and two silent-garbage cases (in-class declaration + qualified definition; unbounded name search). Now covers the declarator parser, bounded search (H3), qualified names and specifier errors; depends on T-0010 and T-0004; ready.
