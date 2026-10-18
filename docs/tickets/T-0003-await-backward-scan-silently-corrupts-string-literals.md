---
id: T-0003
title: Define the .await operand grammar (chains, template args, keywords) and reject what it cannot parse
status: ready
area: transpiler
type: bug
priority: P1
depends_on: [T-0011]
created: 2026-10-17
---

Epic: T-0009. Depends on: T-0011 (token-based lowering).

## User story

As a C++ developer, I want `client.get<Json>(url).await.body().await`
and `co_yield x.await` to lower to exactly what I mean, or to be told
precisely what to parenthesize, so that `.await` reads like Rust and never
silently produces different C++.

## Problem

The operand of `.await` is found by a backward scan (`find_expression_start_backward`,
`transpiler/src/lowering.cpp`; token-based after T-0011, with the same
rules). The rules are too narrow and silently wrong.

Observed with `build/transpiler/asynctp <in> -o <out> --no-line-directives`:

| Input body | Output today (rc=0) |
|---|---|
| `return a().await.b().await;` | `co_return co_await (a())co_await (.b());` |
| `return get<int>().await;` | `co_return get<int>co_await (());` |
| `co_yield x.await; return 0;` | `co_await (co_yield x); co_return 0;` |
| `auto s = g(")").await;` | `auto s = g(")")co_await ();` (fixed by T-0011; regression test kept here) |

Review findings H4 (chained `.await`, template arguments) and N1 (`.await` swallows keywords).

## Scope

- In: a precise postfix-operand rule over tokens; parenthesisation of the result when more postfix operators follow; a `ParseError` for every operand the rule cannot classify.
- Out: making `.await` work inside non-async lambdas (an intentional limitation that mirrors Rust); async lambdas (T-0008).
- Touches: `transpiler/src/lowering.cpp`, `lowering.hpp` (comment), `lowering_tests.cpp`, golden fixture, README.

## Blueprint

1. **Lower through an edit list, not incremental copying.** `lower_body` collects edits
   `{offset, erase_len, insert_text, order}` over file offsets and applies them once at the end.
   Several edits can share an offset: nested `co_await (` insertions at the same operand start are ordered outer-first.
   This is needed for chains and is also what T-0012's source map records.
2. **Operand rule.** Walking backward from the `.` of `.await`, the operand is the longest postfix-expression made of:
   - `Identifier` tokens that are not keywords, plus `this`;
   - `.`, `->`, `::` between them;
   - balanced `( … )` and `[ … ]` groups (Punct-only matching; literals are atoms);
   - a **template-argument list** `< … >`. When the token before a consumed `(` group or `::` is a `>`, balance `>`/`<` backward (counting `(`/`[` groups as atoms). Accept it only if the token before the matching `<` is an `Identifier` or one of the named casts `static_cast`, `dynamic_cast`, `const_cast`, `reinterpret_cast`.
     This chooses the template reading for `a<b>(c).await`, which is the only sensible one when the result is awaited. Document that choice.
   - a **previously lowered `.await`** in the same chain (`X.await` is itself a postfix expression).
3. **Stop tokens** (not part of the operand):
   - any other Punct (`=`, `,`, `;`, `(`, `{`, unary/binary operators, `?`, `:`);
   - `Comment`, `Directive`;
   - an `Identifier` that is a C++ keyword other than `this` and the named casts (`return`, `co_return`, `co_yield`, `throw`, `case`, `new`, `delete`, `sizeof`, `alignof`, `typeid`, `not`, `and`, `or`, ...);
   - an `Identifier` immediately preceded by another non-keyword `Identifier` (a declaration like `Foo x`). Keep one shared keyword table in `lowering.cpp` (T-0005 reuses it).
4. **Keyword directly before the operand.** If the stop token is one of `new`, `delete`, `sizeof`, `alignof`, `typeid`, `co_yield`, `throw`, `co_await`, return
   `ParseError{at that keyword, "'.await' binds tighter than '<kw>'; write '(<kw> expr).await' or '<kw> (expr.await)' to say which you mean"}`.
   `return` and `co_return` are **not** errors: `return x.await` stays `co_return co_await (x)`.
5. **Emission.**
   - `X.await` becomes `co_await (X)`.
   - If the token after `await` is `.`, `->`, `[`, `(` or another `.await`, emit `(co_await (X))` so the following postfix operator applies to the awaited value: `a().await.b()` becomes `(co_await (a())).b()`.
   - `.await(` stays a member call named `await`, as today.
6. **Unary prefix operators** are already correct by C++ precedence (`!x.await` becomes `!co_await (x)`; `*p.await` becomes `*co_await (p)`). Pin them with tests; no special code.
7. Update the `lowering.hpp` comment with the operand rule, the template choice and the error cases. Update README.

### Invariants

- **Fail loudly:** the core of this ticket. Every operand is either fully classified or a `ParseError` naming the token and the fix. No path emits `co_await ()` (T-0011's backstop stays).
- **Text-island rewriter:** only `.await` and its operand are touched; edits are applied to verbatim slices.
- **Other invariants:** not affected.

## Acceptance criteria

- [ ] `lowering_tests`, exact body strings:
  - `return a().await.b().await;` gives `" co_return co_await ((co_await (a())).b()); "`.
  - `return x.await.await;` gives `" co_return co_await ((co_await (x))); "`.
  - `return get<int>().await;` gives `" co_return co_await (get<int>()); "`.
  - `return c.get<std::vector<std::pair<int,int>>>(u).await;` gives `" co_return co_await (c.get<std::vector<std::pair<int,int>>>(u)); "`.
  - `return static_cast<Task<int>&&>(t).await;` gives `" co_return co_await (static_cast<Task<int>&&>(t)); "`.
  - `return ns::f(1)[2].await;` gives `" co_return co_await (ns::f(1)[2]); "`.
  - `return this->t.await;` gives `" co_return co_await (this->t); "`.
  - `return !x.await;` gives `" co_return !co_await (x); "`, and `return *p.await;` gives `" co_return *co_await (p); "`.
  - `auto s = g(")").await;` gives `" auto s = co_await (g(\")\")); "` (kept from the original T-0003).
  - `auto m = m["k)"].await;` gives `" auto m = co_await (m[\"k)\"]); "`.
  - `auto r = R"x()")x".size(), y.await;` gives `" auto r = R\"x()\")x\".size(), co_await (y); "`.
  - `int v = x.await + y.await;` gives `" int v = co_await (x) + co_await (y); "`.
  - `x.await(1);` is unchanged (member call).
- [ ] `lowering_tests`, error cases returning a `ParseError` with the keyword's line/col and a message containing `binds tighter`:
  - `co_yield x.await;`
  - `throw e.await;`
  - `auto p = new Foo().await;`
  - `auto n = sizeof x.await;`
- [ ] Golden fixture `await_chains`, containing the first five success cases above as one async function per case. The expected output is generated after the fix and checked by eye against the rule in Blueprint step 5.
- [ ] Runnable example `examples/await_chains.async.cpp` (renamed with the others by T-0017), registered in `examples/CMakeLists.txt` (`add_async_executable` + `add_test`) and in the Makefile `EXAMPLES`.
  - It must chain `.await` through a member returning a `Task`, call a templated async function with explicit template arguments, and return 0 only if every result matches.
  - This proves the parenthesisation compiles.
- [ ] README "Known MVP limitations": replace the `.await` backward-scan bullet with the operand rule summary and the keyword error. Update the `lowering.hpp` comment to match.
- [ ] `.claude/skills/verify/scripts/verify.sh all` passes.

## Notes

- The original 2026-10-17 scope (string literals only) is absorbed. T-0011 makes literals atomic, and this ticket owns the grammar.
- Template reading of `a < b > (c).await`: a comparison reading would await `(c)` and compare booleans with it, which is never meaningful. The rule picks the template reading and the tests pin it.

## Log

- 2026-10-17 architect: reproduced; needs design choice (forward vs backward) before ready.
- 2026-10-17 architect: re-scoped under epic T-0009 to the full operand grammar (H4, N1 plus the original string case). The design choice is token-based backward scan plus edit list. Reproduced chain/template/co_yield outputs; depends on T-0011; ready.
