---
id: T-0005
title: Lambda-start detection misreads [ after most keywords
status: ready
area: transpiler
type: limitation
priority: P2
depends_on: [T-0011]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0011 (token-based lowering). Shares the keyword table with T-0003; whichever lands second reuses it.

## User story

As a C++ developer, I want `else [x]{ ... }();`, `do [&]{ ... }();` and
`co_yield [x]{ ... }` inside an async function to keep the lambda's own
`return` untouched, so that lambdas behave the same whatever keyword precedes them.

## Problem

Lambda-start detection decides "lambda or subscript" from the token before
`[`. Only `return`, `throw` and `co_return` count as expression-position
keywords. After any other keyword the `[` is read as a subscript, so the
lambda body is not a separate frame and its inner `return` is rewritten to
`co_return`.

Observed with `build/transpiler/asynctp <in> -o <out> --no-line-directives` (rc=0 in every case):

| Body of `async int f(bool c) { ... }` | Output |
|---|---|
| `if (c) {} else [c]{ return 1; }(); return 0;` | `if (c) {} else [c]{ co_return 1; }(); co_return 0;` |
| `int n = 0; do [&]{ return n++; }(); while (n < 2); return n;` | `int n = 0; do [&]{ co_return n++; }(); while (n < 2); co_return n;` |
| `if (c) return 0; else return [c]{ return 1; }();` | correct (inner `return` kept), because `return` is on the allow-list |

The lambda's `co_return` makes GCC reject the non-coroutine lambda. The
user wrote ordinary lambda code and gets an error about a keyword they
never typed.

(The original second bullet, `")"[0]` failing with "unterminated lambda
expression", is fixed by T-0011: a literal before `[` means subscript.)

## Scope

- In: replace the three-keyword allow-list with "every C++ keyword except
  `this` puts the following `[` in expression position", using the shared
  keyword table.
- Out: attribute `[[` handling (not reachable inside a body as a lambda start), Objective-C++.
- Touches: `transpiler/src/lowering.cpp` (lambda-start check), `lowering.hpp` comment, `lowering_tests.cpp`, README.

## Blueprint

1. In the token-based lambda-start check (T-0011 step 3), the previous token is an `Identifier`:
   - keyword other than `this`: lambda;
   - `this`, or any non-keyword identifier: subscript.
2. The keyword table is one `constexpr std::array<std::string_view, N>` (sorted, `std::binary_search`) in `lowering.cpp`'s anonymous namespace, shared with T-0003's stop-token rule.
   Contents: every C++20 keyword plus the alternative tokens (`and`, `or`, `not`, `bitand`, `bitor`, `compl`, `xor`, `and_eq`, `or_eq`, `xor_eq`, `not_eq`).
   Do **not** include contextual identifiers (`override`, `final`, `import`, `module`).
3. Update the `lowering.hpp` comment: remove the "small fixed set of keywords" limitation.

### Invariants

- **Fail loudly:** the rule becomes total, with no silent misclassification for keywords. Unknown identifiers default to subscript, which is correct for every non-keyword identifier.
- **Other invariants:** unaffected (transpiler-only, no new files).

## Acceptance criteria

- [ ] `lowering_tests`, exact body strings:
  - `if (c) {} else [c]{ return 1; }(); return 0;` gives `" if (c) {} else [c]{ return 1; }(); co_return 0; "`.
  - `int n = 0; do [&]{ return n++; }(); while (n < 2); return n;` gives the lambda's `return n++` unchanged and the final `co_return n;`.
  - `if (c) return 0; else return [c]{ return 1; }();` keeps the inner `return 1` (regression guard for the old allow-list).
  - `co_yield [x]{ return x; }();` gives the inner `return x` unchanged. The test checks lambda detection only; it does not need to compile.
  - `case 1: return [k]{ return k; }();` inside a `switch`: the inner `return` is unchanged.
  - `auto v = this[0];` is a subscript (no lambda frame).
  - `auto v = arr[0];` is a subscript.
  - `auto w = not [x]{ return x; }();` gives the inner `return` unchanged.
- [ ] README "Known MVP limitations": delete the lambda-start bullet. `lowering.hpp` comment updated.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Log

- 2026-10-17 architect: reproduced `")"[0]` failure; proposed.
- 2026-10-17 architect: under epic T-0009, the literal half moved into T-0011 (token semantics). Narrowed this ticket to the keyword table, made it depend on T-0011, and moved it to ready.
