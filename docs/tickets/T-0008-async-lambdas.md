---
id: T-0008
title: Async lambdas / closures
status: proposed
area: transpiler
type: feature
priority: P3
depends_on: [T-0004]
created: 2026-10-17
---

Epic: none (independent of T-0009). Depends on: T-0004 (must extend its marker rule 2, which reserves `async [` and treats it as a plain identifier until this ticket lands).

## User story

As a C++ developer, I want `auto job = async [x]() -> int { return f(x).await; };`
so that I can make ad-hoc tasks for `spawn`/`join` without declaring a named
function.

## Problem

`.await` inside a lambda is left untouched today. That matches Rust's rule
for non-async closures, but there's no async version of a lambda.

## Scope (architect to refine)

- Syntax decision first (`async [..](..) {}` vs `[..](..) async {}`).
  It must not collide with T-0004's marker classification.
- Lifetime hazard: a coroutine lambda's captures live in the closure object,
  not in the coroutine frame. A temporary lambda that is called and then
  destroyed leaves dangling captures. The design must prevent this, for
  example by lowering to a by-value parameter pack or a helper that moves the
  closure into the frame. Otherwise the feature is a footgun and should be
  rejected.

## Acceptance criteria

- [ ] Design note in this ticket explaining the lifetime solution, with an
      ASan-verified test showing a temporary async lambda is safe.
- [ ] Lexer/parser/lowering tests + golden fixture.

## Log

- 2026-10-17 architect: proposed (MVP out-of-scope item from README).
- 2026-10-17 architect: triaged under epic T-0009. Still proposed. Added the dependency on T-0004's classification table; the syntax decision must also keep T-0020 tolerant-mode recovery working.
