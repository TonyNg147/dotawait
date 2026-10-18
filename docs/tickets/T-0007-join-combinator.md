---
id: T-0007
title: join combinator to await several tasks concurrently
status: proposed
area: runtime
type: feature
priority: P3
depends_on: []
created: 2026-10-17
---

Epic: none (independent of T-0009; runtime-only).

## User story

As a C++ developer, I want `auto [a, b] = asyncrt::join(fa(), fb()).await;`
so that I can run independent tasks concurrently without `spawn` + shared
state, the way Rust's `join!` works.

## Scope (architect to refine)

- Runtime-only and header-only: `asyncrt::join(Task<Ts>...) -> Task<std::tuple<Ts...>>`
  (`void` maps to an empty marker type). Each child is scheduled on the
  executor's ready queue, and the parent resumes when the last child finishes.
- Exceptions: the first one is rethrown once all children have finished.
  Never leave a child frame dangling.
- No transpiler change. `join!` macro syntax is out of scope until this API exists.

## Acceptance criteria

- [ ] `executor_tests`: interleaving order with `yield_now` inside children,
      a `void` child, an exception in one child, an empty `join()` (or
      disallowed at compile time).
- [ ] ASan clean (frame ownership is the risk here).
- [ ] README runtime API table updated; new example `join.async.cpp` in CMake + Makefile.

## Log

- 2026-10-17 architect: proposed (MVP out-of-scope item from README).
- 2026-10-17 architect: triaged under epic T-0009. Unaffected (runtime-only). If it lands after T-0017, name its example `examples/join.cpp` instead of `join.async.cpp`. Stays proposed.
